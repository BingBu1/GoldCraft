import java.io.ByteArrayInputStream;
import java.io.IOException;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.zip.ZipFile;
import java.util.zip.ZipInputStream;
import org.objectweb.asm.ClassReader;
import org.objectweb.asm.Type;
import org.objectweb.asm.tree.*;

/** Checks release mixins and installed Mod call sites without loading game classes or opening a client. */
public final class EffectEntrypointsProbe {
    private static int checks;
    private static void check(boolean condition,String label) {
        if(!condition)throw new AssertionError(label);
        checks++;System.out.println("PASS "+label);
    }
    private static ClassNode read(byte[] bytes) {
        var result=new ClassNode();new ClassReader(bytes).accept(result,ClassReader.SKIP_DEBUG|ClassReader.SKIP_FRAMES);return result;
    }
    private static ClassNode read(ZipFile jar,String name) throws IOException {
        var entry=jar.getEntry(name+".class");
        if(entry==null)throw new AssertionError("Missing class: "+name);
        try(var stream=jar.getInputStream(entry)){return read(stream.readAllBytes());}
    }
    private static Object value(AnnotationNode annotation,String name) {
        if(annotation.values!=null)for(int i=0;i<annotation.values.size();i+=2)
            if(name.equals(annotation.values.get(i)))return annotation.values.get(i+1);
        return null;
    }
    private static List<AnnotationNode> annotations(List<AnnotationNode> first,List<AnnotationNode> second) {
        var result=new ArrayList<AnnotationNode>();if(first!=null)result.addAll(first);if(second!=null)result.addAll(second);return result;
    }
    private static List<MethodNode> methods(ClassNode owner,String selector) {
        int signature=selector.indexOf('(');String name=signature<0?selector:selector.substring(0,signature);
        return owner.methods.stream().filter(m->m.name.equals(name)&&(signature<0||m.desc.equals(selector.substring(signature)))).toList();
    }
    private static List<MethodInsnNode> calls(MethodNode method) {
        var result=new ArrayList<MethodInsnNode>();
        for(var instruction:method.instructions)if(instruction instanceof MethodInsnNode call)result.add(call);
        return result;
    }
    private static int invocation(MethodNode method,String owner,String name) {
        for(var instruction:method.instructions)
            if(instruction instanceof MethodInsnNode call&&call.owner.equals(owner)&&call.name.equals(name))
                return method.instructions.indexOf(instruction);
        return -1;
    }
    private static void hostedScope(ZipFile release) throws IOException {
        var exporter=read(release,"dev/goldcraft/client/ParticleExporter");
        var frame=methods(exporter,"frame(J)V").getFirst();
        int cpu=invocation(frame,"dev/goldcraft/client/ModMeshCapture","begin");
        int capture=invocation(frame,"dev/goldcraft/client/DirectMeshCapture","begin");
        int stage=invocation(frame,"dev/goldcraft/client/HostedEffectStages","beforeParticles");
        int render=invocation(frame,"net/minecraft/client/particle/ParticleEngine","render");
        int finish=invocation(frame,"dev/goldcraft/client/DirectMeshCapture","finish");
        check(cpu>=0&&cpu<capture&&capture<stage&&stage<render&&render<finish,
            "release snapshot producer and particles execute inside CPU/draw scope");
        long guards=frame.tryCatchBlocks.stream().filter(block->
            frame.instructions.indexOf(block.start)<=stage&&stage<frame.instructions.indexOf(block.end)).count();
        check(guards>=2,"release producer exceptions unwind nested capture resources");
        var config=release.getEntry("goldcraft.client.mixins.json");
        String text;
        try(var stream=release.getInputStream(config)){text=new String(stream.readAllBytes(),java.nio.charset.StandardCharsets.UTF_8);}
        for(String name:List.of("DirectMeshMixin","EffectLayeringMixin","EffectGlMixin","ImmediateBuffersAccessor","ParticleRenderBudgetMixin","ParticleBuffersMixin"))
            check(text.contains("\""+name+"\""),"release registers "+name);
    }
    private static void mixin(ZipFile release,ZipFile game,String simple) throws IOException {
        var mixin=read(release,"dev/goldcraft/client/mixin/"+simple);
        var annotation=annotations(mixin.visibleAnnotations,mixin.invisibleAnnotations).stream()
            .filter(a->a.desc.equals("Lorg/spongepowered/asm/mixin/Mixin;")).findFirst().orElseThrow();
        var targets=(List<?>)value(annotation,"value");
        check(targets!=null&&targets.size()==1,simple+" single release target");
        var owner=read(game,((Type)targets.getFirst()).getInternalName());
        for(var method:mixin.methods)for(var injection:annotations(method.visibleAnnotations,method.invisibleAnnotations)) {
            if(injection.desc.equals("Lorg/spongepowered/asm/mixin/gen/Accessor;")) {
                check(owner.fields.stream().anyMatch(f->f.name.equals(value(injection,"value"))
                    &&f.desc.equals(Type.getReturnType(method.desc).getDescriptor())),simple+" accessor field and return type");
                continue;
            }
            if(!injection.desc.endsWith("/Inject;")&&!injection.desc.endsWith("/Redirect;"))continue;
            var selected=new ArrayList<MethodNode>();
            for(Object selector:(List<?>)value(injection,"method")) {
                var found=methods(owner,(String)selector);
                check(!found.isEmpty(),simple+" resolves "+selector);selected.addAll(found);
            }
            Object ats=value(injection,"at");
            var positions=ats instanceof List<?> list?list:List.of(ats);
            for(Object position:positions) {
                String target=(String)value((AnnotationNode)position,"target");
                if(target==null)continue;
                check(selected.stream().flatMap(m->calls(m).stream()).anyMatch(c->target.equals("L"+c.owner+";"+c.name+c.desc)),
                    simple+" actual invocation "+target);
            }
        }
    }
    private static boolean reachesUpload(ClassNode image,MethodNode method,Set<String> visited) {
        if(!visited.add(method.name+method.desc))return false;
        for(var call:calls(method)) {
            if(call.owner.equals("com/mojang/blaze3d/platform/GlStateManager")&&call.name.equals("_texSubImage2D"))return true;
            if(call.owner.equals(image.name))for(var next:methods(image,call.name+call.desc))
                if(reachesUpload(image,next,visited))return true;
        }
        return false;
    }
    private static void optionalVixMixin(ZipFile release,Map<String,ClassNode> vix,String simple) throws IOException {
        var mixin=read(release,"dev/goldcraft/client/mixin/"+simple);
        var attributes=annotations(mixin.visibleAnnotations,mixin.invisibleAnnotations);
        check(attributes.stream().anyMatch(a->a.desc.equals("Lorg/spongepowered/asm/mixin/Pseudo;")),simple+" is optional");
        var target=attributes.stream().filter(a->a.desc.equals("Lorg/spongepowered/asm/mixin/Mixin;")).findFirst().orElseThrow();
        var names=(List<?>)value(target,"targets");
        check(names!=null&&names.size()==1,simple+" names one installed target");
        var owner=vix.get(((String)names.getFirst()).replace('.','/'));
        check(owner!=null,simple+" target exists in installed VIX");
        for(var method:mixin.methods)for(var injection:annotations(method.visibleAnnotations,method.invisibleAnnotations)) {
            if(!injection.desc.endsWith("/Inject;")&&!injection.desc.endsWith("/Redirect;"))continue;
            var selected=new ArrayList<MethodNode>();
            for(Object selector:(List<?>)value(injection,"method")) {
                var found=methods(owner,(String)selector);check(found.size()==1,simple+" exact target "+selector);selected.addAll(found);
            }
            Object ats=value(injection,"at");var positions=ats instanceof List<?> list?list:List.of(ats);
            for(Object position:positions) {
                String call=(String)value((AnnotationNode)position,"target");if(call==null)continue;
                check(selected.stream().flatMap(m->calls(m).stream()).filter(c->call.equals("L"+c.owner+";"+c.name+c.desc)).count()==1,
                    simple+" redirects the unique installed invocation "+call);
            }
        }
    }
    private static void vixCapture(ZipFile release,ZipFile nightfall,Map<String,ClassNode> vix)throws IOException {
        for(String name:List.of("VixPostParticleMixin","VixPostPipelinesMixin"))optionalVixMixin(release,vix,name);
        var pipeline=vix.get("com/guhao/vix/client/pipeline/PostEffectPipelines$Pipeline");
        for(String name:List.of("called","started"))check(pipeline.fields.stream().anyMatch(f->f.name.equals(name)&&f.desc.equals("Z")),"VIX pipeline "+name+" ownership field");
        var root=vix.get("com/guhao/vix/client/pipeline/PostEffectPipelines");
        check(root.fields.stream().anyMatch(f->f.name.equals("PostEffectQueueInternal")&&f.desc.equals("Ljava/util/PriorityQueue;")),"VIX uses the actual PriorityQueue poll order");
        var screen=vix.get("com/guhao/vix/client/event/ScreenEffectEngine");
        check(methods(screen,"OnRender(Lnet/neoforged/neoforge/client/event/RenderLevelStageEvent;)V").size()==1,"VIX screen stage callback descriptor");
        for(String name:List.of("AirDisturbanceRenderType$Pipeline","ChromaticAberrationRenderType$Pipeline")) {
            var type=read(nightfall,"com/hm/efn/client/render/custom/"+name);
            check(methods(type,"updateAnimation()V").size()==1,name+" actual CPU animation method");
            for(String field:List.of("currentTime","progress"))check(type.fields.stream().anyMatch(f->f.name.equals(field)&&f.desc.equals("F")),name+" "+field+" field");
            check(type.fields.stream().anyMatch(f->f.name.equals("animationStarted")&&f.desc.equals("Z")),name+" animation lifecycle field");
        }
        var distortion=read(nightfall,"com/hm/efn/client/screeneffect/ScreenDistortionEffect$Distortion_Pipeline");
        check(methods(distortion,"getDistortionProgress()F").size()==1,"screen distortion actual progress method");
        check(methods(distortion,"getDistortionIntensity(F)F").size()==1,"screen distortion actual intensity method");
        var dispersion=read(nightfall,"com/hm/efn/client/screeneffect/ColorDispersionEffect");
        check(dispersion.fields.stream().anyMatch(f->f.name.equals("type")&&f.desc.equals("Lcom/hm/efn/client/screeneffect/ColorDispersionEffect$Type;")),"color dispersion enum state");
        var screenBase=vix.get("com/guhao/vix/client/screeneffect/ScreenEffectBase");
        for(String field:List.of("age","lifetime"))check(screenBase.fields.stream().anyMatch(f->f.name.equals(field)&&f.desc.equals("I")),"color dispersion integer "+field);
        var cdPipeline=read(nightfall,"com/hm/efn/client/screeneffect/ColorDispersionEffect$CD_Pipeline");
        check(cdPipeline.superName.equals("com/guhao/vix/client/screeneffect/ScreenEffectBase$SE_Pipeline"),"color dispersion is maskless screen pipeline");
        check(methods(cdPipeline,"PostEffectHandler()V").stream().flatMap(m->calls(m).stream()).anyMatch(c->c.owner.equals("com/hm/efn/client/shaderpasses/ColorDispersion")&&c.name.equals("process")
            &&c.desc.equals("(Lcom/mojang/blaze3d/pipeline/RenderTarget;Lcom/mojang/blaze3d/pipeline/RenderTarget;FFFFFF)V")),"color dispersion actual six-float process descriptor");
        var cdCapture=read(release,"dev/goldcraft/client/ColorDispersionParameters");
        check(methods(cdCapture,"write([FIIZFF)V").size()==1,"release color-dispersion uniform adapter");
        var capture=read(release,"dev/goldcraft/client/VixPostCapture");
        check(methods(capture,"capture").stream().flatMap(m->calls(m).stream()).anyMatch(c->c.owner.equals("net/minecraft/client/particle/ParticleEngine")&&c.name.equals("render")
            &&c.desc.equals("(Lnet/minecraft/client/renderer/LightTexture;Lnet/minecraft/client/Camera;FLnet/minecraft/client/renderer/culling/Frustum;Ljava/util/function/Predicate;)V")),
            "release capture invokes the exact VIX-tail particle overload");
        for(String suffix:List.of("","$Context","$Api")) {
            var type=read(release,"dev/goldcraft/client/VixPostCapture"+suffix);
            check(type.methods.stream().flatMap(m->calls(m).stream()).noneMatch(c->c.owner.equals("com/mojang/blaze3d/pipeline/RenderTarget")||c.owner.equals("com/guhao/vix/client/targets/TargetManager")),
                "VIX capture"+suffix+" contains no hidden target operations");
        }
    }
    public static void main(String[] args) throws Exception {
        try(var game=new ZipFile(args[0]);var release=new ZipFile(args[1]);var maid=new ZipFile(args[2]);
            var epic=new ZipFile(args[3]);var nightfall=new ZipFile(args[4])) {
            for(String name:List.of("DirectMeshMixin","EffectLayeringMixin","RenderParametersAccessor","EffectGlMixin","ImmediateBuffersAccessor","ParticleRenderBudgetMixin","ParticleBuffersMixin","LightmapTextureAccessor"))
                mixin(release,game,name);
            hostedScope(release);
            var uploader=read(game,"com/mojang/blaze3d/vertex/BufferUploader");
            check(methods(uploader,"drawWithShader(Lcom/mojang/blaze3d/vertex/MeshData;)V").size()==1,"Minecraft 1.21.1 MeshData upload boundary");
            var graphics=read(maid,"com/github/tartaricacid/touhoulittlemaid/client/renderer/entity/chatbubble/EntityGraphics");
            check(methods(graphics,"innerBlit").stream().flatMap(m->calls(m).stream()).anyMatch(c->c.owner.equals(uploader.name)&&c.name.equals("drawWithShader")),
                "installed maid bubble reaches intercepted uploader");
            var image=read(game,"com/mojang/blaze3d/platform/NativeImage");
            for(var method:methods(image,"upload"))check(reachesUpload(image,method,new HashSet<>()),"NativeImage "+method.desc+" reaches tracked texSubImage2D");
            var gif=read(maid,"com/github/tartaricacid/touhoulittlemaid/client/renderer/texture/GifTexture");
            for(String method:List.of("doLoad","tick")) {
                var uploads=methods(gif,method).stream().flatMap(m->calls(m).stream()).filter(c->c.owner.equals(image.name)&&c.name.equals("upload")).toList();
                check(!uploads.isEmpty()&&uploads.stream().allMatch(c->methods(image,c.name+c.desc).size()==1),"GIF "+method+" uses covered NativeImage overload");
            }
            var config=read(epic,"yesman/epicfight/config/ClientConfig");
            check(config.fields.stream().anyMatch(f->f.name.equals("activateComputeShader")&&f.desc.equals("Z")),"Epic Fight CPU fallback flag identity");
            var compute=read(epic,"yesman/epicfight/client/renderer/shader/compute/ComputeShaderSetup");
            check(methods(compute,"draw").stream().flatMap(m->calls(m).stream()).anyMatch(c->c.name.equals("glDrawArrays")),"compute bypass requires scoped CPU fallback");
            var after=read(nightfall,"com/hm/efn/client/render/AfterImageManager");
            check(methods(after,"onRenderLevel(Lnet/neoforged/neoforge/client/event/RenderLevelStageEvent;)V").size()==1,"NightFall snapshot callback identity");
            check(after.fields.stream().anyMatch(f->f.name.equals("ACTIVE_TRAILS")&&f.desc.equals("Ljava/util/Map;")),"NightFall active snapshot queue identity");
            var entries=nightfall.entries();Map<String,ClassNode> vix=new HashMap<>();
            while(entries.hasMoreElements()) {
                var entry=entries.nextElement();if(!entry.getName().endsWith("vix-1.0.9.jar"))continue;
                try(var nested=new ZipInputStream(new ByteArrayInputStream(nightfall.getInputStream(entry).readAllBytes()))) {
                    for(var file=nested.getNextEntry();file!=null;file=nested.getNextEntry())
                        if(file.getName().endsWith(".class")) {
                            var type=read(nested.readAllBytes());vix.put(type.name,type);
                        }
                }
            }
            check(vix.containsKey("com/guhao/vix/client/pipeline/PostParticleRenderType"),"VIX separate post-particle type identity");
            check(vix.values().stream().filter(c->c.name.endsWith("PostPassBase")).flatMap(c->c.methods.stream()).flatMap(m->calls(m).stream())
                .anyMatch(c->c.owner.equals(uploader.name)&&c.name.equals("draw")),"VIX screen-space manual upload remains separate");
            vixCapture(release,nightfall,vix);
        }
        System.out.println("{\"checks\":"+checks+",\"releaseTargetsVerified\":true,\"runtimeMixinExecution\":false}");
    }
}
