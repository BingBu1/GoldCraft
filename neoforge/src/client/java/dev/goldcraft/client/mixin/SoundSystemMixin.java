package dev.goldcraft.client.mixin;

import dev.goldcraft.client.HostAudio;
import net.minecraft.client.sound.SoundInstance;
import net.minecraft.client.sound.SoundSystem;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

@Mixin(SoundSystem.class)
public abstract class SoundSystemMixin {
    // This point follows actual resource resolution and source allocation; a
    // server command or a SoundManager.play request alone is not playback evidence.
    @Inject(method="play(Lnet/minecraft/client/sound/SoundInstance;)V",
        at=@At(value="INVOKE",target="Ljava/util/Map;put(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;",ordinal=0))
    private void goldcraft$accepted(SoundInstance sound,CallbackInfo ci){HostAudio.accepted(sound);}
}
