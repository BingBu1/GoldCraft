package dev.goldcraft.client.mixin;

import net.minecraft.client.Mouse;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.gen.Invoker;

@Mixin(Mouse.class)
public interface MouseAccessor {
    @Invoker("onMouseButton") void goldcraft$button(long window,int button,int action,int modifiers);
    @Invoker("onMouseScroll") void goldcraft$scroll(long window,double horizontal,double vertical);
}
