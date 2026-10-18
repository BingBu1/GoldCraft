package dev.goldcraft.world;

import dev.goldcraft.GoldCraft;
import dev.goldcraft.bridge.HostWorldState;
import dev.goldcraft.bridge.Wire;
import net.minecraft.entity.LivingEntity;
import net.minecraft.entity.attribute.EntityAttributes;

/** Apply the actual attack modifiers to the native weapon's own damage base. */
public final class NativeMelee {
    private static long revision;
    private NativeMelee(){}
    public static void clear(){revision=0;}
    public static void update(HostWorldState.Actor actor,LivingEntity owner){
        if(!GoldCraft.meleeAvailable()||!owner.isAlive()||GoldCraft.HOST_WORLD.epoch()==0
            ||actor.slot()<1||actor.slot()>64||actor.serial()==0||actor.spawn()==0||actor.life()==0)return;
        double add=0,base=0,total=1;
        var attribute=owner.getAttributeInstance(EntityAttributes.GENERIC_ATTACK_DAMAGE);
        if(attribute!=null&&!actor.minecraftForm())for(var modifier:attribute.getModifiers()){
            switch(modifier.operation()){
                case ADD_VALUE -> add+=modifier.value();
                case ADD_MULTIPLIED_BASE -> base+=modifier.value();
                case ADD_MULTIPLIED_TOTAL -> total*=1+modifier.value();
            }
        }
        // Never publish NaN or an unbounded Mod attribute. The old lease expires.
        if(!Double.isFinite(add)||!Double.isFinite(base)||!Double.isFinite(total)
            ||Math.abs(add)>1000000||Math.abs(base)>1000000||Math.abs(total)>1000000)return;
        GoldCraft.sendMelee(new Wire.Writer().i64(GoldCraft.HOST_WORLD.epoch()).i64(++revision)
            .i32(actor.slot()).i32(actor.serial()).i32(actor.spawn()).i32(actor.life())
            .f32((float)add).f32((float)base).f32((float)total).toByteArray());
    }
}
