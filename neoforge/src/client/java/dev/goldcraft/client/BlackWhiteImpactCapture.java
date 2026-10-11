package dev.goldcraft.client;

import dev.goldcraft.bridge.PostEffectPlan;
import java.lang.reflect.Field;
import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import java.util.function.LongSupplier;

/** CPU-only adapter for the exact pinned NightFall BWC_Pipeline. Resolved once after JAR verification. */
final class BlackWhiteImpactCapture {
    static final String EFFECT="com.hm.efn.client.screeneffect.BlackWhiteFlashEffect";
    interface Uniforms {
        float get(String name,int component,int size);
        int integer(String name);
        float clear(int component);
        float blit(int component);
        void set(String name,float value);
        void set(String name,float x,float y);
    }
    private final Field start,effect,position,mode,shouldRender,contrast,brightness;
    private final Field duration,intensity,speed,modeValue,threshold,thresholdLerp,chromatic,lens;
    private final Object config;
    private final Method configGet,project;
    private final LongSupplier clock;

    BlackWhiteImpactCapture()throws ReflectiveOperationException {this(System::nanoTime);}
    BlackWhiteImpactCapture(LongSupplier clock)throws ReflectiveOperationException {
        this.clock=clock;
        Class<?> owner=Class.forName(EFFECT),pipeline=Class.forName(EFFECT+"$BWC_Pipeline");
        Class<?> impactMode=Class.forName(EFFECT+"$ImpactMode");
        start=field(pipeline,"startNanos");effect=field(pipeline.getSuperclass(),"effect");
        position=field(owner.getSuperclass(),"pos");mode=field(owner,"impactMode");
        shouldRender=field(owner,"should_render");contrast=field(owner,"contrast");brightness=field(owner,"brightness");
        duration=field(impactMode,"durationSec");intensity=field(impactMode,"intensity");speed=field(impactMode,"speed");
        modeValue=field(impactMode,"modeValue");threshold=field(impactMode,"impactThreshold");
        thresholdLerp=field(impactMode,"impactThresholdLerp");chromatic=field(impactMode,"chromaticStrength");
        lens=field(impactMode,"lensDistortStrength");
        config=Class.forName("com.hm.efn.EFNClientConfig").getField("IMPACT_FRAME").get(null);
        configGet=config.getClass().getMethod("get");
        // Use the original fixed70-degree projection, including window aspect and behind-camera fallback.
        project=pipeline.getDeclaredMethod("worldToScreenUV",position.getType());project.setAccessible(true);
    }

    PostEffectPlan.Impact capture(Object pipeline,Uniforms uniforms)throws ReflectiveOperationException {
        if(!((Boolean)invoke(configGet,config)))return null;
        long now=clock.getAsLong(),started=start.getLong(pipeline);
        if(started==0){started=now;start.setLong(pipeline,started);}
        Object source=effect.get(pipeline),impactMode=mode.get(source);
        double elapsed=(now-started)/1.0e9;
        float time=(float)Math.min(elapsed/(double)duration.getFloat(impactMode),1.0);
        if(!shouldRender.getBoolean(source)||time>=1)return null;
        float[] uv=(float[])invoke(project,null,position.get(source));
        var result=new PostEffectPlan.Impact(contrast.getFloat(source),brightness.getFloat(source),time,
            intensity.getFloat(impactMode),speed.getFloat(impactMode),modeValue.getFloat(impactMode),
            threshold.getFloat(impactMode),thresholdLerp.getFloat(impactMode),uv[0],uv[1],
            chromatic.getFloat(impactMode),lens.getFloat(impactMode),
            uniforms.get("ColorDark",0,3),uniforms.get("ColorDark",1,3),uniforms.get("ColorDark",2,3),
            uniforms.get("ColorLight",0,3),uniforms.get("ColorLight",1,3),uniforms.get("ColorLight",2,3),
            uniforms.get("InvertFactor",0,1),uniforms.get("CappedGrayscale",0,1),
            uniforms.get("RadialStrength",0,1),uniforms.integer("Samples"),
            uniforms.clear(0),uniforms.clear(1),uniforms.clear(2),uniforms.clear(3),
            uniforms.blit(0),uniforms.blit(1),uniforms.blit(2),uniforms.blit(3));
        // Preserve the producer's effective CPU uniform writes, without binding or querying GL.
        uniforms.set("Contrast",result.contrast());uniforms.set("Brightness",result.brightness());
        uniforms.set("Time",result.time());uniforms.set("Intensity",result.intensity());
        uniforms.set("Speed",result.speed());uniforms.set("Mode",result.mode());
        uniforms.set("ImpactThreshold",result.impactThreshold());uniforms.set("ImpactThresholdLerp",result.impactThresholdLerp());
        uniforms.set("FocalUV",result.focalU(),result.focalV());
        uniforms.set("ChromaticStrength",result.chromaticStrength());uniforms.set("LensDistortStrength",result.lensDistortStrength());
        return result;
    }

    private static Field field(Class<?> type,String name)throws ReflectiveOperationException {
        Field result=type.getDeclaredField(name);result.setAccessible(true);return result;
    }
    private static Object invoke(Method method,Object receiver,Object...args)throws ReflectiveOperationException {
        try{return method.invoke(receiver,args);}
        catch(InvocationTargetException failure){
            Throwable cause=failure.getCause();
            if(cause instanceof Error fatal)throw fatal;
            if(cause instanceof RuntimeException runtime)throw runtime;
            throw failure;
        }
    }
}
