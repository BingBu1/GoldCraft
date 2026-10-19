package dev.goldcraft.world;

import dev.goldcraft.GoldCraft;
import dev.goldcraft.bridge.HostWorldState;
import net.minecraft.util.math.Vec3d;
import java.util.ArrayList;
import java.util.IdentityHashMap;

/** Collect original knockback calls in attack order, then await the matching hurt receipt. */
public final class NativeKnockback {
    private static final ThreadLocal<Attack> ATTACK=new ThreadLocal<>();
    private NativeKnockback(){}
    public static Attack attack(){return new Attack();}
    public static final class Attack implements AutoCloseable {
        private final Attack previous=ATTACK.get();
        private final IdentityHashMap<NativePlayerHitboxEntity,Hit> latest=new IdentityHashMap<>();
        private final ArrayList<Hit> hits=new ArrayList<>();
        private Attack(){ATTACK.set(this);}
        private Hit create(NativePlayerHitboxEntity box){var hit=new Hit(box,this);latest.put(box,hit);hits.add(hit);return hit;}
        @Override public void close(){
            if(previous==null)ATTACK.remove();else ATTACK.set(previous);
            for(var hit:hits){hit.sealed=true;hit.flush();}
        }
    }
    static Hit hurt(NativePlayerHitboxEntity box){
        var attack=ATTACK.get();var pending=attack==null?null:attack.latest.get(box);
        // Sweeps push before hurt; a second/nested hurt must get its own receipt.
        var hit=pending!=null&&!pending.attached?pending:attack==null?new Hit(box,null):attack.create(box);
        hit.attached=true;return hit;
    }
    static void apply(NativePlayerHitboxEntity box,Runnable vanilla){
        var hit=NativeHitEffects.knockback(box);
        if(hit==null){
            var attack=ATTACK.get();if(attack==null)return;
            hit=attack.latest.get(box);if(hit==null)hit=attack.create(box);
        }
        hit.apply(vanilla);
    }
    static final class Hit {
        private final NativePlayerHitboxEntity box;
        private final Attack attack;
        private final long epoch,generation,tick;
        private final int serial,spawn,life;
        private final HostWorldState.Actor snapshot;
        private final boolean grounded;
        private final Vec3d initial;
        private Vec3d velocity;
        private boolean attached,ready,sealed,sent;
        private long confirmedEvent;
        private Hit(NativePlayerHitboxEntity box,Attack attack){
            this.box=box;this.attack=attack;
            var body=box.body();var host=GoldCraft.HOST_WORLD;var actor=body.actor();
            snapshot=actor;
            epoch=body.epoch();generation=GoldCraft.authorityGeneration();tick=host.tick();
            serial=actor.serial();spawn=actor.spawn();life=actor.life();
            var movement=host.nativeMovement().get(host,actor,box.getServer().getTicks());
            boolean valid=GoldCraft.nativeKnockbackAvailable()&&movement!=null&&movement.walking()&&!actor.minecraftForm();
            grounded=valid&&movement.grounded();
            initial=valid?new Vec3d(movement.velocityX(),movement.velocityY(),movement.velocityZ()):null;
            velocity=initial;
        }
        private void apply(Runnable vanilla){
            if(initial==null||sent)return;
            var saved=box.getVelocity();boolean ground=box.isOnGround(),dirty=box.velocityDirty;
            box.setVelocity(velocity);box.setOnGround(grounded);
            try{vanilla.run();velocity=box.getVelocity();}
            finally{box.setVelocity(saved);box.setOnGround(ground);box.velocityDirty=dirty;}
        }
        void local(boolean accepted){
            ready=accepted;if(attack==null)sealed=true;else attack.latest.put(box,this);flush();
        }
        void confirmed(long event){confirmedEvent=event;flush();}
        private void flush(){
            if(sent||!sealed||!attached||!ready||confirmedEvent==0||initial==null)return;
            sent=true;var body=box.body();var actor=body.actor();
            if(box.isRemoved()||body.isRemoved()||!body.isAlive()||epoch!=GoldCraft.HOST_WORLD.epoch()
                ||generation!=GoldCraft.authorityGeneration()||serial!=actor.serial()||spawn!=actor.spawn()||life!=actor.life())return;
            NativeImpulse.confirmed(box,velocity.subtract(initial),confirmedEvent,tick,snapshot);
        }
    }
}
