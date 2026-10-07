package dev.goldcraft.client;

import com.google.gson.JsonObject;
import dev.goldcraft.bridge.BridgeLink;
import dev.goldcraft.bridge.FramePacer;
import dev.goldcraft.bridge.Wire;
import dev.goldcraft.client.mixin.GameRendererAccessor;
import net.minecraft.client.MinecraftClient;
import org.joml.Vector3f;

/** One real NeoForge camera update shared by hands, entities, particles and CS. */
final class HostCamera {
    private static final FramePacer pacing=new FramePacer(60);
    private static long revision,frames;
    private static float fov=70;
    private HostCamera(){}
    static void frame(MinecraftClient client,BridgeLink link){
        if(!HostInput.hosted()||client.getOverlay()!=null)return;
        var camera=client.gameRenderer.getCamera();
        float delta=client.getRenderTickCounter().getTickDelta(false);
        if(GoldCraftClient.presentationOnly(client))
            camera.update(client.world,client.getCameraEntity()==null?client.player:client.getCameraEntity(),
                !client.options.getPerspective().isFirstPerson(),client.options.getPerspective().isFrontView(),delta);
        client.getEntityRenderDispatcher().configure(client.world,camera,client.targetedEntity);
        if(!HostInput.controlling()||HostInput.life()==0||!pacing.ready(System.nanoTime()))return;
        var position=camera.getPos();
        float pitch=camera.getPitch(),yaw=(float)Math.IEEEremainder(-camera.getYaw()-90,360);
        var up=new Vector3f(0,1,0).rotate(camera.getRotation());
        double p=Math.toRadians(pitch),y=Math.toRadians(yaw);
        double alongRight=up.x*Math.sin(y)+up.z*Math.cos(y);
        double alongUp=up.x*Math.sin(p)*Math.cos(y)-up.z*Math.sin(p)*Math.sin(y)+up.y*Math.cos(p);
        float roll=(float)Math.toDegrees(Math.atan2(alongRight,alongUp));
        fov=(float)Math.max(1,Math.min(178,((GameRendererAccessor)client.gameRenderer).goldcraft$fov(camera,delta,true)));
        int perspective=client.options.getPerspective().ordinal();
        if(link.send(Wire.CAMERA,new Wire.Writer().i64(GoldCraftClient.HOST.epoch()).i64(++revision).i64(System.nanoTime())
            .i32(HostInput.life()).i32(perspective).f32((float)position.x).f32((float)position.y).f32((float)position.z)
            .f32(pitch).f32(yaw).f32(roll).f32(fov).toByteArray()))frames++;
    }
    static void diagnostics(JsonObject data){
        var client=MinecraftClient.getInstance();var camera=client.gameRenderer.getCamera();
        JsonObject value=new JsonObject();value.addProperty("frames",frames);value.addProperty("perspective",client.options.getPerspective().name());
        value.addProperty("x",camera.getPos().x);value.addProperty("y",camera.getPos().y);value.addProperty("z",camera.getPos().z);
        value.addProperty("yaw",camera.getYaw());value.addProperty("pitch",camera.getPitch());value.addProperty("verticalFov",fov);data.add("camera",value);
    }
}
