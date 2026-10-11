package dev.goldcraft.client;

import java.lang.reflect.Field;
import com.google.gson.JsonObject;
import net.neoforged.fml.ModList;

/** Scoped CPU vertex export for the exact supported Epic Fight and maid model APIs. */
final class ModMeshCapture implements AutoCloseable {
    private static boolean resolved;
    private static Field compute,maidCapture;
    private static long frames,computeFallbacks;
    private final Field scopeCompute,scopeMaid;
    private boolean previousCompute,previousMaid,savedCompute,savedMaid,closed;
    static ModMeshCapture begin(){
        try {
            if(!resolved){
                if(ModList.get().isLoaded("epicfight"))compute=Class.forName("yesman.epicfight.config.ClientConfig").getField("activateComputeShader");
                if(ModList.get().isLoaded("ysm_geo_compat"))maidCapture=Class.forName("com.ysmef.geomodel.renderer.RenderPaths").getField("externalVertexCapture");
                resolved=true;
            }
        }catch(ReflectiveOperationException error){throw new IllegalStateException("Installed animation Mod does not expose its verified vertex-capture API",error);}
        return new ModMeshCapture(compute,maidCapture);
    }
    ModMeshCapture(Field computeField,Field maidField){
        scopeCompute=computeField;scopeMaid=maidField;
        try {
            if(scopeCompute!=null){previousCompute=scopeCompute.getBoolean(null);savedCompute=true;scopeCompute.setBoolean(null,false);}
            if(scopeMaid!=null){previousMaid=scopeMaid.getBoolean(null);savedMaid=true;scopeMaid.setBoolean(null,true);}
            frames++;if(previousCompute)computeFallbacks++;
        }catch(ReflectiveOperationException error){
            var failure=new IllegalStateException("Installed animation Mod does not expose its verified vertex-capture API",error);
            new CaptureCleanup(failure).run(this::close);throw failure;
        }catch(RuntimeException|Error failure){new CaptureCleanup(failure).run(this::close);throw failure;}
    }
    @Override public void close(){
        if(closed)return;closed=true;
        var cleanup=new CaptureCleanup();
        if(savedMaid)cleanup.run(()->restore(scopeMaid,previousMaid));
        if(savedCompute)cleanup.run(()->restore(scopeCompute,previousCompute));
        cleanup.rethrow();
    }
    private static void restore(Field field,boolean value) {
        try {field.setBoolean(null,value);}catch(IllegalAccessException error){throw new IllegalStateException(error);}
    }
    static void diagnostics(JsonObject data) {
        data.addProperty("effectCpuCaptureFrames",frames);
        data.addProperty("effectComputeFallbackFrames",computeFallbacks);
    }
}
