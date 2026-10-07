package dev.goldcraft.client;

import java.lang.reflect.Field;
import net.neoforged.fml.ModList;

/** Scoped CPU vertex export for the exact supported Epic Fight and maid model APIs. */
final class ModMeshCapture implements AutoCloseable {
    private static boolean resolved;
    private static Field compute,maidCapture;
    private boolean previousCompute,previousMaid;
    static ModMeshCapture begin(){return new ModMeshCapture();}
    private ModMeshCapture(){
        try {
            if(!resolved){
                if(ModList.get().isLoaded("epicfight"))compute=Class.forName("yesman.epicfight.config.ClientConfig").getField("activateComputeShader");
                if(ModList.get().isLoaded("ysm_geo_compat"))maidCapture=Class.forName("com.ysmef.geomodel.renderer.RenderPaths").getField("externalVertexCapture");
                resolved=true;
            }
            if(compute!=null){previousCompute=compute.getBoolean(null);compute.setBoolean(null,false);}
            if(maidCapture!=null){previousMaid=maidCapture.getBoolean(null);maidCapture.setBoolean(null,true);}
        }catch(ReflectiveOperationException error){throw new IllegalStateException("Installed animation Mod does not expose its verified vertex-capture API",error);}
    }
    @Override public void close(){
        try {
            if(maidCapture!=null)maidCapture.setBoolean(null,previousMaid);
            if(compute!=null)compute.setBoolean(null,previousCompute);
        }catch(IllegalAccessException error){throw new IllegalStateException(error);}
    }
}
