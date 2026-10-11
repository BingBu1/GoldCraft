package dev.goldcraft.client;

import dev.goldcraft.GoldCraft;
import java.lang.invoke.MethodHandle;
import java.lang.invoke.MethodHandles;
import java.lang.invoke.MethodType;
import net.neoforged.fml.ModList;

/** Uses Epic's real mouse hook, including TPS, lock snapping and free target switching. */
final class HostEpicCamera {
    private static boolean resolved;
    private static MethodHandle instance,turn;
    private static String error="";
    private HostEpicCamera(){}
    static boolean turn(double yawDegrees,double pitchDegrees){
        if(!resolved){
            resolved=true;
            if(ModList.get().isLoaded("epicfight"))try{
                var type=Class.forName("yesman.epicfight.api.client.camera.EpicFightCameraAPI");
                var lookup=MethodHandles.lookup();
                instance=lookup.findStatic(type,"getInstance",MethodType.methodType(type)).asType(MethodType.methodType(Object.class));
                turn=lookup.findVirtual(type,"turnCamera",MethodType.methodType(boolean.class,double.class,double.class))
                    .asType(MethodType.methodType(boolean.class,Object.class,double.class,double.class));
            }catch(ReflectiveOperationException|LinkageError failure){report(failure);}
        }
        if(turn==null)return false;
        try{
            Object camera=instance.invokeExact();
            // Entity.turn and Epic.turnCamera both convert mouse units with 0.15.
            return (boolean)turn.invokeExact(camera,yawDegrees/0.15D,pitchDegrees/0.15D);
        }catch(Throwable failure){
            if(failure instanceof Error fatal)throw fatal;
            report(failure);return false;
        }
    }
    static String error(){return error;}
    private static void report(Throwable failure){
        String next=failure.toString();if(!next.equals(error)){error=next;GoldCraft.LOGGER.warn("Epic host camera input unavailable",failure);}
    }
}
