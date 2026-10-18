package dev.goldcraft.mixin;

import net.minecraft.server.world.ServerWorld;
import net.minecraft.util.math.BlockPos;
import net.minecraft.village.raid.Raid;
import net.minecraft.village.raid.RaidManager;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.gen.Accessor;
import org.spongepowered.asm.mixin.gen.Invoker;
import java.util.Map;

@Mixin(RaidManager.class)
public interface NativeRaidManagerAccessor {
    @Accessor("raids") Map<Integer,Raid> goldcraft$raids();
    @Invoker("getOrCreateRaid") Raid goldcraft$raid(ServerWorld world,BlockPos pos);
}
