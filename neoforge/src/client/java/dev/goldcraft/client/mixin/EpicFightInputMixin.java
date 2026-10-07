package dev.goldcraft.client.mixin;

import dev.goldcraft.client.HostKeys;
import org.lwjgl.glfw.GLFW;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Pseudo;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Redirect;

/** Exact Epic Fight21.17 InputManager physical-hold workaround for shared mouse bindings. */
@Pseudo
@Mixin(targets="yesman.epicfight.api.client.input.InputManager",remap=false)
abstract class EpicFightInputMixin {
    @Redirect(method="isPhysicalKeyDown",at=@At(value="INVOKE",target="Lorg/lwjgl/glfw/GLFW;glfwGetKey(JI)I"))
    private static int goldcraft$keyboard(long window,int key){
        return HostKeys.overrides(window)?(HostKeys.keyDown(key)?GLFW.GLFW_PRESS:GLFW.GLFW_RELEASE):GLFW.glfwGetKey(window,key);
    }
    @Redirect(method="isPhysicalKeyDown",at=@At(value="INVOKE",target="Lorg/lwjgl/glfw/GLFW;glfwGetMouseButton(JI)I"))
    private static int goldcraft$mouse(long window,int key){
        return HostKeys.overrides(window)?(HostKeys.mouseDown(key)?GLFW.GLFW_PRESS:GLFW.GLFW_RELEASE):GLFW.glfwGetMouseButton(window,key);
    }
}
