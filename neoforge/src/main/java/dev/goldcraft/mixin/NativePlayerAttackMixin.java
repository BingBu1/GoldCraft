package dev.goldcraft.mixin;

import com.llamalad7.mixinextras.injector.wrapmethod.WrapMethod;
import com.llamalad7.mixinextras.injector.wrapoperation.Operation;
import dev.goldcraft.world.NativeKnockback;
import net.minecraft.entity.Entity;
import net.minecraft.entity.player.PlayerEntity;
import org.spongepowered.asm.mixin.Mixin;

@Mixin(PlayerEntity.class)
abstract class NativePlayerAttackMixin {
    // WrapMethod selectors are not remapped by the pinned Loom 1.7 toolchain.
    @WrapMethod(method={"attack(Lnet/minecraft/entity/Entity;)V",
        "attack(Lnet/minecraft/world/entity/Entity;)V"},remap=false,require=1,allow=1)
    private void goldcraft$attack(Entity target,Operation<Void> original){
        try(var scope=NativeKnockback.attack()){original.call(target);}
    }
}
