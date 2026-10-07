package dev.goldcraft.world;

import com.google.gson.JsonObject;
import dev.goldcraft.GoldCraft;
import dev.goldcraft.bridge.HostWorldState;
import dev.goldcraft.bridge.Wire;
import net.minecraft.block.BlockState;
import net.minecraft.entity.Entity;
import net.minecraft.entity.LivingEntity;
import net.minecraft.entity.damage.DamageSource;
import net.minecraft.entity.damage.DamageTypes;
import net.minecraft.entity.player.PlayerEntity;
import net.minecraft.registry.RegistryKeys;
import net.minecraft.server.MinecraftServer;
import net.minecraft.server.network.ServerPlayerEntity;
import net.minecraft.server.world.ServerWorld;
import net.minecraft.util.Hand;
import net.minecraft.util.hit.BlockHitResult;
import net.minecraft.util.math.BlockPos;
import net.minecraft.util.math.Box;
import net.minecraft.util.math.Direction;
import net.minecraft.util.math.Vec3d;
import net.minecraft.world.chunk.WorldChunk;
import java.util.*;

/** Minecraft owns state; native edicts supply real GoldSrc movement, shots, explosions and Use. */
public final class MinecraftObjects {
    public static final int LIMIT=512;
    public static final float CS_HEALTH_PER_MC=5;
    private static ServerWorld world;
    private static long epoch,revision,nextKey,lastAction,damageActions,useActions,rejected,broken;
    private static int candidates,exported;
    private static final Map<Long,WorldChunk> dirty=new LinkedHashMap<>();
    private static final Map<Long,Map<BlockPos,BlockTarget>> chunks=new LinkedHashMap<>();
    private static final Map<UUID,Long> entityKeys=new HashMap<>();
    private static Map<Long,Target> targets=Map.of();
    private sealed interface Target permits BlockTarget,MobTarget {}
    private static final class BlockTarget implements Target {
        final BlockPos pos;final BlockState state;final List<Box> boxes;final long[] keys;
        float damage;
        BlockTarget(BlockPos pos,BlockState state,List<Box> boxes){
            this.pos=pos;this.state=state;this.boxes=boxes;keys=new long[boxes.size()];
            for(int i=0;i<keys.length;i++)keys[i]=++nextKey;
        }
        float strength(){float h=state.getHardness(world,pos);return h<0?1000000:Math.max(1,h*50);}
    }
    private record MobTarget(Entity entity) implements Target {}
    private record Shape(long key,int flags,float health,Box box,Target target) {}
    private MinecraftObjects(){}
    public static long key(Entity entity){return entity==null?0:entityKeys.getOrDefault(entity.getUuid(),0L);}
    public static void initialize(){
        net.neoforged.neoforge.common.NeoForge.EVENT_BUS.addListener((net.neoforged.neoforge.event.level.ChunkEvent.Load event)->{
            if(event.getLevel()==world&&event.getChunk() instanceof WorldChunk c)dirty.put(c.getPos().toLong(),c);
        });
        net.neoforged.neoforge.common.NeoForge.EVENT_BUS.addListener((net.neoforged.neoforge.event.level.ChunkEvent.Unload event)->{
            if(event.getLevel()==world){long pos=event.getChunk().getPos().toLong();dirty.remove(pos);chunks.remove(pos);}
        });
    }
    public static void changed(WorldChunk chunk){
        if(chunk.getWorld()==world&&!HostWorldReset.clearing())dirty.put(chunk.getPos().toLong(),chunk);
    }
    public static void clear(){
        world=null;epoch=revision=nextKey=lastAction=damageActions=useActions=rejected=broken=0;
        dirty.clear();chunks.clear();entityKeys.clear();targets=Map.of();candidates=exported=0;
    }
    private static void scan(WorldChunk chunk){
        var previous=chunks.getOrDefault(chunk.getPos().toLong(),Map.of());
        Map<BlockPos,BlockTarget> result=new LinkedHashMap<>();
        var sections=chunk.getSectionArray();int sx=chunk.getPos().getStartX(),sz=chunk.getPos().getStartZ();
        for(int i=0;i<sections.length;i++){
            var section=sections[i];if(section.isEmpty())continue;int sy=chunk.getBottomY()+16*i;
            for(int y=0;y<16;y++)for(int z=0;z<16;z++)for(int x=0;x<16;x++){
                var state=section.getBlockState(x,y,z);if(state.isAir())continue;
                var pos=new BlockPos(sx+x,sy+y,sz+z);
                var boxes=state.getCollisionShape(world,pos).getBoundingBoxes().stream().map(b->b.offset(pos)).toList();
                if(boxes.isEmpty())continue;
                var old=previous.get(pos);
                result.put(pos,old!=null&&old.state==state&&old.boxes.equals(boxes)?old:new BlockTarget(pos,state,boxes));
            }
        }
        chunks.put(chunk.getPos().toLong(),result);
    }
    private static double distance(Box box,List<Vec3d> players){
        double closest=Double.MAX_VALUE;
        for(var p:players){double x=Math.max(box.minX-p.x,Math.max(0,p.x-box.maxX)),y=Math.max(box.minY-p.y,Math.max(0,p.y-box.maxY)),z=Math.max(box.minZ-p.z,Math.max(0,p.z-box.maxZ));closest=Math.min(closest,x*x+y*y+z*z);}
        return closest;
    }
    public static void tick(MinecraftServer server,HostWorldState host){
        var nextWorld=HostSession.hostWorld(server,host);if(nextWorld==null)return;
        if(world!=nextWorld||epoch!=host.epoch()){
            clear();world=nextWorld;epoch=host.epoch();
            for(var c:HostWorldReset.loadedChunks(world))dirty.put(c.getPos().toLong(),c);
        }
        for(int i=0;i<8&&!dirty.isEmpty();i++){
            var it=dirty.entrySet().iterator();var c=it.next().getValue();it.remove();scan(c);
        }
        List<Vec3d> players=host.actors().stream().filter(a->(a.flags()&1)!=0).map(a->{var p=a.minecraftFeet();return new Vec3d(p.x(),p.y(),p.z());}).toList();
        List<Shape> shapes=new ArrayList<>();
        for(var chunk:chunks.values())for(var b:chunk.values())for(int i=0;i<b.keys.length;i++){
            if(distance(b.boxes.get(i),players)<128*128)shapes.add(new Shape(b.keys[i],1|4,Math.max(1,b.strength()-b.damage),b.boxes.get(i),b));
        }
        Set<UUID> living=new HashSet<>();
        for(var e:world.iterateEntities()){
            if(e instanceof PlayerEntity||e instanceof NativePlayerHitboxEntity||!(e instanceof LivingEntity)||!e.isAlive()||e.isRemoved()||!e.canHit())continue;
            living.add(e.getUuid());long key=entityKeys.computeIfAbsent(e.getUuid(),ignored->++nextKey);
            if(distance(e.getBoundingBox(),players)<128*128)shapes.add(new Shape(key,2|4,((LivingEntity)e).getHealth()*CS_HEALTH_PER_MC,e.getBoundingBox(),new MobTarget(e)));
        }
        entityKeys.keySet().retainAll(living);
        shapes.sort(Comparator.comparingDouble(s->distance(s.box,players)));
        candidates=shapes.size();exported=Math.min(LIMIT,candidates);
        Map<Long,Target> nextTargets=new HashMap<>();
        Wire.Writer w=new Wire.Writer().i64(epoch).i64(++revision).i32(exported);
        for(int i=0;i<exported;i++){
            var s=shapes.get(i);var b=s.box;nextTargets.put(s.key,s.target);
            w.i64(s.key).i32(s.flags).f32(s.health)
                .f32((float)(b.minX*32)).f32((float)(-b.maxZ*32)).f32((float)((b.minY-64)*32))
                .f32((float)(b.maxX*32)).f32((float)(-b.minZ*32)).f32((float)((b.maxY-64)*32));
        }
        if(GoldCraft.sendToHost(Wire.MINECRAFT_OBJECTS,w.toByteArray()))targets=nextTargets;
    }
    public static void action(MinecraftServer server,HostWorldState host,byte[] payload){
        Wire.Reader r=new Wire.Reader(payload);long e=r.i64(),event=r.i64(),key=r.i64();
        int action=r.i32(),slot=r.i32(),serial=r.i32(),life=r.i32();float amount=r.f32();int bits=r.i32();
        Vec3d source=new Vec3d(r.f32()/32.0,0,0);float gy=r.f32(),gz=r.f32();source=new Vec3d(source.x,gz/32.0+64,-gy/32.0);r.finish();
        if(world==null||e!=epoch||event<=lastAction||action<1||action>2||amount<0||amount>1000000){rejected++;return;}
        lastAction=event;
        var target=targets.get(key);var actor=host.actors().stream().filter(a->a.slot()==slot).findFirst().orElse(null);
        if(target==null||(slot!=0&&(actor==null||actor.serial()!=serial||actor.life()!=life))){rejected++;return;}
        ServerPlayerEntity player=actor==null?null:server.getPlayerManager().getPlayer(actor.minecraftPlayer());
        if(action==2){
            if(player==null||!player.isAlive()){rejected++;return;}
            if(target instanceof BlockTarget b&&world.getBlockState(b.pos)==b.state){
                var from=actor.minecraftFeet();var eye=new Vec3d(from.x(),from.y()+1.62,from.z());
                var center=b.boxes.getFirst().getCenter();if(eye.squaredDistanceTo(center)>16){rejected++;return;}
                var delta=eye.subtract(center);var face=Direction.getFacing(delta.x,delta.y,delta.z);
                b.state.onUse(world,player,new BlockHitResult(center,face,b.pos,false));useActions++;
            }else if(target instanceof MobTarget m&&!m.entity.isRemoved()) {m.entity.interact(player,Hand.MAIN_HAND);useActions++;}
            return;
        }
        if(amount<=0)return;
        if(target instanceof BlockTarget b){
            if(world.getBlockState(b.pos)!=b.state||b.state.getHardness(world,b.pos)<0){rejected++;return;}
            b.damage+=amount;damageActions++;
            if(b.damage>=b.strength()&&world.breakBlock(b.pos,true,player)){broken++;}
        }else if(target instanceof MobTarget m&&m.entity.isAlive()&&!m.entity.isRemoved()){
            var type=(bits&64)!=0?DamageTypes.EXPLOSION:(bits&2)!=0?DamageTypes.ARROW:DamageTypes.PLAYER_ATTACK;
            var attacker=NativePlayers.attacker(server,host,actor);
            var damage=new DamageSource(world.getRegistryManager().get(RegistryKeys.DAMAGE_TYPE).entryOf(type),attacker);
            // Each event came from a real native TakeDamage invocation. Preserve CS
            // weapon cadence instead of dropping equal hits during MC's 10-tick immunity.
            m.entity.timeUntilRegen=0;
            if(m.entity.damage(damage,amount/CS_HEALTH_PER_MC))damageActions++;
        }
    }
    public static void diagnostics(JsonObject data){
        JsonObject o=new JsonObject();o.addProperty("revision",revision);o.addProperty("candidates",candidates);o.addProperty("exported",exported);
        o.addProperty("overflow",Math.max(0,candidates-exported));o.addProperty("damageActions",damageActions);o.addProperty("useActions",useActions);
        o.addProperty("brokenBlocks",broken);o.addProperty("rejectedActions",rejected);o.addProperty("pendingChunks",dirty.size());data.add("minecraftObjects",o);
    }
}
