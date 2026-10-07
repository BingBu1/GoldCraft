package dev.goldcraft.world;

import com.google.gson.JsonObject;
import dev.goldcraft.GoldCraft;
import dev.goldcraft.bridge.HostWorldState;
import dev.goldcraft.bridge.Wire;
import net.minecraft.entity.Entity;
import net.minecraft.entity.LivingEntity;
import net.minecraft.entity.damage.DamageSource;
import net.minecraft.entity.mob.Angerable;
import net.minecraft.entity.mob.MobEntity;
import net.minecraft.entity.player.PlayerEntity;
import net.minecraft.registry.tag.DamageTypeTags;
import net.minecraft.server.MinecraftServer;
import net.minecraft.server.world.ChunkTicketType;
import net.minecraft.server.world.ServerWorld;
import net.minecraft.util.math.ChunkPos;
import java.util.*;

/** Native player identities are visible to vanilla targeting even without a paired Minecraft client. */
public final class NativePlayers {
    private static final ChunkTicketType<Integer> TICKET=ChunkTicketType.create("goldcraft_native_player",Comparator.<Integer>naturalOrder(),40);
    private static final Map<Integer,NativePlayerEntity> players=new HashMap<>();
    private static final Map<Integer,NativePlayerHitboxEntity> hitboxes=new HashMap<>();
    private static final Map<Integer,ChunkPos> tickets=new HashMap<>();
    private static ServerWorld world;
    private static long epoch,sequence,requests,accepted,rejected;
    private static final Map<Long,NativePlayerEntity> pending=new LinkedHashMap<>();
    private NativePlayers(){}

    public static void clear(){
        for(var p:players.values())p.discard();
        for(var p:hitboxes.values())p.discard();
        if(world!=null)for(var t:tickets.entrySet())world.getChunkManager().removeTicket(TICKET,t.getValue(),3,t.getKey());
        players.clear();hitboxes.clear();tickets.clear();pending.clear();world=null;epoch=sequence=requests=accepted=rejected=0;
    }
    private static boolean alive(HostWorldState.Actor actor){return actor.life()!=0&&(actor.flags()&5)==1&&(actor.team()==1||actor.team()==2)&&actor.health()>0;}
    private static void transferTargets(LivingEntity from,LivingEntity to){
        if(from==null||from==to)return;
        for(var entity:from.getWorld().getEntitiesByClass(MobEntity.class,from.getBoundingBox().expand(128),e->true)){
            if(entity.getTarget()==from)entity.setTarget(to);
            if(entity.getAttacker()==from)entity.setAttacker(to);
            if(entity instanceof Angerable anger&&from.getUuid().equals(anger.getAngryAt())){
                if(to==null)anger.stopAnger();else anger.setAngryAt(to.getUuid());
            }
        }
    }
    public static PlayerEntity attacker(MinecraftServer server,HostWorldState host,HostWorldState.Actor actor){
        if(actor==null||!alive(actor))return null;
        var paired=server.getPlayerManager().getPlayer(actor.minecraftPlayer());
        if(actor.minecraftForm()&&(actor.flags()&32)!=0&&paired!=null&&paired.isAlive()&&!paired.isSpectator())return paired;
        var targetWorld=HostSession.hostWorld(server,host);if(targetWorld==null)return null;
        if(world!=targetWorld||epoch!=host.epoch()){clear();world=targetWorld;epoch=host.epoch();}
        var proxy=players.get(actor.slot());
        if(proxy!=null&&(proxy.actor().serial()!=actor.serial()||proxy.actor().life()!=actor.life()||proxy.isRemoved())){
            boolean formChange=proxy.actor().serial()==actor.serial()&&proxy.actor().minecraftForm()!=actor.minecraftForm();
            if(!formChange)transferTargets(proxy,null);
            proxy.discard();players.remove(actor.slot());
            var replacement=new NativePlayerEntity(world,epoch,actor,NativePlayers::damage);
            players.put(actor.slot(),replacement);world.spawnEntity(replacement);
            if(formChange)transferTargets(proxy,replacement);
            proxy=replacement;
        }
        if(proxy==null){
            proxy=new NativePlayerEntity(world,epoch,actor,NativePlayers::damage);players.put(actor.slot(),proxy);world.spawnEntity(proxy);
            if(paired!=null)transferTargets(paired,proxy);
        }else proxy.update(actor);
        return proxy;
    }
    public static void tick(MinecraftServer server,HostWorldState host){
        var targetWorld=HostSession.hostWorld(server,host);if(targetWorld==null){clear();return;}
        if(world!=targetWorld||epoch!=host.epoch()){clear();world=targetWorld;epoch=host.epoch();}
        Set<Integer> retained=new HashSet<>();
        for(var actor:host.actors()){
            if(!alive(actor))continue;
            PlayerEntity body=attacker(server,host,actor);
            if(!(body instanceof NativePlayerEntity)){
                var previous=players.remove(actor.slot());if(previous!=null){transferTargets(previous,body);previous.discard();}
                continue;
            }
            retained.add(actor.slot());ChunkPos chunk=body.getChunkPos(),old=tickets.put(actor.slot(),chunk);
            var hitbox=hitboxes.get(actor.slot());
            if(hitbox==null||hitbox.isRemoved()){
                hitbox=new NativePlayerHitboxEntity(NativePlayerHitboxEntity.TYPE.get(),world);
                hitboxes.put(actor.slot(),hitbox);hitbox.follow((NativePlayerEntity)body);world.spawnEntity(hitbox);
            }else hitbox.follow((NativePlayerEntity)body);
            if(old!=null&&!old.equals(chunk))world.getChunkManager().removeTicket(TICKET,old,3,actor.slot());
            world.getChunkManager().addTicket(TICKET,chunk,3,actor.slot());
        }
        for(var it=players.entrySet().iterator();it.hasNext();){var p=it.next();if(!retained.contains(p.getKey())){transferTargets(p.getValue(),null);p.getValue().discard();it.remove();}}
        for(var it=hitboxes.entrySet().iterator();it.hasNext();){var p=it.next();if(!retained.contains(p.getKey())){p.getValue().discard();it.remove();}}
        for(var it=tickets.entrySet().iterator();it.hasNext();){var t=it.next();if(!retained.contains(t.getKey())){world.getChunkManager().removeTicket(TICKET,t.getValue(),3,t.getKey());it.remove();}}
        if(!players.isEmpty())world.resetIdleTimeout();
        pending.entrySet().removeIf(e->e.getValue().isRemoved());
        while(pending.size()>256)pending.remove(pending.keySet().iterator().next());
    }
    public static List<? extends PlayerEntity> targetingPlayers(ServerWorld view,List<? extends PlayerEntity> vanilla){
        if(view!=world||players.isEmpty())return vanilla;
        List<PlayerEntity> result=new ArrayList<>(vanilla);
        for(var p:players.values())if(!p.isRemoved()&&p.isAlive())result.add(p);
        return result;
    }
    private static boolean damage(NativePlayerEntity target,DamageSource source,float amount){
        Entity attacker=source.getAttacker();
        long key=MinecraftObjects.key(attacker);
        var playerSource=attacker instanceof net.minecraft.server.network.ServerPlayerEntity p?GoldCraft.HOST_WORLD.actor(p.getUuid()):null;
        if(target.epoch()!=epoch||target.isRemoved()||(key==0&&playerSource==null)||amount>1000)return false;
        int kind=source.isIn(DamageTypeTags.IS_EXPLOSION)?3:source.isIn(DamageTypeTags.IS_PROJECTILE)?2:1;
        Entity direct=source.getSource();
        var point=direct==null?attacker.getPos():direct.getPos();
        // Melee originates inside the attacking body, above the supporting floor.
        if(kind==1)point=attacker.getBoundingBox().getCenter();
        var gs=HostCollision.goldsrc(point.x,point.y,point.z);var actor=target.actor();long event=++sequence;
        byte[] data=new Wire.Writer().i64(epoch).i64(event).i64(key).i32(actor.slot()).i32(actor.serial()).i32(actor.spawn()).i32(kind)
            .f32(amount*MinecraftObjects.CS_HEALTH_PER_MC).f32(gs.x()).f32(gs.y()).f32(gs.z())
            .i32(playerSource==null?0:playerSource.slot()).i32(playerSource==null?0:playerSource.serial()).i32(playerSource==null?0:playerSource.spawn()).toByteArray();
        if(!GoldCraft.sendToHost(Wire.DAMAGE_REQUEST,data))return false;
        pending.put(event,target);requests++;return true;
    }
    public static void result(byte[] bytes){
        Wire.Reader r=new Wire.Reader(bytes);long e=r.i64(),event=r.i64();int slot=r.i32(),serial=r.i32(),life=r.i32(),status=r.i32();r.f32();r.f32();r.finish();
        if(e!=epoch)return;
        var target=pending.remove(event);
        if(target==null||target.actor().slot()!=slot||target.actor().serial()!=serial||target.actor().spawn()!=life)return;
        if(status==0)accepted++;else rejected++;
    }
    public static void diagnostics(JsonObject data){
        JsonObject n=new JsonObject();n.addProperty("players",players.size());n.addProperty("damageRequests",requests);
        n.addProperty("damageAccepted",accepted);n.addProperty("damageRejected",rejected);n.addProperty("pendingDamage",pending.size());
        var bodies=new com.google.gson.JsonArray();
        for(var entry:hitboxes.entrySet()){
            var b=new JsonObject();b.addProperty("slot",entry.getKey());b.addProperty("hitbox",entry.getValue().getId());bodies.add(b);
        }
        n.add("hitboxes",bodies);data.add("nativePlayers",n);
    }
}
