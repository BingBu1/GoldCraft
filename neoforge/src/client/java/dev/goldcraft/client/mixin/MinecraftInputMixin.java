package dev.goldcraft.client.mixin;

import dev.goldcraft.client.HostInput;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.ModifyVariable;
import net.minecraft.client.MinecraftClient;

@Mixin(MinecraftClient.class)
public abstract class MinecraftInputMixin {
    @ModifyVariable(method="handleBlockBreaking",at=@At("HEAD"),argsOnly=true)
    private boolean goldcraft$hostHeldAttack(boolean original){return HostInput.controlling()?HostInput.attacking()&&((MinecraftClient)(Object)this).currentScreen==null:original;}
}
