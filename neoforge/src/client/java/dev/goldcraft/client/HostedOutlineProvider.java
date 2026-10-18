package dev.goldcraft.client;

import net.minecraft.client.render.RenderLayer;
import net.minecraft.client.render.VertexConsumer;
import net.minecraft.client.render.VertexConsumerProvider;
import net.minecraft.client.render.VertexConsumers;

/** Outline layer routing and fixed team color, independent of normal model tint. */
final class HostedOutlineProvider implements VertexConsumerProvider {
    static final VertexConsumer DISCARD=new VertexConsumer(){
        public VertexConsumer vertex(float x,float y,float z){return this;}
        public VertexConsumer color(int r,int g,int b,int a){return this;}
        public VertexConsumer texture(float u,float v){return this;}
        public VertexConsumer overlay(int u,int v){return this;}
        public VertexConsumer light(int u,int v){return this;}
        public VertexConsumer normal(float x,float y,float z){return this;}
    };
    private final VertexConsumerProvider parent;
    private int color;
    HostedOutlineProvider(VertexConsumerProvider parent){this.parent=parent;}
    void color(int rgb){color=0xff000000|rgb;}
    @Override public VertexConsumer getBuffer(RenderLayer layer){
        RenderLayer semantic=layer instanceof RenderLayerDelegate delegate?delegate.goldcraft$delegateLayer():layer;
        if(semantic.isOutline())return new Colored(parent.getBuffer(semantic),color);
        var normal=parent.getBuffer(layer);
        var outline=semantic.getAffectedOutline();
        return outline.isPresent()?VertexConsumers.union(new Colored(parent.getBuffer(outline.get()),color),normal):normal;
    }
    private record Colored(VertexConsumer delegate,int tint) implements VertexConsumer {
        @Override public VertexConsumer vertex(float x,float y,float z){delegate.vertex(x,y,z).color(tint);return this;}
        @Override public VertexConsumer texture(float u,float v){delegate.texture(u,v);return this;}
        @Override public VertexConsumer color(int r,int g,int b,int a){return this;}
        @Override public VertexConsumer overlay(int u,int v){return this;}
        @Override public VertexConsumer light(int u,int v){return this;}
        @Override public VertexConsumer normal(float x,float y,float z){return this;}
    }
}
