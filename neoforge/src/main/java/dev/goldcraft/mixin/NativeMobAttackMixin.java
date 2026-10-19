package dev.goldcraft.mixin;

import com.llamalad7.mixinextras.injector.wrapmethod.WrapMethod;
import com.llamalad7.mixinextras.injector.wrapoperation.Operation;
import dev.goldcraft.world.NativeKnockback;
import net.minecraft.entity.Entity;
import net.minecraft.entity.mob.MobEntity;
import org.spongepowered.asm.mixin.Mixin;

@Mixin(MobEntity.class)
abstract class NativeMobAttackMixin {
    // Loom 1.7's remapper does not visit WrapMethod selectors. Keep the pinned
    // Yarn and Mojang descriptors explicit; exactly one must match the target.
    @WrapMethod(method={"tryAttack(Lnet/minecraft/entity/Entity;)Z",
        "doHurtTarget(Lnet/minecraft/world/entity/Entity;)Z"},remap=false,require=1,allow=1)
    private boolean goldcraft$attack(Entity target,Operation<Boolean> original){
        try(var scope=NativeKnockback.attack()){return original.call(target);}
    }
}
