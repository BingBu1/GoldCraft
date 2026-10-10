package dev.goldcraft.client;

import com.google.gson.JsonObject;
import java.lang.invoke.MethodHandle;
import java.lang.invoke.MethodHandles;
import java.lang.invoke.MethodType;
import net.minecraft.entity.player.PlayerEntity;
import net.neoforged.fml.ModList;

/** Samples the actual Epic Fight mode only when optional sandbox diagnostics run. */
final class AnimationDiagnostics {
    private static boolean resolved;
    private static MethodHandle playerPatch,playerMode;
    private static String bindingError="";

    private AnimationDiagnostics() {}

    static void sample(PlayerEntity player,JsonObject data) {
        if(player==null)return;
        if(!resolved) {
            resolved=true;
            if(!ModList.get().isLoaded("epicfight"))return;
            try {
                var lookup=MethodHandles.lookup();
                var capabilities=Class.forName("yesman.epicfight.world.capabilities.EpicFightCapabilities");
                var patch=Class.forName("yesman.epicfight.world.capabilities.entitypatch.player.PlayerPatch");
                var mode=Class.forName("yesman.epicfight.world.capabilities.entitypatch.player.PlayerPatch$PlayerMode");
                var patchGetter=lookup.findStatic(capabilities,"getPlayerPatch",MethodType.methodType(patch,PlayerEntity.class));
                var modeGetter=lookup.findVirtual(patch,"getPlayerMode",MethodType.methodType(mode));
                playerPatch=patchGetter.asType(MethodType.methodType(Object.class,PlayerEntity.class));
                playerMode=modeGetter.asType(MethodType.methodType(Object.class,Object.class));
            }catch(ReflectiveOperationException|RuntimeException error) {
                bindingError=error.toString();
            }
        }
        if(!bindingError.isEmpty()){data.addProperty("epicFightModeError",bindingError);return;}
        if(playerMode==null)return;
        try {
            Object patch=playerPatch.invokeExact(player);
            if(patch==null){data.addProperty("epicFightMode","UNINITIALIZED");return;}
            Object mode=playerMode.invokeExact(patch);
            data.addProperty("epicFightMode",((Enum<?>)mode).name());
        }catch(Throwable error) {
            if(error instanceof VirtualMachineError fatal)throw fatal;
            if(error instanceof ThreadDeath fatal)throw fatal;
            data.addProperty("epicFightModeError",error.toString());
        }
    }
}
