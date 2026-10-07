package dev.goldcraft.client.mixin;

import dev.goldcraft.client.HostAudio;
import net.minecraft.client.sound.SoundListener;
import net.minecraft.client.sound.SoundListenerTransform;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.ModifyArg;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

@Mixin(SoundListener.class)
public abstract class SoundListenerMixin {
    @ModifyArg(method="setVolume",at=@At(value="INVOKE",target="Lorg/lwjgl/openal/AL10;alListenerf(IF)V",remap=false),index=1)
    private float goldcraft$focusGain(float volume){return HostAudio.gain(volume);}

    @Inject(method="setTransform",at=@At("TAIL"))
    private void goldcraft$listener(SoundListenerTransform transform,CallbackInfo ci){HostAudio.listener(transform);}
}
