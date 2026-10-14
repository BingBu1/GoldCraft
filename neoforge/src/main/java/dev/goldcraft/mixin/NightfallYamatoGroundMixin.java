package dev.goldcraft.mixin;

import dev.goldcraft.world.NightfallGroundProbe;
import net.minecraft.block.BlockState;
import net.minecraft.util.math.BlockPos;
import net.minecraft.world.World;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Pseudo;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Redirect;

/** Bound the producer's ground search and expose host support to its effects. */
@Pseudo
@Mixin(targets="com.hm.efn.gameasset.animations.EFNYamatoAnimations",remap=false)
public abstract class NightfallYamatoGroundMixin {
    @Redirect(method="lambda$static$3",at=@At(value="INVOKE",
        target="Lnet/minecraft/world/level/Level;getBlockState(Lnet/minecraft/core/BlockPos;)Lnet/minecraft/world/level/block/state/BlockState;"),
        require=2,expect=2,allow=2)
    private static BlockState goldcraft$ground(World world,BlockPos position){
        return NightfallGroundProbe.read(world,position);
    }
}
