package dev.goldcraft.client;

import com.mojang.blaze3d.vertex.VertexConsumer;
import net.minecraft.client.renderer.RenderType;
import net.minecraft.resources.ResourceLocation;
import java.util.*;

/** Runs the shipped provider/collector against actual vanilla layers and consumer. */
public final class OutlineRoutingProbe {
    static int checks;
    static void check(boolean value){checks++;if(!value)throw new AssertionError("check "+checks);}
    static void geometry(VertexConsumer v){
        for(int i=0;i<4;i++)v.addVertex(i,64+i,-i).setColor(255,1,2,3).setUv(.1f*i,.25f)
            .setUv1(1,3).setUv2(15,15).setNormal(1,2,3);
    }
    public static void main(String[] args)throws Exception {
        net.neoforged.fml.loading.LoadingModList.of(List.of(),List.of(),List.of(),List.of(),Map.of());
        net.minecraft.SharedConstants.tryDetectVersion();net.minecraft.server.Bootstrap.bootStrap();
        var texture=ResourceLocation.withDefaultNamespace("textures/entity/steve.png");
        var regular=RenderType.entityCutoutNoCull(texture);var outline=regular.outline().orElseThrow();
        var buffers=new LinkedHashMap<RenderType,WorldExporter.Collector>();
        var provider=new HostedOutlineProvider(layer->buffers.computeIfAbsent(layer,ignored->new WorldExporter.Collector(true,false)));
        provider.color(0x12b957);var first=provider.getBuffer(regular);
        provider.color(0x7733ff);var second=provider.getBuffer(regular);
        geometry(first);geometry(second);
        var normal=buffers.get(regular).finish();var mask=buffers.get(outline).finish();
        check(normal.length==224&&mask.length==224);
        check(Byte.toUnsignedInt(normal[20])==255&&Byte.toUnsignedInt(normal[23])==3);
        check(Byte.toUnsignedInt(mask[20])==0x12&&Byte.toUnsignedInt(mask[23])==255);
        check(Byte.toUnsignedInt(mask[112+20])==0x77&&Byte.toUnsignedInt(mask[112+23])==255);
        var reference=new WorldExporter.Collector(true,false);
        var ctor=Class.forName("net.minecraft.client.renderer.OutlineBufferSource$EntityOutlineGenerator")
            .getDeclaredConstructor(VertexConsumer.class,int.class);ctor.setAccessible(true);
        geometry((VertexConsumer)ctor.newInstance(reference,0xff12b957));
        check(Arrays.equals(reference.finish(),Arrays.copyOf(mask,112)));
        var direct=new WorldExporter.Collector(true,false);var p=new HostedOutlineProvider(ignored->direct);
        p.color(0x12b957);geometry(p.getBuffer(outline));check(Arrays.equals(reference.finish(),direct.finish()));
        var emissive=RenderType.entityTranslucentEmissive(texture);check(emissive.outline().isPresent());
        var noOutline=RenderType.eyes(texture);check(noOutline.outline().isEmpty());
        int old=buffers.size();geometry(provider.getBuffer(noOutline));check(buffers.size()==old+1);
        buffers.get(noOutline).finish();
        check(buffers.get(noOutline).hurtVertices()==4&&buffers.get(outline).hurtVertices()==0);
        System.out.println("PASS actual vanilla routing + shipped collector/provider checks="+checks);
    }
}
