import java.nio.file.*;
import java.util.*;
import net.fabricmc.tinyremapper.*;
import net.fabricmc.tinyremapper.extension.mixin.MixinExtension;
import org.objectweb.asm.*;
import org.objectweb.asm.tree.*;

/** Uses the pinned real TinyRemapper Mixin extension and workspace mapping table. */
public final class RemapFramebuffer {
    public static void main(String[] args)throws Exception {
        var remapper=TinyRemapper.newRemapper().threads(1)
            .withMappings(TinyUtils.createTinyMappingProvider(Path.of(args[0]),"named","mojang"))
            .extension(new MixinExtension()).build();
        try {
            remapper.readClassPath(Path.of(args[2]));remapper.readInputs(Path.of(args[1]));
            remapper.apply((name,bytes)->{
                try{Path out=Path.of(args[3],name+".class");Files.createDirectories(out.getParent());Files.write(out,bytes);}
                catch(java.io.IOException e){throw new java.io.UncheckedIOException(e);}
            });
        }finally{remapper.finish();}
        var node=new ClassNode();new ClassReader(Files.readAllBytes(Path.of(args[3],"dev/goldcraft/client/mixin/FramebufferClearColorAccessor.class"))).accept(node,0);
        var annotations=new ArrayList<AnnotationNode>();if(node.visibleAnnotations!=null)annotations.addAll(node.visibleAnnotations);
        if(node.invisibleAnnotations!=null)annotations.addAll(node.invisibleAnnotations);
        var mixin=annotations.stream().filter(a->a.desc.equals("Lorg/spongepowered/asm/mixin/Mixin;")).findFirst().orElseThrow();
        if(!mixin.values.toString().contains("Lcom/mojang/blaze3d/pipeline/RenderTarget;"))throw new AssertionError("Remapped target mismatch "+mixin.values);
        var method=node.methods.stream().filter(m->m.name.equals("goldcraft$clearColor")).findFirst().orElseThrow();
        var accessor=method.visibleAnnotations.stream().filter(a->a.desc.endsWith("/Accessor;")).findFirst().orElseThrow();
        if(!accessor.values.equals(List.of("value","clearChannels")))throw new AssertionError("Remapped field mismatch "+accessor.values);
        System.out.println("PASS actual TinyRemapper Mixin extension named Framebuffer.clearColor -> Mojang RenderTarget.clearChannels");
    }
}
