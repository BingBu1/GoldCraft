package dev.goldcraft.client;

import com.google.gson.JsonObject;
import dev.goldcraft.GoldCraft;
import dev.goldcraft.bridge.ParticleSnapshot;
import dev.goldcraft.bridge.PostEffectPlan;
import dev.goldcraft.client.mixin.ParticleManagerAccessor;
import dev.goldcraft.client.mixin.LightmapTextureAccessor;
import dev.goldcraft.client.mixin.FramebufferClearColorAccessor;
import java.io.IOException;
import java.lang.reflect.Field;
import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import java.nio.file.Files;
import java.nio.ByteBuffer;
import java.security.MessageDigest;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.HexFormat;
import java.util.IdentityHashMap;
import java.util.List;
import java.util.Map;
import java.util.Queue;
import java.util.Set;
import net.minecraft.client.MinecraftClient;
import net.minecraft.client.gl.JsonEffectShaderProgram;
import net.minecraft.client.particle.ParticleTextureSheet;
import net.minecraft.client.render.Frustum;
import net.minecraft.util.Identifier;
import net.neoforged.fml.ModList;
import net.neoforged.neoforge.client.event.RenderLevelStageEvent;
import org.joml.Matrix4f;
import org.joml.Quaternionf;

/**
 * Runs the installed VIX particle-tail once. Optional mixins replace only target
 * bookkeeping; the producer still chooses its shader, builder, geometry and sorted
 * indices. Typed passes consume CS scene color/depth, never the hidden MC target.
 */
public final class VixPostCapture {
    record Frame(List<PostEffectPlan.Stage> stages,List<DirectMeshCapture.Draw> draws,List<Integer> depthResets,int count) {
        static Frame empty(){return new Frame(List.of(),List.of(),List.of(),0);}
    }
    private static final String ROOT="com.guhao.vix.client.pipeline.PostEffectPipelines";
    private static final String TYPE="com.guhao.vix.client.pipeline.PostParticleRenderType";
    private static final String NF="com.hm.efn.client.render.custom.";
    private static final String SCREEN="com.hm.efn.client.screeneffect.ScreenDistortionEffect$Distortion_Pipeline";
    private static final String DISPERSION="com.hm.efn.client.screeneffect.ColorDispersionEffect";
    private static Api api;
    private static boolean resolved,resourcesVerified;
    private static Context active;
    private static String error="",lastFailure="",lastFailureParticles="";
    private static long frames,failures,nonemptyFrames,lastFailureLog,undefinedNightfallBlades,expiredScreenDistortions;
    private static final long[] stageFrames=new long[PostEffectPlan.BW_CONTRAST+1];
    private static int lastStages,lastDraws;
    private static final Map<String,String> RESOURCE_HASHES=Map.ofEntries(
        Map.entry("black_white_simple.fsh","89432ec2c4e4565898eeece0d70f1a1d1999ce59790be13d946ebf16825d3abe"),
        Map.entry("black_white_simple.json","e398b28b123ee73a18ea3d8838ddb1d12f0988536b431325f6cf476bdf5b9912"),
        Map.entry("air_disturbance.fsh","336c0123bc1bd5a7cdca6190ce6a21976035cc623d9c3cf80e7dceb2f629a0cf"),
        Map.entry("air_disturbance.json","4c0a9e4288d9cc1fb8c8be10693ff0b1a9b241e217c7d76176f1a79dcedd35e8"),
        Map.entry("chromatic_aberration.fsh","61c0105ed280fe4eed7dace3d1c89701d8ee7ca51be53cee253a8038bc666305"),
        Map.entry("chromatic_aberration.json","a183275b7925f052b50fc575f317a9f965cd3752ea42961c4813f3c21adbcea4"),
        Map.entry("color_dispersion.fsh","08dcd85523d534a8af5db2b154146475b7d9aefe9d26ea9845d7193d149b7fa9"),
        Map.entry("color_dispersion.json","e906992640eab5efdf811956566ecf3ccd2b581cdea71050df52577e9874baad"),
        Map.entry("screen_distortion.fsh","2a710be6c5493d4971587c05ea627685826955f56de04005d3c3cc12e2194f14"),
        Map.entry("screen_distortion.json","8fe20b847232f5596f9960171fb239eea5b07d0355535c869eeb911e3b49128d"),
        Map.entry("space_broken.fsh","9d76668d129c05a217170d39c9206e191b006c4b35b1694e508f75bf12e44e67"),
        Map.entry("space_broken.json","063bf5a9ba1849997ad80abb49625ef393f5d2b90e2377fa625ff202b359a384"),
        Map.entry("unity_composite.fsh","cf87d4ff378f96fdafc8af8d32679f3786a85953ef282e55e6319496ab7b5365"),
        Map.entry("unity_composite.json","5afa00c978d46e1a1cf09f20974c882742605599ce9109f3e7e65451b341b4f7"),
        Map.entry("down_sampling.fsh","f52ac09094c5778871ccfd53b117157a256835644e1598c7d42ea09635c6ae2b"),
        Map.entry("down_sampling.json","daf5e9e031f1bea294f2ab7bc87857c6a7cf3c36c79f39d996e9de29086341b1"),
        Map.entry("up_sampling.fsh","1e59ec8371be20e6de9aefee2d0aa0579bfe1b032ef19123f1db0e44e932bbf4"),
        Map.entry("up_sampling.json","086636109719b983299c14d1613bc26aaf2b82ab707bf6f579cebe3988af8b5b"),
        Map.entry("blit.fsh","bb9351ccd461eef6a0b6780a8756691081fbc4b4e0fa34734fb1f5db37d92b18"),
        Map.entry("blit.json","a164a89fcbde1b80d1db118ed892b8d1caa506274aa859997c0cb3017711ff1f"),
        Map.entry("blit.vsh","87b5456f4fbf4565af12da6db03cd756d748fbbdb07f6d46f761d8ac64449d19"),
        Map.entry("minecraft:shaders/core/position_color_tex_lightmap.vsh","419b8906a583fd4172cd744bc043414a22089d77eda4738513488a366ed7c0ad"),
        Map.entry("minecraft:shaders/core/position_color_tex_lightmap.fsh","0f4f21b345f7244effef1b9e2cf6cec91318cc77dd1ce893e97546693a4a3278"),
        Map.entry("minecraft:shaders/core/position_color_tex_lightmap.json","f64f9193fa57cf1751f413ab3f25287235dcb3db7b85423fb9b2f4d6af93a947"),
        Map.entry("minecraft:shaders/core/position_color_lightmap.vsh","3e60e8f3ce49deda01d31112e835c65e6521d9ac46a6f7010097503f2bd8e050"),
        Map.entry("minecraft:shaders/core/position_color_lightmap.fsh","bd3b3424d34f1d861cbf57378daf08b6bd1c65239ad0a2f0760b63afc476c666"),
        Map.entry("minecraft:shaders/core/position_color_lightmap.json","53e0a7d1a51a8cd9f1f044e1845b7d2b152d68d340abe87603f181d870cb499a"),
        Map.entry("minecraft:shaders/core/particle.vsh","b1d9f46ed0d464bcee6ba31219489aed39cf06d48680f0f107e08ea19240eb21"),
        Map.entry("minecraft:shaders/core/particle.fsh","7d07dcf396ae055be1c70bc7fdbfd12d2ac22bf145ce9dca40cb3f1038258b59"),
        Map.entry("minecraft:shaders/core/particle.json","1acb68ecbb60b06ed1acb7a3f0f0fdf64cc18b47f56850de450a33a22f9b5f59"));
    private VixPostCapture(){}

    static Frame capture(MinecraftClient client,float partialTick) {
        resolve();
        if(api==null)return Frame.empty();
        if(active!=null)throw new IllegalStateException("Nested VIX capture");
        try {
            verifyResources(client);
            if(api.activeFlag.getBoolean(null)||api.shared.getBoolean(null)||!api.queue.isEmpty()||!api.priorityQueue.isEmpty())
                throw new IllegalStateException("VIX already owns an unfinished frame");
            int particleCount=0;
            var queues=((ParticleManagerAccessor)client.particleManager).goldcraft$particles();
            for(var entry:queues.entrySet())if(api.type.isInstance(entry.getKey())&&!entry.getValue().isEmpty()) {
                api.pipeline(entry.getKey()); // Preflight every type before calling its beginPost.
                particleCount=Math.addExact(particleCount,entry.getValue().size());
                if(particleCount>ParticleSnapshot.MAX_PARTICLES)throw new IllegalArgumentException("VIX particle budget");
            }
            var camera=client.gameRenderer.getCamera();
            Matrix4f modelView=new Matrix4f().rotation(camera.getRotation().conjugate(new Quaternionf()));
            double fov=HostCamera.verticalFov();
            var projection=client.gameRenderer.getBasicProjectionMatrix(fov);
            var frustum=new Frustum(modelView,client.gameRenderer.getBasicProjectionMatrix(Math.max(fov,client.options.getFov().getValue())));
            var position=camera.getPos();frustum.setPosition(position.x,position.y,position.z);
            try(var cpu=ModMeshCapture.begin();var capture=DirectMeshCapture.begin(position,true)) {
                Context context=new Context(capture,client);active=context;
                try {
                    invoke(api.activate,null);
                    // Ordinary PostParticleRenderType.begin returns null. The real VIX tail
                    // renders only these types, descending priority with ORTHOGRAPHIC_Z sorting.
                    client.particleManager.render(client.gameRenderer.getLightmapTextureManager(),camera,partialTick,frustum,
                        sheet->api.type.isInstance(sheet));
                    if(particleCount>0&&(!context.savedDepth||context.starts==0))
                        throw new IllegalStateException("Verified VIX capture hooks did not execute");
                    if(context.current!=null)throw new IllegalStateException("Unfinished VIX mask");
                    capture.finish();
                    if(!capture.unsupported().isEmpty())throw new IllegalStateException("Unsupported VIX mask material: "+capture.unsupported());
                    if(capture.drawCount()!=0)throw new IllegalStateException("VIX mesh without pipeline ownership");
                    var event=new RenderLevelStageEvent(RenderLevelStageEvent.Stage.AFTER_PARTICLES,client.worldRenderer,null,
                        modelView,projection,client.worldRenderer.getTicks(),client.getRenderTickCounter(),camera,frustum);
                    // Exact installed callback retains HashSet insertion iteration and shouldPost/frustum behavior.
                    invoke(api.screenRender,null,event);
                    Frame result=context.drain(client,particleCount);
                    lastStages=result.stages().size();lastDraws=result.draws().size();frames++;error="";
                    if(lastStages>0)nonemptyFrames++;
                    for(var stage:result.stages())stageFrames[stage.kind()]++;
                    return result;
                }finally{
                    try{context.cleanup();}finally{active=null;}
                }
            }
        }catch(ReflectiveOperationException|IOException|RuntimeException|LinkageError failure){
            failures++;lastStages=lastDraws=0;
            lastFailure=failure.toString();lastFailureParticles=failureParticles(client);
            report(failure);return Frame.empty();
        }
    }

    /** Redirected at the actual base beginPost invocation; outside this scope dispatches unchanged. */
    public static void start(Object pipeline) {
        if(active==null){invoke(method(pipeline.getClass(),"start"),pipeline);return;}
        try{active.start(pipeline);}catch(ReflectiveOperationException failure){throw failed(failure);}
    }
    /** Redirected at the actual finish invocation, before VIX restores its ordinary material state. */
    public static void suspend(Object pipeline) {
        if(active==null){invoke(method(pipeline.getClass(),"suspend"),pipeline);return;}
        active.suspend(pipeline);
    }
    public static boolean skipSharedDepth(){if(active==null)return false;active.savedDepth=true;return true;}
    static boolean capturing(){return active!=null;}
    /** Called before the pinned producer submits its first vertex. No coordinate replacement or allocation. */
    public static boolean rejectUndefinedNightfallBlade(org.joml.Vector3fc width,float fade) {
        // active exists only after verifyResources checks NightFall3.4.0 and its exact JAR hash.
        if(active==null||width.isFinite()&&Float.isFinite(fade))return false;
        undefinedNightfallBlades++;
        return true;
    }
    static ByteBuffer maskVertices(ByteBuffer source,int count,EffectGeometry.Layout layout,String shader) {
        if(active==null)return source;
        String name=shader.replace("minecraft:","");
        if(name.equals("particle"))return PostLightmap.shade(source,count,layout,active.lightmap,false);
        if(name.equals("position_color_lightmap"))return PostLightmap.shade(source,count,layout,active.lightmap,true);
        if(!name.equals("position_color_tex_lightmap"))throw new IllegalStateException("Unverified VIX mask shader "+shader);
        return source;
    }
    static void resourcesChanged(){resourcesVerified=false;}
    static boolean supported(){resolve();return api!=null;}
    static void diagnostics(JsonObject data){
        data.addProperty("vixPostCaptureResolved",api!=null);data.addProperty("vixPostResourcesVerified",resourcesVerified);
        data.addProperty("vixPostFrames",frames);data.addProperty("vixPostFailures",failures);
        data.addProperty("vixPostUndefinedNightfallBlades",undefinedNightfallBlades);
        data.addProperty("vixPostExpiredScreenDistortions",expiredScreenDistortions);
        data.addProperty("vixPostStages",lastStages);data.addProperty("vixPostBatches",lastDraws);
        data.addProperty("vixPostCaptureError",error);
        data.addProperty("vixPostLastFailure",lastFailure);data.addProperty("vixPostLastFailureParticles",lastFailureParticles);
        data.addProperty("vixPostNonemptyFrames",nonemptyFrames);
        var kinds=new JsonObject();
        for(int kind=1;kind<stageFrames.length;kind++)kinds.addProperty(Integer.toString(kind),stageFrames[kind]);
        data.add("vixPostStageCounts",kinds);
    }

    /** Bounded evidence from the failed frame's queues, not an attribution to one particle. */
    private static String failureParticles(MinecraftClient client) {
        try {
            var counts=new java.util.TreeMap<String,Integer>();int inspected=0;
            var queues=((ParticleManagerAccessor)client.particleManager).goldcraft$particles();
            for(var entry:queues.entrySet())if(api.type.isInstance(entry.getKey())) {
                for(var particle:entry.getValue()) {
                    if(++inspected>ParticleSnapshot.MAX_PARTICLES)return counts+" (particle budget reached)";
                    String name=entry.getKey().getClass().getName()+"/"+particle.getClass().getName();
                    if(counts.containsKey(name)||counts.size()<16)counts.merge(name,1,Integer::sum);
                    else counts.merge("other",1,Integer::sum);
                }
            }
            return counts.toString();
        }catch(RuntimeException failure){return "Particle queue diagnostics unavailable: "+failure.getClass().getSimpleName();}
    }

    private static final class Context {
        final DirectMeshCapture capture;
        final Map<Object,List<DirectMeshCapture.Draw>> masks=new IdentityHashMap<>();
        final Map<Object,List<Integer>> resetOffsets=new IdentityHashMap<>();
        final Set<Object> owned=java.util.Collections.newSetFromMap(new IdentityHashMap<>());
        final int[] lightmap=new int[256];
        Object current;boolean currentReset,savedDepth;int starts;
        Context(DirectMeshCapture capture,MinecraftClient client){
            this.capture=capture;
            var image=((LightmapTextureAccessor)client.gameRenderer.getLightmapTextureManager()).goldcraft$image();
            if(image==null||image.getWidth()!=16||image.getHeight()!=16)throw new IllegalStateException("Unexpected VIX lightmap size");
            for(int y=0;y<16;y++)for(int x=0;x<16;x++)lightmap[y*16+x]=image.getColor(x,y);
        }
        void start(Object pipeline)throws ReflectiveOperationException {
            int kind=spec(pipeline);
            if(current!=null)throw new IllegalStateException("Nested VIX mask target");
            if(api.buffer.get(pipeline)!=null)throw new IllegalStateException("VIX target still owned by another frame");
            current=pipeline;currentReset=kind==PostEffectPlan.BLOOM;owned.add(pipeline);starts++;
            masks.computeIfAbsent(pipeline,ignored->new ArrayList<>());
            resetOffsets.computeIfAbsent(pipeline,ignored->new ArrayList<>());
            capture.source("vix:"+api.name.get(pipeline));
            if(!api.started.getBoolean(pipeline)) {
                api.started.setBoolean(pipeline,true);api.queue.add(pipeline);
                Animation animation=api.animations.get(pipeline.getClass());
                if(animation!=null&&!animation.started.getBoolean(pipeline)) {
                    animation.time.setFloat(pipeline,0);animation.progress.setFloat(pipeline,0);animation.started.setBoolean(pipeline,true);
                }
            }
        }
        void suspend(Object pipeline) {
            if(current!=pipeline)throw new IllegalStateException("Mismatched VIX mask target");
            var next=capture.takePostDraws();var mask=masks.get(pipeline);
            // Base Pipeline.start copies scene depth on every render-type begin;
            // its color target still accumulates across these boundaries.
            if(currentReset&&!next.isEmpty())resetOffsets.get(pipeline).add(mask.size());
            mask.addAll(next);current=null;currentReset=false;
        }
        Frame drain(MinecraftClient client,int count)throws ReflectiveOperationException {
            // Use the installed PriorityQueue itself, including its unspecified tie behavior.
            Object pipeline;
            while((pipeline=api.queue.poll())!=null){owned.add(pipeline);api.priorityQueue.add(pipeline);}
            List<PostEffectPlan.Stage> stages=new ArrayList<>();List<DirectMeshCapture.Draw> draws=new ArrayList<>();
            List<Integer> depthResets=new ArrayList<>();
            while((pipeline=api.priorityQueue.poll())!=null) {
                int kind=spec(pipeline);boolean screen=PostEffectPlan.maskless(kind);
                if(!screen&&!api.called.getBoolean(pipeline))continue;
                var mask=masks.getOrDefault(pipeline,List.of());
                if(kind==PostEffectPlan.BW_CONTRAST) {
                    if(!mask.isEmpty())throw new IllegalStateException("Maskless impact owns geometry");
                    var impact=api.impact.capture(pipeline,new ImpactUniforms(client));
                    if(impact==null)continue;
                    if(stages.size()>=PostEffectPlan.MAX_STAGES)throw new IllegalArgumentException("VIX stage budget");
                    stages.add(PostEffectPlan.Stage.impact(api.priority.getInt(pipeline),draws.size(),impact));
                    continue;
                }
                // VIX removes at age > lifetime, leaving the final interpolated tick queued.
                // NightFall's intensity takes a square root outside its domain there. End only this
                // expired pass before it creates NaN; retain every other pass in the frame.
                if(kind==PostEffectPlan.SCREEN_DISTORTION) {
                    Object effect=api.screenEffect.get(pipeline);
                    float normalizedAge=((Number)invoke(api.screenNormalizedAge,effect)).floatValue();
                    if(expiredScreenDistortion(api.screenAge.getInt(effect),api.screenLifetime.getInt(effect),normalizedAge)) {
                        expiredScreenDistortions++;
                        continue;
                    }
                }
                float[] p=parameters(client,pipeline,kind);
                // A fully clipped mask is an identity pass. Keep producer animation advancement.
                if(!screen&&mask.isEmpty())continue;
                if(stages.size()>=PostEffectPlan.MAX_STAGES)throw new IllegalArgumentException("VIX stage budget");
                int first=draws.size();draws.addAll(mask);
                for(int offset:resetOffsets.getOrDefault(pipeline,List.of()))depthResets.add(first+offset);
                stages.add(new PostEffectPlan.Stage(kind,api.priority.getInt(pipeline),first,mask.size(),
                    p[0],p[1],p[2],p[3],p[4],p[5],p[6],p[7]));
            }
            return new Frame(List.copyOf(stages),List.copyOf(draws),List.copyOf(depthResets),draws.isEmpty()?0:Math.max(1,count));
        }
        void cleanup() {
            var cleanup=new CaptureCleanup();
            Object pipeline;
            while((pipeline=api.queue.poll())!=null)owned.add(pipeline);
            while((pipeline=api.priorityQueue.poll())!=null)owned.add(pipeline);
            for(Object value:owned)cleanup.run(()->{
                try{api.called.setBoolean(value,false);api.started.setBoolean(value,false);}
                catch(IllegalAccessException failure){throw failed(failure);}
            });
            // All buffers remained null and depthShared stayed false; close only resets
            // VIX texture bookkeeping/active state, never a hidden scene render target.
            cleanup.run(()->invoke(api.close,null));cleanup.rethrow();
        }
    }
    static boolean expiredScreenDistortion(int age,int lifetime,float normalizedAge) {
        return age>=0&&lifetime>0&&Float.isFinite(normalizedAge)&&normalizedAge>1;
    }
    private record Animation(Field started,Field time,Field progress,Method update){}
    private static final class ImpactUniforms implements BlackWhiteImpactCapture.Uniforms {
        private final MinecraftClient client;
        private JsonEffectShaderProgram program,blit;
        ImpactUniforms(MinecraftClient client){this.client=client;}
        private JsonEffectShaderProgram program() {
            try {if(program==null)program=api.effect("black_white_contrast","black_white_simple");return program;}
            catch(ReflectiveOperationException failure){throw failed(failure);}
        }
        public float get(String name,int component,int size){return uniform(program(),name,component,size);}
        public int integer(String name) {
            var value=program().getUniformByName(name);
            if(value==null||value.getCount()!=1||value.getIntData()==null)
                throw new IllegalStateException("Unexpected VIX integer uniform layout "+name);
            return value.getIntData().get(0);
        }
        public float clear(int component) {
            float[] value=((FramebufferClearColorAccessor)client.getFramebuffer()).goldcraft$clearColor();
            if(value==null||value.length!=4)throw new IllegalStateException("Unexpected main target clear color");
            return value[component];
        }
        public float blit(int component) {
            try{if(blit==null)blit=api.effect("blit");return uniform(blit,"ColorModulate",component,4);}
            catch(ReflectiveOperationException failure){throw failed(failure);}
        }
        public void set(String name,float value){program().getUniformByNameOrDummy(name).set(value);}
        public void set(String name,float x,float y){program().getUniformByNameOrDummy(name).set(x,y);}
    }
    private static float[] parameters(MinecraftClient client,Object pipeline,int kind)throws ReflectiveOperationException {
        float[] result=new float[8];
        if(kind==PostEffectPlan.BLOOM) {
            var effect=api.effect("unity_composite");
            result[3]=uniform(effect,"BloomIntensive");result[4]=uniform(effect,"BloomBase");
            result[6]=uniform(effect,"BloomThresholdUp");result[7]=uniform(effect,"BloomThresholdDown");
        }else if(kind==PostEffectPlan.SPACE_BROKEN) {
            result[3]=uniform(api.effect("space_broken"),"InterScale");
        }else if(kind==PostEffectPlan.AIR||kind==PostEffectPlan.CHROMATIC) {
            Animation animation=api.animations.get(pipeline.getClass());invoke(animation.update,pipeline);
            result[0]=animation.time.getFloat(pipeline);result[1]=1;
            if(kind==PostEffectPlan.AIR) {
                result[2]=.3f;
                if(client.player!=null){double yaw=Math.toRadians(client.player.getYaw());result[1]=(float)Math.cos(yaw);result[2]=(float)Math.sin(yaw)*.5f;}
            }
            var effect=api.effect(kind==PostEffectPlan.AIR?"air_disturbance":"chromatic_aberration");
            // Installed process writes undeclared Strength/Progress/BladeLength uniforms.
            // safeGetUniform is a no-op for them: preserve the real DistortionStrength.
            if(effect.getUniformByName("Strength")!=null||effect.getUniformByName("Progress")!=null||effect.getUniformByName("BladeLength")!=null)
                throw new IllegalStateException("Unverified VIX disturbance uniform schema");
            result[3]=uniform(effect,"DistortionStrength");
            // Mirror only the effective writes made by the real handler; no GL uniform query/upload.
            effect.getUniformByNameOrDummy("Time").set(result[0]);
            effect.getUniformByNameOrDummy("Direction").set(result[1],result[2]);
        }else if(kind==PostEffectPlan.COLOR_DISPERSION) {
            Object effect=api.screenEffect.get(pipeline);
            var program=api.effect("color_dispersion");
            ColorDispersionParameters.write(result,api.screenAge.getInt(effect),api.screenLifetime.getInt(effect),
                api.dispersionType.get(effect)==api.dispersionPost,uniform(program,"Center",0,2),uniform(program,"Center",1,2));
            // Mirror the handler's effective uniform writes; Center is retained.
            program.getUniformByNameOrDummy("Level").set(result[0],result[1],result[2]);
            program.getUniformByNameOrDummy("ColorModulate").set(result[5],result[6],result[7]);
        }else {
            Object effect=api.screenEffect.get(pipeline);
            float progress=((Number)invoke(api.screenProgress,pipeline)).floatValue();
            float intensity=((Number)invoke(api.screenIntensity,pipeline,progress)).floatValue();
            result[0]=api.screenAge.getInt(effect)*.05f*api.screenSpeed.getFloat(effect);
            result[3]=api.screenStrength.getFloat(effect)*intensity;
            result[4]=api.screenFrequency.getFloat(effect);result[5]=progress;
            var program=api.effect("screen_distortion");
            program.getUniformByNameOrDummy("Time").set(result[0]);program.getUniformByNameOrDummy("DistortionStrength").set(result[3]);
            program.getUniformByNameOrDummy("Frequency").set(result[4]);program.getUniformByNameOrDummy("Progress").set(result[5]);
        }
        return result;
    }
    private static float uniform(JsonEffectShaderProgram program,String name){return uniform(program,name,0,1);}
    private static float uniform(JsonEffectShaderProgram program,String name,int component,int size) {
        var uniform=program.getUniformByName(name);
        if(uniform==null||uniform.getFloatData()==null||uniform.getCount()!=size)
            throw new IllegalStateException("Unexpected VIX uniform layout "+name);
        return uniform.getFloatData().get(component);
    }
    private static int spec(Object pipeline)throws ReflectiveOperationException {
        String name=String.valueOf(api.name.get(pipeline)),type=pipeline.getClass().getName();
        return switch(name) {
            case "efn:bloom_particle" -> checked(type,NF+"BloomParticleRenderType$Pipeline",PostEffectPlan.BLOOM);
            case "efn:mesh_space_broken_0","efn:mesh_space_broken_1" -> checked(type,NF+"SpaceBrokenRenderType$Pipeline",PostEffectPlan.SPACE_BROKEN);
            case "efn:space_broken_0","efn:space_broken_1" -> checked(type,NF+"MeshSpaceBrokenRenderType$Pipeline",PostEffectPlan.SPACE_BROKEN);
            case "efn:chromatic_aberration_o" -> checked(type,NF+"ChromaticAberrationRenderType$Pipeline",PostEffectPlan.CHROMATIC);
            case "efn:air_disturbance" -> checked(type,NF+"AirDisturbanceRenderType$Pipeline",PostEffectPlan.AIR);
            case "efn:screen_distortion" -> checked(type,SCREEN,PostEffectPlan.SCREEN_DISTORTION);
            case "efn:color_dispersion" -> checked(type,DISPERSION+"$CD_Pipeline",PostEffectPlan.COLOR_DISPERSION);
            case "efn:bw_contrast" -> checked(type,BlackWhiteImpactCapture.EFFECT+"$BWC_Pipeline",PostEffectPlan.BW_CONTRAST);
            default -> throw new IllegalStateException("Unsupported VIX pipeline "+name+" ("+type+")");
        };
    }
    private static int checked(String actual,String expected,int kind){if(!actual.equals(expected))throw new IllegalStateException("Unverified VIX pipeline class "+actual);return kind;}

    private static final class Api {
        final Class<?> type=Class.forName(TYPE),pipelines=Class.forName(ROOT),pipeline=Class.forName(ROOT+"$Pipeline");
        final Field name=field(pipeline,"name"),priority=field(pipeline,"priority"),started=field(pipeline,"started"),called=field(pipeline,"called"),buffer=field(pipeline,"bufferTarget");
        final Field activeFlag=field(pipelines,"Active"),shared=field(pipelines,"depthShared");
        final Method getPipeline=type.getMethod("getPipeline"),activate=pipelines.getMethod("active"),close=pipelines.getMethod("close");
        final Method screenRender=Class.forName("com.guhao.vix.client.event.ScreenEffectEngine").getMethod("OnRender",RenderLevelStageEvent.class);
        final Queue<Object> queue=queue(pipelines,"PostEffectQueue"),priorityQueue=queue(pipelines,"PostEffectQueueInternal");
        final Map<Class<?>,Animation> animations=new IdentityHashMap<>();
        final Class<?> passes=Class.forName("com.hm.efn.registries.PostPasses");
        final Field passEffect=field(Class.forName("com.guhao.vix.client.shaderpasses.PostPassBase"),"effect");
        final Class<?> screenPipeline=Class.forName(SCREEN),screen=Class.forName("com.hm.efn.client.screeneffect.ScreenDistortionEffect");
        final Field screenEffect=field(Class.forName("com.guhao.vix.client.screeneffect.ScreenEffectBase$SE_Pipeline"),"effect");
        final Field screenAge=field(Class.forName("com.guhao.vix.client.screeneffect.ScreenEffectBase"),"age"),screenStrength=field(screen,"maxDistortion"),screenSpeed=field(screen,"distortionSpeed"),screenFrequency=field(screen,"frequency");
        final Field screenLifetime=field(Class.forName("com.guhao.vix.client.screeneffect.ScreenEffectBase"),"lifetime");
        final Method screenNormalizedAge=method(Class.forName("com.guhao.vix.client.screeneffect.ScreenEffectBase"),"getNormalizedAgeWithPartialTicks");
        final Field dispersionType=field(Class.forName(DISPERSION),"type");
        final Object dispersionPost=Class.forName(DISPERSION+"$Type").getField("POST").get(null);
        final Method screenProgress=method(screenPipeline,"getDistortionProgress"),screenIntensity=method(screenPipeline,"getDistortionIntensity",float.class);
        final BlackWhiteImpactCapture impact=new BlackWhiteImpactCapture();
        Api()throws ReflectiveOperationException {
            for(String name:List.of("AirDisturbanceRenderType$Pipeline","ChromaticAberrationRenderType$Pipeline")) {
                Class<?> type=Class.forName(NF+name);
                animations.put(type,new Animation(field(type,"animationStarted"),field(type,"currentTime"),field(type,"progress"),method(type,"updateAnimation")));
            }
        }
        Object pipeline(ParticleTextureSheet sheet)throws ReflectiveOperationException {
            Object value=invoke(getPipeline,sheet);spec(value);
            // These supported producers inherit the audited base begin/finish; no
            // subclass is allowed to allocate or bind a target before our boundary.
            if(sheet.getClass().getMethod("beginPost",net.minecraft.client.render.Tessellator.class,net.minecraft.client.texture.TextureManager.class).getDeclaringClass()!=type
                ||sheet.getClass().getMethod("finish").getDeclaringClass()!=type)
                throw new IllegalStateException("Unverified VIX begin/finish override "+sheet.getClass().getName());
            return value;
        }
        JsonEffectShaderProgram effect(String name)throws ReflectiveOperationException {
            return effect(name,name);
        }
        JsonEffectShaderProgram effect(String name,String resource)throws ReflectiveOperationException {
            Object pass=passes.getField(name).get(null);
            if(pass==null)throw new IllegalStateException("VIX post programs have not loaded");
            Object value=passEffect.get(pass);
            if(!(value instanceof JsonEffectShaderProgram shader)||!shader.getName().equals("efn:"+resource))
                throw new IllegalStateException("Unverified VIX post program "+name);
            return shader;
        }
    }
    @SuppressWarnings("unchecked") private static Queue<Object> queue(Class<?> type,String name)throws ReflectiveOperationException{return (Queue<Object>)type.getField(name).get(null);}
    private static Field field(Class<?> type,String name)throws ReflectiveOperationException{Field field=type.getDeclaredField(name);field.setAccessible(true);return field;}
    private static Method method(Class<?> type,String name,Class<?>...params) {
        try {Method method=type.getDeclaredMethod(name,params);method.setAccessible(true);return method;}
        catch(NoSuchMethodException failure){if(type.getSuperclass()!=null)return method(type.getSuperclass(),name,params);throw failed(failure);}
    }
    private static Object invoke(Method method,Object receiver,Object...args) {
        try{return method.invoke(receiver,args);}catch(InvocationTargetException failure){throw failed(failure.getCause());}
        catch(ReflectiveOperationException failure){throw failed(failure);}
    }
    private static RuntimeException failed(Throwable failure){if(failure instanceof Error fatal)throw fatal;return failure instanceof RuntimeException runtime?runtime:new IllegalStateException("VIX capture API failed",failure);}
    private static void resolve() {
        if(resolved)return;resolved=true;
        var mods=ModList.get();if(!mods.isLoaded("vix")&&!mods.isLoaded("efn"))return;
        try {
            verifyMod("efn","3.4.0","ec66e0b42a0f8561ebbf57af356dfd237be170297cc5e5e712de0bec34a7fa4c");
            verifyMod("vix","1.0.9","0d6d3aee5030abcae0247ef2d7ccc8267a2a56513d0009941a107ed36f4f6aca");
            api=new Api();
        }catch(ReflectiveOperationException|IOException|RuntimeException|LinkageError failure){report(failure);api=null;}
    }
    private static void verifyMod(String id,String version,String hash)throws IOException {
        var mods=ModList.get();
        var info=mods.getModContainerById(id).orElseThrow(()->new IllegalStateException("Missing VIX dependency "+id)).getModInfo();
        if(!info.getVersion().toString().equals(version))throw new IllegalStateException("Unverified "+id+" version "+info.getVersion());
        try(var input=Files.newInputStream(info.getOwningFile().getFile().getFilePath())){
            if(!sha(input.readAllBytes()).equals(hash))throw new IllegalStateException("Unverified "+id+" artifact hash");
        }
    }
    private static void verifyResources(MinecraftClient client)throws IOException {
        if(resourcesVerified)return;
        for(var entry:RESOURCE_HASHES.entrySet()) {
            var id=entry.getKey().contains(":")?Identifier.of(entry.getKey()):Identifier.of("efn","shaders/program/"+entry.getKey());
            var resource=client.getResourceManager().getResource(id).orElseThrow(()->new IOException("Missing VIX resource "+id));
            try(var input=resource.getInputStream()) {
                if(!sha(input.readAllBytes()).equals(entry.getValue()))throw new IOException("Unverified VIX resource override "+id);
            }
        }
        resourcesVerified=true;
    }
    private static String sha(byte[] bytes){try{return HexFormat.of().formatHex(MessageDigest.getInstance("SHA-256").digest(bytes));}catch(java.security.NoSuchAlgorithmException impossible){throw new AssertionError(impossible);}}
    private static void report(Throwable failure){
        error=failure.toString();
        long now=System.nanoTime();
        // Coordinates vary every failed frame. Retain exact values in status,
        // without making one unsupported draw flood the render-thread log.
        if(lastFailureLog==0||now-lastFailureLog>=5_000_000_000L){
            lastFailureLog=now;GoldCraft.LOGGER.warn("VIX post capture unavailable; clearing post frame",failure);
        }
    }
}
