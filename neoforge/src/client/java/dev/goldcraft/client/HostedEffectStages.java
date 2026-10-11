package dev.goldcraft.client;

import com.google.gson.JsonObject;
import dev.goldcraft.GoldCraft;
import java.lang.invoke.MethodHandle;
import java.lang.invoke.MethodHandles;
import java.lang.invoke.MethodType;
import java.util.Map;
import net.minecraft.client.MinecraftClient;
import net.minecraft.client.render.Frustum;
import net.neoforged.fml.ModList;
import net.neoforged.neoforge.client.event.RenderLevelStageEvent;
import org.joml.Matrix4f;
import org.joml.Quaternionf;

/** Replays verified effect producers while the host replaces the normal world frame. */
final class HostedEffectStages {
    interface Hooks {
        boolean hasTrails();
        void afterEntities(MinecraftClient client);
    }

    private static boolean resolved;
    private static HostedEffectStages installed;
    private static Class<?> postParticleType;
    private static String nightfallVersion="",error="";
    private final Hooks hooks;
    private boolean active;
    private long frames,completed,failures;

    HostedEffectStages(Hooks hooks) {this.hooks=hooks;}

    /** Call after entity export, before the actual particle renderer; pair with afterParticles in finally. */
    static boolean beforeParticles(MinecraftClient client) {
        if(!GoldCraftClient.presentationOnly(client))return false;
        resolve();
        if(installed==null)return false;
        try {return installed.begin(true,client);}
        catch(RuntimeException|LinkageError failure) {
            report(failure);
            return false;
        }
    }

    static void afterParticles(boolean owned) {if(owned&&installed!=null)installed.end();}

    /** Exclude these queues in ParticleManager's predicate, including VIX's render-tail injection. */
    static boolean isUnsupportedPostEffect(Object sheet) {
        resolve();
        return postParticleType!=null&&postParticleType.isInstance(sheet);
    }

    static void diagnostics(JsonObject data) {
        data.addProperty("hostedEffectStagePolicy","verified-producers-only");
        if(!nightfallVersion.isEmpty())data.addProperty("hostedNightfallVersion",nightfallVersion);
        if(installed!=null)installed.appendDiagnostics(data);
        if(postParticleType!=null) {
            data.addProperty("hostedVixPostProcessingSupported",false);
            data.addProperty("hostedVixPostProcessingReason",
                "Requires CS scene color/depth, mask targets and ordered shader passes; VIX RenderPost is not replayed");
        }
        if(!error.isEmpty())data.addProperty("hostedEffectStageError",error);
    }

    private static void resolve() {
        if(resolved)return;
        resolved=true;
        var mods=ModList.get();
        if(mods.isLoaded("vix")) {
            try {postParticleType=Class.forName("com.guhao.vix.client.pipeline.PostParticleRenderType");}
            catch(ReflectiveOperationException|LinkageError failure){report(failure);}
        }
        if(mods.isLoaded("efn")) {
            nightfallVersion=mods.getModContainerById("efn").orElseThrow().getModInfo().getVersion().toString();
            if(!nightfallVersion.equals("3.4.0")) {
                report(new IllegalStateException("NightFall stage producer has not been verified for "+nightfallVersion));
                return;
            }
            try {installed=new HostedEffectStages(new NightfallHooks());}
            catch(ReflectiveOperationException|LinkageError failure){report(failure);}
        }
    }

    boolean begin(boolean hosted,MinecraftClient client) {
        if(!hosted||active)return false;
        active=true;
        try {
            if(!hooks.hasTrails()) {active=false;return false;}
            hooks.afterEntities(client);
            frames++;
            return true;
        }catch(RuntimeException|Error failure) {
            active=false;
            failures++;
            throw failure;
        }
    }

    void end() {if(active){active=false;completed++;}}

    void appendDiagnostics(JsonObject data) {
        data.addProperty("hostedAfterEntitiesFrames",frames);
        data.addProperty("hostedEffectStagesCompleted",completed);
        data.addProperty("hostedEffectStageFailures",failures);
        data.addProperty("hostedEffectStageActive",active);
    }

    private static void report(Throwable failure) {
        String message=failure.toString();
        if(!message.equals(error)) {error=message;GoldCraft.LOGGER.warn("Hosted effect stage unavailable",failure);}
    }

    /** Exact NightFall 3.4.0 callback: reads stage/partial tick and adds snapshots; it does not draw. */
    private static final class NightfallHooks implements Hooks {
        private final MethodHandle trails,afterEntities;
        private final Quaternionf inverseCamera=new Quaternionf();
        private final Matrix4f modelView=new Matrix4f();

        NightfallHooks() throws ReflectiveOperationException {
            var type=Class.forName("com.hm.efn.client.render.AfterImageManager");
            var lookup=MethodHandles.lookup();
            var field=type.getDeclaredField("ACTIVE_TRAILS");
            field.setAccessible(true);
            trails=lookup.unreflectGetter(field).asType(MethodType.methodType(Map.class));
            afterEntities=lookup.findStatic(type,"onRenderLevel",MethodType.methodType(void.class,RenderLevelStageEvent.class));
        }

        public boolean hasTrails() {
            try {return !((Map<?,?>)trails.invokeExact()).isEmpty();}
            catch(Throwable failure){throw failed(failure);}
        }

        public void afterEntities(MinecraftClient client) {
            var camera=client.gameRenderer.getCamera();
            var ticks=client.getRenderTickCounter();
            camera.getRotation().conjugate(inverseCamera);
            modelView.rotation(inverseCamera);
            double fov=HostCamera.verticalFov();
            var projection=client.gameRenderer.getBasicProjectionMatrix(fov);
            // GameRenderer uses the unbobbed projection with max(dynamic FOV, configured FOV) for culling.
            var frustum=new Frustum(modelView,client.gameRenderer.getBasicProjectionMatrix(Math.max(fov,client.options.getFov().getValue())));
            var position=camera.getPos();
            frustum.setPosition(position.x,position.y,position.z);
            var event=new RenderLevelStageEvent(RenderLevelStageEvent.Stage.AFTER_ENTITIES,client.worldRenderer,null,
                modelView,projection,client.worldRenderer.getTicks(),ticks,camera,frustum);
            // Do not broadcast the event bus: other subscribers may draw, post effects or repeat entity work.
            // This verified callback ignores pose/projection; the event is not a substitute for a complete world frame.
            try {afterEntities.invokeExact(event);}
            catch(Throwable failure){throw failed(failure);}
        }

        private static RuntimeException failed(Throwable failure) {
            if(failure instanceof Error fatal)throw fatal;
            return failure instanceof RuntimeException runtime?runtime:new IllegalStateException("NightFall after-entity producer failed",failure);
        }
    }
}
