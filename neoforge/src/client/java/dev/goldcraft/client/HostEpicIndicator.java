package dev.goldcraft.client;

import com.google.gson.JsonObject;
import dev.goldcraft.GoldCraft;
import dev.goldcraft.world.NativePlayerHitboxEntity;
import java.lang.invoke.MethodHandle;
import java.lang.invoke.MethodHandles;
import java.lang.invoke.MethodType;
import net.minecraft.client.MinecraftClient;
import net.minecraft.client.render.VertexConsumerProvider;
import net.minecraft.client.util.math.MatrixStack;
import net.minecraft.entity.Entity;
import net.minecraft.entity.LivingEntity;
import net.minecraft.entity.player.PlayerEntity;
import net.minecraft.util.math.MathHelper;
import net.minecraft.world.World;
import net.neoforged.fml.ModList;

/** The native proxy has no LivingEntityRenderer, so explicitly run Epic's own target UI. */
final class HostEpicIndicator {
    private static boolean resolved;
    private static MethodHandle playerPatch,target,entityPatch,shouldDraw,draw,disabled;
    private static Object indicator,rule;
    private static long draws,failures;
    private static int lastEntity=-1;
    private static String error="";
    private HostEpicIndicator(){}
    static void render(MinecraftClient client,float delta,VertexConsumerProvider buffers){
        lastEntity=-1;
        if(!HostInput.controlling()||client.options.hudHidden||client.player==null||!client.player.isAlive())return;
        resolve();if(draw==null)return;
        try{
            Object patch=playerPatch.invokeExact((PlayerEntity)client.player);
            if(patch==null)return;
            LivingEntity focused=(LivingEntity)target.invokeExact(patch);
            if(!(focused instanceof NativePlayerHitboxEntity body)||!HostEpicTargets.canTarget(body))return;
            if((boolean)disabled.invokeExact(rule,(World)client.world))return;
            Object bodyPatch=entityPatch.invokeExact((Entity)body);
            if(!(boolean)shouldDraw.invokeExact(indicator,(LivingEntity)body,bodyPatch,patch,delta))return;
            MatrixStack matrices=new MatrixStack();
            matrices.translate(MathHelper.lerp(delta,body.lastRenderX,body.getX()),
                MathHelper.lerp(delta,body.lastRenderY,body.getY()),MathHelper.lerp(delta,body.lastRenderZ,body.getZ()));
            draw.invokeExact(indicator,(LivingEntity)body,bodyPatch,patch,matrices,buffers,delta);
            draws++;lastEntity=body.getId();
        }catch(Throwable failure){
            if(failure instanceof Error fatal)throw fatal;
            failures++;report(failure);
        }
    }
    static void diagnostics(JsonObject data){
        data.addProperty("epicIndicatorDraws",draws);data.addProperty("epicIndicatorEntity",lastEntity);
        data.addProperty("epicIndicatorFailures",failures);if(!error.isEmpty())data.addProperty("epicIndicatorError",error);
    }
    private static void resolve(){
        if(resolved)return;resolved=true;
        if(!ModList.get().isLoaded("epicfight"))return;
        try{
            var lookup=MethodHandles.lookup();
            var caps=Class.forName("yesman.epicfight.world.capabilities.EpicFightCapabilities");
            var playerType=Class.forName("yesman.epicfight.world.capabilities.entitypatch.player.PlayerPatch");
            var localType=Class.forName("yesman.epicfight.client.world.capabilites.entitypatch.player.LocalPlayerPatch");
            var livingType=Class.forName("yesman.epicfight.world.capabilities.entitypatch.LivingEntityPatch");
            var patchType=Class.forName("yesman.epicfight.world.capabilities.entitypatch.EntityPatch");
            var ui=Class.forName("yesman.epicfight.client.gui.EntityUI");
            var rules=Class.forName("yesman.epicfight.world.gamerule.EpicFightGameRules");
            var ruleType=Class.forName("yesman.epicfight.world.gamerule.EpicFightGameRules$ConfigurableGameRule");
            indicator=ui.getField("TARGET_INDICATOR").get(null);rule=rules.getField("DISABLE_ENTITY_UI").get(null);
            playerPatch=lookup.findStatic(caps,"getPlayerPatch",MethodType.methodType(playerType,PlayerEntity.class)).asType(MethodType.methodType(Object.class,PlayerEntity.class));
            target=lookup.findVirtual(localType,"getTarget",MethodType.methodType(LivingEntity.class)).asType(MethodType.methodType(LivingEntity.class,Object.class));
            entityPatch=MethodHandles.insertArguments(lookup.findStatic(caps,"getEntityPatch",MethodType.methodType(patchType,Entity.class,Class.class)),1,livingType)
                .asType(MethodType.methodType(Object.class,Entity.class));
            shouldDraw=lookup.findVirtual(ui,"shouldDraw",MethodType.methodType(boolean.class,LivingEntity.class,livingType,localType,float.class))
                .asType(MethodType.methodType(boolean.class,Object.class,LivingEntity.class,Object.class,Object.class,float.class));
            disabled=lookup.findVirtual(ruleType,"getRuleValue",MethodType.methodType(Object.class,World.class))
                .asType(MethodType.methodType(boolean.class,Object.class,World.class));
            draw=lookup.findVirtual(ui,"draw",MethodType.methodType(void.class,LivingEntity.class,livingType,localType,MatrixStack.class,VertexConsumerProvider.class,float.class))
                .asType(MethodType.methodType(void.class,Object.class,LivingEntity.class,Object.class,Object.class,MatrixStack.class,VertexConsumerProvider.class,float.class));
        }catch(ReflectiveOperationException|LinkageError failure){report(failure);}
    }
    private static void report(Throwable failure){
        String next=failure.toString();if(!next.equals(error)){error=next;GoldCraft.LOGGER.warn("Epic host target indicator unavailable",failure);}
    }
}
