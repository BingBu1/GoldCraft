package dev.goldcraft.mixin;

import net.minecraft.entity.player.HungerManager;
import net.minecraft.entity.player.PlayerEntity;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.gen.Accessor;

@Mixin(PlayerEntity.class)
public interface NativeHungerAccessor {
    @Accessor("hungerManager") void goldcraft$hunger(HungerManager value);
}
