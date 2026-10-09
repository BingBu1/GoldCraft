package dev.goldcraft.world;

import dev.goldcraft.GoldCraft;
import dev.goldcraft.bridge.HostWorldState;
import dev.goldcraft.bridge.MapMining;
import dev.goldcraft.bridge.Wire;
import dev.goldcraft.net.MiningPayload;
import net.minecraft.block.Blocks;
import net.minecraft.entity.attribute.EntityAttributes;
import net.minecraft.entity.projectile.ProjectileUtil;
import net.minecraft.item.ItemStack;
import net.minecraft.server.MinecraftServer;
import net.minecraft.server.network.ServerPlayerEntity;
import net.minecraft.util.math.Box;
import net.minecraft.world.GameMode;
import net.minecraft.world.RaycastContext;
import java.util.HashMap;
import java.util.Map;
import java.util.UUID;

/** Minecraft validates held intent and tool state; HLDS retraces and commits damage. */
public final class HostMining {
    private static final Map<UUID,Intent> INTENTS=new HashMap<>();
    private static final Map<Long,Pending> PENDING=new HashMap<>();
    private static long event,tick,policyRevision;
    private static final class Intent {
        long epoch,revision,lastIntent,nextTick,pending;
        int serial,life;
        boolean held;
    }
    private record Pending(UUID player,HostWorldState.Actor actor,HostRaycast.Hit hit,ItemStack tool,long expires) {}
    private HostMining(){}
    public static void clear(){INTENTS.clear();PENDING.clear();event=tick=policyRevision=0;}
    public static void disconnect(UUID id){INTENTS.remove(id);PENDING.values().removeIf(p->p.player().equals(id));}
    public static void intent(ServerPlayerEntity player,MiningPayload payload){
        var host=GoldCraft.HOST_WORLD;var actor=host.actor(player.getUuid());var policy=host.mining();
        if(actor==null||payload.epoch()!=host.epoch()||payload.serial()!=actor.serial()||payload.life()!=actor.life()
            ||payload.revision()!=policy.revision()||!actor.minecraftForm()||(actor.flags()&32)==0)return;
        var state=INTENTS.computeIfAbsent(player.getUuid(),ignored->new Intent());
        state.epoch=payload.epoch();state.revision=payload.revision();state.serial=payload.serial();state.life=payload.life();
        state.held=payload.held();state.lastIntent=System.nanoTime();
    }
    public static void tick(MinecraftServer server,HostWorldState host){
        ++tick;var policy=host.mining();
        if(policyRevision!=policy.revision()){
            policyRevision=policy.revision();INTENTS.values().forEach(s->s.held=false);
        }
        PENDING.entrySet().removeIf(entry->{
            if(tick<entry.getValue().expires())return false;
            var state=INTENTS.get(entry.getValue().player());if(state!=null&&state.pending==entry.getKey())state.pending=0;
            return true;
        });
        if(!policy.enabled()||host.freeze())return;
        for(var entry:INTENTS.entrySet()){
            var state=entry.getValue();var player=server.getPlayerManager().getPlayer(entry.getKey());var actor=host.actor(entry.getKey());
            if(!state.held||state.pending!=0||tick<state.nextTick||System.nanoTime()-state.lastIntent>350_000_000L
                ||player==null||!player.isAlive()||player.isUsingItem()||actor==null||!actor.minecraftForm()||(actor.flags()&33)!=33
                ||state.epoch!=host.epoch()||state.revision!=policy.revision()||state.serial!=actor.serial()||state.life!=actor.life()
                ||!player.getWorld().getRegistryKey().getValue().toString().equals(host.dimension()))continue;
            var mode=player.interactionManager.getGameMode();
            if(mode!=GameMode.CREATIVE&&mode!=GameMode.SURVIVAL)continue;
            double reach=Math.min(6,player.getBlockInteractionRange());
            if(!Double.isFinite(reach)||reach<=0)continue;
            var eye=player.getEyePos();var end=eye.add(player.getRotationVec(1).multiply(reach));
            var hit=player.getWorld().raycast(new RaycastContext(eye,end,RaycastContext.ShapeType.OUTLINE,RaycastContext.FluidHandling.NONE,player));
            if(!(hit instanceof HostRaycast.Hit target)||!policy.canRequest(target.model))continue;
            double distance=eye.squaredDistanceTo(hit.getPos());
            if(ProjectileUtil.raycast(player,eye,hit.getPos(),new Box(eye,hit.getPos()).expand(1),e->!e.isSpectator()&&e.canHit(),distance)!=null)continue;
            double base=player.getAttributeValue(EntityAttributes.GENERIC_ATTACK_DAMAGE)*5;
            float cooldown=player.getAttackCooldownProgressPerTick();
            if(!Double.isFinite(base)||base<=0||!Float.isFinite(cooldown)||cooldown<=0)continue;
            float amount=mode==GameMode.CREATIVE?5000:(float)Math.min(5000,base);
            var point=HostCollision.goldsrc(hit.getPos().x,hit.getPos().y,hit.getPos().z);
            if(event==Long.MAX_VALUE)throw new IllegalStateException("Mining event sequence exhausted");
            long id=++event;
            byte[] request=MapMining.request(policy,id,actor,target.slot,target.serial,target.model,point.x(),point.y(),point.z(),amount,(float)(reach*Wire.UNITS_PER_BLOCK));
            if(GoldCraft.sendToHost(Wire.MAP_MINING_REQUEST,request)){
                state.pending=id;state.nextTick=tick+Math.max(4,(int)Math.ceil(cooldown));
                PENDING.put(id,new Pending(entry.getKey(),actor,target,player.getMainHandStack(),tick+40));
                player.resetLastAttackedTicks();
            }
        }
    }
    public static void result(MinecraftServer server,HostWorldState host,byte[] bytes){
        var result=MapMining.result(bytes);var pending=PENDING.get(result.event());
        if(pending==null||result.epoch()!=host.epoch()||result.slot()!=pending.actor().slot()||result.serial()!=pending.actor().serial()
            ||result.life()!=pending.actor().life()||result.target()!=pending.hit().slot||result.targetSerial()!=pending.hit().serial)return;
        PENDING.remove(result.event());
        var state=INTENTS.get(pending.player());if(state!=null&&state.pending==result.event())state.pending=0;
        var player=server.getPlayerManager().getPlayer(pending.player());var actor=host.actor(pending.player());
        if(result.status()!=MapMining.APPLIED||result.after()>0||player==null||!player.isAlive()||actor==null
            ||actor.serial()!=result.serial()||actor.life()!=result.life()||player.getMainHandStack()!=pending.tool()
            ||player.interactionManager.getGameMode()!=GameMode.SURVIVAL)return;
        // Native map entities have no MC block state. Stone is the documented
        // durability fallback until the host material export supplies one.
        pending.tool().postMine(player.getWorld(),Blocks.STONE.getDefaultState(),pending.hit().getBlockPos(),player);
    }
}
