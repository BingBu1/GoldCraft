package dev.goldcraft.world;

import com.mojang.authlib.GameProfile;
import dev.goldcraft.bridge.HostWorldState;
import net.minecraft.entity.EntityDimensions;
import net.minecraft.entity.EntityPose;
import net.minecraft.entity.attribute.EntityAttributes;
import net.minecraft.entity.damage.DamageSource;
import net.minecraft.entity.player.PlayerEntity;
import net.minecraft.registry.tag.DamageTypeTags;
import net.minecraft.server.world.ServerWorld;
import net.minecraft.util.math.BlockPos;
import java.nio.charset.StandardCharsets;
import java.util.UUID;

/** Server-only body of a real CS connection. Never logs in or creates a network player. */
public final class NativePlayerEntity extends PlayerEntity {
    @FunctionalInterface public interface DamageSink {boolean apply(NativePlayerEntity target,DamageSource source,float amount);}
    private final DamageSink damageSink;
    private final long epoch;
    private HostWorldState.Actor actor;
    private float lastNativeDamage;

    public NativePlayerEntity(ServerWorld world,long epoch,HostWorldState.Actor actor,DamageSink damageSink){
        super(world,BlockPos.ORIGIN,0,new GameProfile(identity(epoch,actor),"CS_"+actor.slot()));
        this.epoch=epoch;this.damageSink=damageSink;update(actor);
    }
    private static UUID identity(long epoch,HostWorldState.Actor actor){
        return UUID.nameUUIDFromBytes(("goldcraft/native/"+Long.toUnsignedString(epoch)+"/"+actor.slot()+"/"+actor.serial()+"/"+actor.life()).getBytes(StandardCharsets.UTF_8));
    }
    public long epoch(){return epoch;}
    public HostWorldState.Actor actor(){return actor;}
    public void update(HostWorldState.Actor next){
        boolean dimensions=actor==null||!actor.mins().equals(next.mins())||!actor.maxs().equals(next.maxs());
        actor=next;
        if(dimensions)calculateDimensions();
        var feet=next.minecraftFeet();refreshPositionAndAngles(feet.x(),feet.y(),feet.z(),-next.yaw()-90,next.pitch());
        setVelocity(0,0,0);setOnGround(true);
        float health=Math.max(0,next.health()/MinecraftObjects.CS_HEALTH_PER_MC);
        getAttributeInstance(EntityAttributes.GENERIC_MAX_HEALTH).setBaseValue(Math.max(20,health));setHealth(health);
    }
    @Override public EntityDimensions getBaseDimensions(EntityPose pose){
        if(actor==null)return super.getBaseDimensions(pose);
        float width=(actor.maxs().x()-actor.mins().x())/32,height=(actor.maxs().z()-actor.mins().z())/32;
        return EntityDimensions.fixed(width,height).withEyeHeight(Math.max(0.1f,height-0.25f));
    }
    @Override public boolean isSpectator(){return false;}
    @Override public boolean isCreative(){return false;}
    @Override public boolean shouldSave(){return false;}
    @Override public boolean isPushable(){return false;}
    @Override public void tick(){if(timeUntilRegen>0)timeUntilRegen--;setVelocity(0,0,0);}
    @Override public boolean damage(DamageSource source,float amount){
        if(isRemoved()||!isAlive()||isInvulnerableTo(source)||!Float.isFinite(amount)||amount<=0)return false;
        if(source.isScaledWithDifficulty())amount=switch(getWorld().getDifficulty()){
            case PEACEFUL -> 0;case EASY -> Math.min(amount/2+1,amount);case HARD -> amount*1.5f;default -> amount;
        };
        if(amount<=0)return false;
        float applied=amount;
        boolean cooldown=timeUntilRegen>10&&!source.isIn(DamageTypeTags.BYPASSES_COOLDOWN);
        if(cooldown){if(amount<=lastNativeDamage)return false;applied-=lastNativeDamage;}
        if(!damageSink.apply(this,source,applied))return false;
        lastNativeDamage=amount;if(!cooldown)timeUntilRegen=20;
        // ReHLDS owns health/armor/death. The next authority snapshot confirms them.
        return true;
    }
}
