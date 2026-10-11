package dev.goldcraft.client;

import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Random;
import java.util.Set;
import net.minecraft.client.render.BufferBuilder;
import net.minecraft.client.render.VertexConsumers;
import net.minecraft.client.render.VertexFormat;
import net.minecraft.client.render.VertexFormats;
import net.minecraft.client.util.BufferAllocator;
import org.joml.Matrix4f;
import org.objectweb.asm.ClassReader;
import org.objectweb.asm.Opcodes;
import org.objectweb.asm.tree.*;
import org.junit.jupiter.api.Test;
import static org.junit.jupiter.api.Assertions.*;

/** Inspect the actual game definitions without initializing an untransformed game registry. */
class EntityGlintTest {
    private static ClassNode read(String name) throws IOException {
        try(var stream=EntityGlintTest.class.getResourceAsStream("/"+name+".class")) {
            assertNotNull(stream,name);
            var result=new ClassNode();new ClassReader(stream).accept(result,ClassReader.SKIP_DEBUG|ClassReader.SKIP_FRAMES);
            return result;
        }
    }
    private static MethodNode method(ClassNode owner,String name) {
        return owner.methods.stream().filter(m->m.name.equals(name)).findFirst().orElseThrow();
    }
    @Test void actualGameGlintLayersRequireEqualDepthAndSpecialBlendDespiteOpaqueSortFlag() throws IOException {
        var game=read("net/minecraft/client/render/RenderLayer");
        var definitions=new HashMap<String,List<AbstractInsnNode>>();
        var current=new ArrayList<AbstractInsnNode>();
        for(var instruction:method(game,"<clinit>").instructions) {
            current.add(instruction);
            if(instruction instanceof FieldInsnNode field&&field.getOpcode()==Opcodes.PUTSTATIC) {
                if(field.owner.equals(game.name))definitions.put(field.name,List.copyOf(current));
                current.clear();
            }
        }
        var getters=List.of("getArmorEntityGlint","getGlintTranslucent","getGlint","getEntityGlint","getDirectEntityGlint");
        for(String getter:getters) {
            var body=method(game,getter);
            String fieldName=null;
            for(var node:body.instructions)if(node instanceof FieldInsnNode field&&field.getOpcode()==Opcodes.GETSTATIC)fieldName=field.name;
            assertNotNull(fieldName,getter);
            var definition=definitions.get(fieldName);assertNotNull(definition,fieldName);
            Set<String> needed=Set.of("EQUAL_DEPTH_TEST","GLINT_TRANSPARENCY","COLOR_MASK");
            var fields=definition.stream().filter(n->n instanceof FieldInsnNode).map(n->((FieldInsnNode)n).name).toList();
            assertTrue(fields.containsAll(needed),getter+fields);
            assertEquals(getter.equals("getArmorEntityGlint"),fields.contains("VIEW_OFFSET_Z_LAYERING"),getter+fields);
            var factory=definition.stream().filter(n->n instanceof MethodInsnNode call&&call.owner.equals(game.name)&&call.name.equals("of"))
                .map(n->(MethodInsnNode)n).findFirst().orElseThrow();
            assertFalse(factory.desc.contains("Z"),"The short factory fixes both crumbling/sort booleans to false");
        }
        // Verify the selected getters remain exactly those actual layers; a
        // matching texture name alone must not reclassify ordinary materials.
        var exporter=read("dev/goldcraft/client/EntityExporter");
        var selected=new ArrayList<String>();
        for(var instruction:method(exporter,"isGlintLayer").instructions)
            if(instruction instanceof MethodInsnNode call&&call.owner.equals(game.name))selected.add(call.name);
        assertEquals(getters,selected);
    }

    @Test void armorBaseAndGlintShareTheSameViewSpaceLayeringPhase() throws IOException {
        var game=read("net/minecraft/client/render/RenderLayer");
        var base=method(game,"createArmorCutoutNoCull");
        var phases=new ArrayList<FieldInsnNode>();
        for(var instruction:base.instructions)
            if(instruction instanceof FieldInsnNode field&&field.name.endsWith("_LAYERING"))phases.add(field);
        assertEquals(1,phases.size());
        assertEquals("VIEW_OFFSET_Z_LAYERING",phases.getFirst().name);
        // Both the ordinary and equal-depth armor-decal variants use this factory.
        // The field type also validates the Mixin accessor against the matched game.
        var parameters=read("net/minecraft/client/render/RenderLayer$MultiPhaseParameters");
        var layering=parameters.fields.stream().filter(field->field.name.equals("layering")).findFirst().orElseThrow();
        assertEquals("Lnet/minecraft/client/render/RenderPhase$Layering;",layering.desc);
        var phase=read("net/minecraft/client/render/RenderPhase");
        var viewOffset=phase.fields.stream().filter(field->field.name.equals("VIEW_OFFSET_Z_LAYERING")).findFirst().orElseThrow();
        assertEquals(layering.desc,viewOffset.desc);
        assertEquals(Opcodes.ACC_PUBLIC|Opcodes.ACC_STATIC|Opcodes.ACC_FINAL,
            viewOffset.access&(Opcodes.ACC_PUBLIC|Opcodes.ACC_STATIC|Opcodes.ACC_FINAL));
    }

    @Test void actualDualConsumerPreservesIdenticalBaseAndGlintPositionsThroughExport() {
        var random=new Random(0x61_1f);
        var layout=new EffectGeometry.Layout(20,0,-1,12,-1);
        for(var mode:List.of(VertexFormat.DrawMode.QUADS,VertexFormat.DrawMode.TRIANGLES)) {
            int count=mode==VertexFormat.DrawMode.QUADS?4:3;
            for(int pose=0;pose<64;pose++) {
                // Entity renderers transform once before Dual distributes the
                // same position to the ordinary and glint vertex consumers.
                var model=pose==0?new Matrix4f():new Matrix4f()
                    .translation(random.nextFloat()*16000-8000,random.nextFloat()*16000-8000,random.nextFloat()*16000-8000)
                    .rotateXYZ(random.nextFloat()*6,random.nextFloat()*6,random.nextFloat()*6)
                    .scale(.1f+random.nextFloat()*3);
                var base=new WorldExporter.Collector(true,mode==VertexFormat.DrawMode.TRIANGLES);
                try(var allocator=new BufferAllocator(256)) {
                    var glint=new BufferBuilder(allocator,mode,VertexFormats.POSITION_TEXTURE);
                    var both=VertexConsumers.union(base,glint);
                    for(int vertex=0;vertex<count;vertex++) {
                        float x=vertex==0?-0f:Math.nextDown((float)vertex);
                        both.vertex(model,x,vertex*.137f,vertex*-.219f).texture(vertex*.25f,vertex*.3f);
                    }
                    try(var buffer=glint.end()) {
                        var parameters=buffer.getDrawParameters();
                        byte[] effect=EffectGeometry.convert(buffer.getBuffer(),count,layout,EffectGeometry.Mode.valueOf(mode.name()),
                            null,parameters.indexType().size,parameters.indexCount(),new Matrix4f(),
                            new Matrix4f().translation(.73f,-.41f,0).rotateZ(.17f).scale(8),0,0,0,false,4);
                        var baseBytes=ByteBuffer.wrap(base.finish()).order(ByteOrder.LITTLE_ENDIAN);
                        var glintBytes=ByteBuffer.wrap(effect).order(ByteOrder.LITTLE_ENDIAN);
                        assertEquals(4*28,baseBytes.remaining());assertEquals(4*24,glintBytes.remaining());
                        for(int vertex=0;vertex<4;vertex++)for(int component=0;component<3;component++)
                            assertEquals(baseBytes.getInt(vertex*28+component*4),glintBytes.getInt(vertex*24+component*4),
                                "Equal-depth position bits: "+mode+" pose="+pose+" vertex="+vertex+" component="+component);
                    }
                }
            }
        }
    }
}
