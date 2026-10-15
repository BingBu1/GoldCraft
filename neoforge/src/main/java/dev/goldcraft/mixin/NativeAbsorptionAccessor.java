package dev.goldcraft.mixin;

import net.minecraft.entity.data.TrackedData;
import net.minecraft.entity.player.PlayerEntity;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.gen.Accessor;

@Mixin(PlayerEntity.class)
public interface NativeAbsorptionAccessor {
    @Accessor("ABSORPTION_AMOUNT")
    static TrackedData<Float> goldcraft$absorption(){throw new AssertionError();}
}
