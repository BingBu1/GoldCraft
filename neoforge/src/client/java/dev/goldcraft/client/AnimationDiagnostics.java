package dev.goldcraft.client;

import com.google.gson.JsonObject;
import java.lang.invoke.MethodHandle;
import java.lang.invoke.MethodHandles;
import java.lang.invoke.MethodType;
import net.minecraft.entity.player.PlayerEntity;
import net.minecraft.entity.LivingEntity;
import dev.goldcraft.world.NativePlayerHitboxEntity;
import net.neoforged.fml.ModList;

/** Samples the actual Epic Fight mode only when optional sandbox diagnostics run. */
final class AnimationDiagnostics {
    private static boolean resolved;
    private static MethodHandle playerPatch,playerMode,cameraInstance,cameraTarget,cameraLocked;
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
                var camera=Class.forName("yesman.epicfight.api.client.camera.EpicFightCameraAPI");
                cameraInstance=lookup.findStatic(camera,"getInstance",MethodType.methodType(camera))
                    .asType(MethodType.methodType(Object.class));
                cameraTarget=lookup.findVirtual(camera,"getFocusingEntity",MethodType.methodType(LivingEntity.class))
                    .asType(MethodType.methodType(LivingEntity.class,Object.class));
                cameraLocked=lookup.findVirtual(camera,"isLockingOnTarget",MethodType.methodType(boolean.class))
                    .asType(MethodType.methodType(boolean.class,Object.class));
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
            Object camera=cameraInstance.invokeExact();
            LivingEntity target=(LivingEntity)cameraTarget.invokeExact(camera);
            var value=new JsonObject();value.addProperty("locked",(boolean)cameraLocked.invokeExact(camera));
            value.addProperty("entityId",target==null?-1:target.getId());
            if(target instanceof NativePlayerHitboxEntity nativeBody){
                value.addProperty("slot",nativeBody.hostSlot());value.addProperty("serial",nativeBody.hostSerial());
                value.addProperty("life",nativeBody.hostLife());value.addProperty("epoch",Long.toUnsignedString(nativeBody.hostEpoch()));
                value.addProperty("eligible",HostEpicTargets.canTarget(nativeBody));
                for(var actor:GoldCraftClient.HOST.actors())if(actor.slot()==nativeBody.hostSlot())value.addProperty("targetTeam",actor.team());
            }
            var viewer=GoldCraftClient.HOST.actor(player.getUuid());if(viewer!=null)value.addProperty("viewerTeam",viewer.team());
            data.add("epicFightTarget",value);
        }catch(Throwable error) {
            if(error instanceof VirtualMachineError fatal)throw fatal;
            if(error instanceof ThreadDeath fatal)throw fatal;
            data.addProperty("epicFightModeError",error.toString());
        }
    }
}
