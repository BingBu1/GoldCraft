package dev.goldcraft.client;

import net.minecraft.client.render.VertexConsumer;
import org.junit.jupiter.api.Test;
import static org.junit.jupiter.api.Assertions.*;

class HostedOutlineProviderTest {
    private static void geometry(VertexConsumer v){
        for(int i=0;i<4;i++)v.vertex(i,64+i,-i).color(255,1,2,3).texture(.1f*i,.25f).overlay(1,3).light(15,15).normal(1,2,3);
    }
    @Test void actualVanillaOutlineConsumerMatchesFixedColor()throws Exception{
        var ours=new WorldExporter.Collector(true,false);var reference=new WorldExporter.Collector(true,false);
        // RenderLayer needs the game registries and NeoForge access transforms.
        // Actual layer routing is exercised by Test-OutlineCapture separately.
        var colored=Class.forName("dev.goldcraft.client.HostedOutlineProvider$Colored")
            .getDeclaredConstructor(VertexConsumer.class,int.class);colored.setAccessible(true);
        var ctor=Class.forName("net.minecraft.client.render.OutlineVertexConsumerProvider$OutlineVertexConsumer")
            .getDeclaredConstructor(VertexConsumer.class,int.class);ctor.setAccessible(true);
        var vanilla=(VertexConsumer)ctor.newInstance(reference,0xff12b957);
        geometry((VertexConsumer)colored.newInstance(ours,0xff12b957));geometry(vanilla);
        assertArrayEquals(reference.finish(),ours.finish());assertEquals(0,ours.hurtVertices());
    }
}
