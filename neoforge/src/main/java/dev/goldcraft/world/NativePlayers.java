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
    private static final long[] damageStatus=new long[7];
    private record Pending(NativePlayerEntity target,int kind,long generation){}
    private static final Map<Long,Pending> pending=new LinkedHashMap<>();
    public static NativePlayerHitboxEntity hitbox(HostWorldState.Actor actor){
        var box=hitboxes.get(actor.slot());
        return box!=null&&!box.isRemoved()&&box.body()!=null&&box.body().actor().serial()==actor.serial()
            &&box.body().actor().spawn()==actor.spawn()?box:null;
    }
    private NativePlayers(){}

    public static void clear(){
        NativeStatusEffects.clear();
        for(var p:players.values())p.discard();
        for(var p:hitboxes.values())p.discard();
        if(world!=null)for(var t:tickets.entrySet())world.getChunkManager().removeTicket(TICKET,t.getValue(),3,t.getKey());
        players.clear();hitboxes.clear();tickets.clear();pending.clear();Arrays.fill(damageStatus,0);world=null;epoch=sequence=requests=accepted=rejected=0;
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
            boolean formChange=proxy.actor().serial()==actor.serial()&&proxy.actor().spawn()==actor.spawn();
            if(!formChange)transferTargets(proxy,null);
            proxy.discard();players.remove(actor.slot());
            var replacement=new NativePlayerEntity(world,epoch,actor,NativePlayers::damage);
            if(formChange)replacement.inheritHealth(proxy);
            players.put(actor.slot(),replacement);
            if(formChange)transferTargets(proxy,replacement);
            proxy=replacement;
        }
        if(proxy==null){
            // The logical PlayerEntity is supplied to vanilla AI by
            // targetingPlayers. Only its hitbox enters spatial entity queries;
            // registering both makes one native player consume two skill hits.
            proxy=new NativePlayerEntity(world,epoch,actor,NativePlayers::damage);players.put(actor.slot(),proxy);
            if(paired!=null)transferTargets(paired,proxy);
        }else proxy.update(actor);
        ensureHitbox(proxy);return proxy;
    }
    private static NativePlayerHitboxEntity ensureHitbox(NativePlayerEntity body){
        var actor=body.actor();
            var hitbox=hitboxes.get(actor.slot());
            if(hitbox==null||hitbox.isRemoved()||!hitbox.follows(body)){
                // Keep a removed entity reference dead across a native respawn,
                // reconnect or form lifecycle instead of silently retargeting it.
                var oldBox=hitbox;
                boolean sameSpawn=oldBox!=null&&oldBox.body()!=null&&oldBox.body().actor().serial()==actor.serial()
                    &&oldBox.body().actor().spawn()==actor.spawn();
                hitbox=new NativePlayerHitboxEntity(NativePlayerHitboxEntity.TYPE.get(),world);
                hitboxes.put(actor.slot(),hitbox);hitbox.follow(body);world.spawnEntity(hitbox);
                if(sameSpawn)hitbox.inheritTimers(oldBox);
                if(oldBox!=null){NativeStatusEffects.transfer(oldBox,sameSpawn?hitbox:null);oldBox.discard();}
            }else hitbox.follow(body);
        NativeStatusEffects.transfer(body,hitbox);
        return hitbox;
    }
    public static void tick(MinecraftServer server,HostWorldState host){
        var targetWorld=HostSession.hostWorld(server,host);if(targetWorld==null){clear();return;}
        if(world!=targetWorld||epoch!=host.epoch()){clear();world=targetWorld;epoch=host.epoch();}
        Set<Integer> retained=new HashSet<>();
        for(var actor:host.actors()){
            if(!alive(actor)){
                NativeStatusDeath.retire(hitboxes.get(actor.slot()),host,actor);
                continue;
            }
            PlayerEntity body=attacker(server,host,actor);
            if(!(body instanceof NativePlayerEntity)){
                var oldBox=hitboxes.remove(actor.slot());
                if(oldBox!=null){
                    boolean sameSpawn=oldBox.body().actor().serial()==actor.serial()&&oldBox.body().actor().spawn()==actor.spawn();
                    NativeStatusEffects.transfer(oldBox,sameSpawn?body:null);oldBox.discard();
                }
                var previous=players.remove(actor.slot());if(previous!=null){transferTargets(previous,body);previous.discard();}
                NativeStatusEffects.hud(actor,body,body instanceof net.minecraft.server.network.ServerPlayerEntity p?p:null,world.getTime());
                continue;
            }
            body.tick();
            retained.add(actor.slot());ChunkPos chunk=body.getChunkPos(),old=tickets.put(actor.slot(),chunk);
            var hitbox=ensureHitbox((NativePlayerEntity)body);
            var paired=server.getPlayerManager().getPlayer(actor.minecraftPlayer());
            if(paired!=null)NativeStatusEffects.transfer(paired,hitbox);
            NativeStatusEffects.hud(actor,hitbox,paired,world.getTime());
            if(old!=null&&!old.equals(chunk))world.getChunkManager().removeTicket(TICKET,old,3,actor.slot());
            world.getChunkManager().addTicket(TICKET,chunk,3,actor.slot());
        }
        for(var it=players.entrySet().iterator();it.hasNext();){var p=it.next();if(!retained.contains(p.getKey())){transferTargets(p.getValue(),null);p.getValue().discard();it.remove();}}
        for(var it=hitboxes.entrySet().iterator();it.hasNext();){var p=it.next();if(!retained.contains(p.getKey())){NativeStatusEffects.clearMirror(p.getKey());p.getValue().discard();it.remove();}}
        for(var it=tickets.entrySet().iterator();it.hasNext();){var t=it.next();if(!retained.contains(t.getKey())){world.getChunkManager().removeTicket(TICKET,t.getValue(),3,t.getKey());it.remove();}}
        if(!players.isEmpty())world.resetIdleTimeout();
        pending.entrySet().removeIf(e->{var old=e.getValue().target().actor();
            return host.actors().stream().noneMatch(a->a.slot()==old.slot()&&a.serial()==old.serial()&&a.spawn()==old.spawn()&&alive(a));});
    }
    public static List<? extends PlayerEntity> targetingPlayers(ServerWorld view,List<? extends PlayerEntity> vanilla){
        if(view!=world||players.isEmpty())return vanilla;
        List<PlayerEntity> result=new ArrayList<>(vanilla);
        for(var p:players.values())if(!p.isRemoved()&&p.isAlive())result.add(p);
        return result;
    }
    public static NativePlayerEntity byUuid(ServerWorld view,UUID uuid){
        if(view!=world)return null;
        for(var body:players.values())if(!body.isRemoved()&&body.getUuid().equals(uuid))return body;
        return null;
    }
    private static boolean damage(NativePlayerEntity target,DamageSource source,float amount){
        var status=target.hitbox()==null?null:NativeStatusExecution.current(target.hitbox());
        if(status!=null)return statusDamage(target,source,amount,status);
        if(amount<=0)return false;
        var explosion=NativeTntDamage.center(target,source);
        if(explosion!=null)return tntDamage(target,source,amount,explosion);
        Entity attacker=source.getAttacker();
        long key=MinecraftObjects.key(attacker);
        var playerSource=attacker instanceof net.minecraft.server.network.ServerPlayerEntity p?GoldCraft.HOST_WORLD.actor(p.getUuid()):null;
        if(target.epoch()!=epoch||target.isRemoved()||(key==0&&playerSource==null)||amount>1000||pending.size()>=256)return false;
        int kind=source.isIn(DamageTypeTags.IS_EXPLOSION)?3:source.isIn(DamageTypeTags.IS_PROJECTILE)?2:NativeSkillDamage.isSkill(source)?4:1;
        Entity direct=source.getSource();
        var point=direct==null?attacker.getPos():direct.getPos();
        // Melee originates inside the attacking body, above the supporting floor.
        if(kind==1||kind==4)point=attacker.getBoundingBox().getCenter();
        var gs=HostCollision.goldsrc(point.x,point.y,point.z);var actor=target.actor();long event=++sequence;
        var request=new Wire.Writer().i64(epoch).i64(event).i64(key).i32(actor.slot()).i32(actor.serial()).i32(actor.spawn()).i32(kind)
            .f32(amount*MinecraftObjects.CS_HEALTH_PER_MC).f32(gs.x()).f32(gs.y()).f32(gs.z())
            .i32(playerSource==null?0:playerSource.slot()).i32(playerSource==null?0:playerSource.serial()).i32(playerSource==null?0:playerSource.spawn());
        if(kind==4){
            request.i64(GoldCraft.HOST_WORLD.tick())
                .f32(actor.origin().x()+(actor.mins().x()+actor.maxs().x())*.5f)
                .f32(actor.origin().y()+(actor.mins().y()+actor.maxs().y())*.5f)
                .f32(actor.origin().z()+(actor.mins().z()+actor.maxs().z())*.5f);
        }
        boolean active=playerSource!=null&&ActivePlayerDamage.eligible(source,kind)&&GoldCraft.playerGameplayAvailable();
        if(!(active?GoldCraft.sendPlayerGameplay(Wire.PLAYER_ATTACK,request.toByteArray()):GoldCraft.sendToHost(Wire.DAMAGE_REQUEST,request.toByteArray())))return false;
        pending.put(event,new Pending(target,kind,GoldCraft.authorityGeneration()));
        target.healthLedger().awaitReceipt(event);requests++;return true;
    }
    private static boolean tntDamage(NativePlayerEntity target,DamageSource source,float amount,net.minecraft.util.math.Vec3d center){
        if(!GoldCraft.tntDamageAvailable()||target.epoch()!=epoch||target.isRemoved()||amount>1000||pending.size()>=256)return false;
        var host=GoldCraft.HOST_WORLD;var attacker=source.getAttacker();var owner=NativeTntDamage.owner(host,source);
        long key=owner!=null?0:MinecraftObjects.key(attacker);
        var actor=target.actor();var point=HostCollision.goldsrc(center.x,center.y,center.z);long event=++sequence;
        var packet=new Wire.Writer().i64(epoch).i64(event).i64(key)
            .i32(actor.slot()).i32(actor.serial()).i32(actor.spawn()).i32(6)
            .f32(amount*MinecraftObjects.CS_HEALTH_PER_MC).f32(point.x()).f32(point.y()).f32(point.z())
            .i32(owner==null?0:owner.slot()).i32(owner==null?0:owner.serial()).i32(owner==null?0:owner.spawn())
            .i64(host.tick()).f32(actor.origin().x()+(actor.mins().x()+actor.maxs().x())*.5f)
            .f32(actor.origin().y()+(actor.mins().y()+actor.maxs().y())*.5f)
            .f32(actor.origin().z()+(actor.mins().z()+actor.maxs().z())*.5f).toByteArray();
        if(!GoldCraft.sendTntDamage(packet))return false;
        pending.put(event,new Pending(target,6,GoldCraft.authorityGeneration()));
        target.healthLedger().awaitReceipt(event);requests++;return true;
    }
    private static boolean statusDamage(NativePlayerEntity target,DamageSource source,float amount,NativeStatusExecution status){
        if(!GoldCraft.statusEffectsAvailable()||target.epoch()!=epoch||target.isRemoved()||pending.size()>=256)return false;
        var attacker=status.origin==null?null:status.origin.resolve(world);
        // A Mod may supply a stronger concrete attacker within its callback.
        if(status.origin==null&&source.getAttacker()!=null&&!source.getAttacker().isRemoved()&&source.getAttacker().isAlive()
            &&source.getAttacker()!=target.hitbox()&&source.getAttacker()!=target)
            attacker=source.getAttacker();
        var origin=NativeStatusOrigin.capture(attacker);
        long key=MinecraftObjects.key(attacker);
        int attackerSlot=origin==null?0:origin.slot();
        if(attackerSlot!=0)key=0;
        if(attackerSlot==0&&key==0)attacker=null;
        boolean healing=amount<0;
        amount=Math.clamp(amount,-1000,1000);
        int flags=(status.periodic?1:0)|(!healing&&source.isIn(DamageTypeTags.BYPASSES_ARMOR)?2:0)
            |(!healing&&status.poison?4:0)|(attacker==null?8:0)|(status.traceContact?16:0);
        var actor=target.actor();var point=HostCollision.goldsrc(status.contact.x,status.contact.y,status.contact.z);
        long event=++sequence;
        var packet=new Wire.Writer().i64(epoch).i64(event).i64(attacker==null?0:key)
            .i32(actor.slot()).i32(actor.serial()).i32(actor.spawn()).i32(5)
            .f32(amount*MinecraftObjects.CS_HEALTH_PER_MC).f32(point.x()).f32(point.y()).f32(point.z())
            .i32(attackerSlot).i32(attackerSlot==0?0:origin.serial()).i32(attackerSlot==0?0:origin.spawn())
            .i64(GoldCraft.HOST_WORLD.tick()).f32(actor.origin().x()+(actor.mins().x()+actor.maxs().x())*.5f)
            .f32(actor.origin().y()+(actor.mins().y()+actor.maxs().y())*.5f)
            .f32(actor.origin().z()+(actor.mins().z()+actor.maxs().z())*.5f).i32(flags).toByteArray();
        if(!GoldCraft.sendStatusToHost(Wire.DAMAGE_REQUEST,packet))return false;
        pending.put(event,new Pending(target,5,GoldCraft.authorityGeneration()));target.healthLedger().sent(event,-amount);requests++;return true;
    }
    public static void result(byte[] bytes){
        Wire.Reader r=new Wire.Reader(bytes);long e=r.i64(),event=r.i64();int slot=r.i32(),serial=r.i32(),spawn=r.i32(),status=r.i32();
        float health=r.f32(),armor=r.f32();var request=pending.get(event);
        if(e!=epoch||request==null)return;
        long tick=request.kind()==5?r.i64():GoldCraft.HOST_WORLD.tick();r.finish();
        if(request.generation()!=GoldCraft.authorityGeneration()||(request.kind()==5&&!GoldCraft.statusEffectsAvailable())
            ||(request.kind()==6&&!GoldCraft.tntDamageAvailable()))return;
        var target=request.target();
        if(target.actor().slot()!=slot||target.actor().serial()!=serial||target.actor().spawn()!=spawn||armor<0||status<0||status>6)return;
        pending.remove(event);
        target.healthLedger().receipt(event,Math.max(0,health)/MinecraftObjects.CS_HEALTH_PER_MC,tick);
        if(status==0)accepted++;else rejected++;
        damageStatus[status]++;
    }
    public static void diagnostics(JsonObject data){
        JsonObject n=new JsonObject();n.addProperty("players",players.size());n.addProperty("damageRequests",requests);
        n.addProperty("damageAccepted",accepted);n.addProperty("damageRejected",rejected);n.addProperty("pendingDamage",pending.size());
        var reasons=new com.google.gson.JsonArray();for(long count:damageStatus)reasons.add(count);n.add("damageStatus",reasons);
        var bodies=new com.google.gson.JsonArray();
        for(var entry:hitboxes.entrySet()){
            var b=new JsonObject();b.addProperty("slot",entry.getKey());b.addProperty("hitbox",entry.getValue().getId());bodies.add(b);
        }
        n.add("hitboxes",bodies);data.add("nativePlayers",n);
    }
}
