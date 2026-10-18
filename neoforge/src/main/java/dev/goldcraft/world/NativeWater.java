package dev.goldcraft.world;

import dev.goldcraft.GoldCraft;
import dev.goldcraft.bridge.HostWorldState;
import dev.goldcraft.bridge.Wire;
import dev.goldcraft.mixin.NativeWaterAccessor;
import net.minecraft.entity.LivingEntity;
import net.minecraft.entity.attribute.EntityAttributes;
import net.minecraft.entity.effect.StatusEffects;
import net.minecraft.util.math.BlockPos;
import net.neoforged.neoforge.common.NeoForgeMod;

/** Parameters from the actual water travel owner; native movement retains its hull and input. */
public final class NativeWater {
    private static long revision;
    private NativeWater(){}
    public static void clear(){revision=0;}
    public static void update(HostWorldState.Actor actor,LivingEntity owner){
        if(!GoldCraft.waterAvailable()||!owner.isAlive()||GoldCraft.HOST_WORLD.epoch()==0
            ||actor.slot()<1||actor.slot()>64||actor.serial()==0||actor.spawn()==0||actor.life()==0)return;
        boolean proxy=owner instanceof NativePlayerHitboxEntity;
        // The spatial proxy is deliberately no-gravity; that is implementation state,
        // not a player ability. Read its real gravity attribute instead.
        float gravity=(float)(proxy?owner.getAttributeValue(EntityAttributes.GENERIC_GRAVITY):owner.getFinalGravity());
        float drag=((NativeWaterAccessor)owner).goldcraft$waterDrag();
        float speed=(float)(proxy?owner.getAttributeValue(EntityAttributes.GENERIC_MOVEMENT_SPEED):owner.getMovementSpeed());
        float efficiency=(float)owner.getAttributeValue(EntityAttributes.GENERIC_WATER_MOVEMENT_EFFICIENCY);
        float swim=(float)owner.getAttributeValue(NeoForgeMod.SWIM_SPEED);
        int flags=0;
        if(owner.hasStatusEffect(StatusEffects.DOLPHINS_GRACE))flags|=2;
        if(owner.hasStatusEffect(StatusEffects.SLOW_FALLING))flags|=4;
        if(owner.isSprinting())flags|=8;
        if(owner.isSwimming()&&!owner.hasVehicle()){
            flags|=16;
            if(!owner.getWorld().getBlockState(BlockPos.ofFloored(owner.getX(),owner.getY()+.9,owner.getZ())).getFluidState().isEmpty())flags|=32;
        }
        if(flags!=0||efficiency!=0||swim!=1||gravity!=.08f||drag!=.8f)flags|=1;
        if(!Float.isFinite(drag)||drag<=0||drag>=1||!finiteRange(speed,1024)||!finiteRange(efficiency,1)
            ||!finiteRange(swim,1024)||!finiteRange(gravity,1))return;
        GoldCraft.sendWater(new Wire.Writer().i64(GoldCraft.HOST_WORLD.epoch()).i64(++revision)
            .i32(actor.slot()).i32(actor.serial()).i32(actor.spawn()).i32(actor.life()).i32(flags)
            .f32(drag).f32(speed).f32(efficiency).f32(swim).f32(gravity).toByteArray());
    }
    private static boolean finiteRange(float value,float max){return Float.isFinite(value)&&value>=0&&value<=max;}
}
