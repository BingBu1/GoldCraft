package dev.goldcraft.client;

import com.google.gson.JsonObject;
import net.minecraft.client.MinecraftClient;
import org.joml.Matrix4f;
import org.joml.Quaternionf;

/** YSM native face tests must see the same camera basis as the exported world vertices. */
public final class YsmCaptureProjection {
    private static final Matrix4f worldToClip=new Matrix4f();
    private static final Quaternionf inverseCamera=new Quaternionf();
    private static boolean active;
    private static long frames,draws;
    private YsmCaptureProjection() {}

    static void begin() {
        var client=MinecraftClient.getInstance();
        var camera=client.gameRenderer.getCamera();
        double fov=HostCamera.verticalFov();
        var position=camera.getPos();
        begin(client.gameRenderer.getBasicProjectionMatrix(fov),camera.getRotation(),
            (float)position.x,(float)position.y,(float)position.z);
    }
    static void begin(Matrix4f projection,Quaternionf camera,float x,float y,float z) {
        camera.conjugate(inverseCamera);
        worldToClip.set(projection).rotate(inverseCamera).translate(-x,-y,-z);
        active=true;frames++;
    }
    static void end() {active=false;}
    public static Matrix4f forNativeDraw(Matrix4f original) {
        if(active){draws++;original.set(worldToClip);}
        return original;
    }
    static void diagnostics(JsonObject data) {
        data.addProperty("ysmProjectionFrames",frames);
        data.addProperty("ysmProjectionDraws",draws);
        data.addProperty("ysmProjectionActive",active);
    }
}
