package dev.goldcraft.client;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;

/** CPU equivalent of particle.vsh's texelFetch of the actual current 16x16 lightmap. */
final class PostLightmap {
    private PostLightmap(){}
    static ByteBuffer shade(ByteBuffer source,int count,EffectGeometry.Layout layout,int[] lightmap,boolean normalized) {
        if(count<0||count>65536||lightmap.length!=256||layout.stride()<1||layout.color()<0||layout.light()<0
            ||layout.color()+4>layout.stride()||layout.light()+4>layout.stride()
            ||(long)count*layout.stride()>source.remaining())throw new IllegalArgumentException("VIX lightmap layout");
        ByteBuffer input=source.slice().order(ByteOrder.nativeOrder());
        ByteBuffer output=ByteBuffer.allocate(count*layout.stride()).order(ByteOrder.nativeOrder());
        output.put(input.duplicate().limit(output.capacity())).flip();
        int firstU=-1,firstV=-1;
        for(int vertex=0;vertex<count;vertex++) {
            int base=vertex*layout.stride(),u=Short.toUnsignedInt(input.getShort(base+layout.light())),v=Short.toUnsignedInt(input.getShort(base+layout.light()+2));
            if(u>240||v>240)throw new IllegalArgumentException("VIX lightmap UV outside verified domain");
            int light;
            if(normalized) {
                // position_color_lightmap.fsh samples normalized interpolated UV2. The
                // verified producer uses one constant pair. Varying pairs require a
                // separate texture sampler and are rejected instead of approximated.
                if(vertex==0){firstU=u;firstV=v;}
                else if(u!=firstU||v!=firstV)throw new IllegalArgumentException("Varying normalized VIX lightmap UV");
                light=lightmap[(v==0?0:15)*16+(u==0?0:15)];
            }else light=lightmap[(v/16)*16+u/16];
            for(int component=0;component<4;component++) {
                int color=Byte.toUnsignedInt(input.get(base+layout.color()+component)),factor=(light>>>(component*8))&255;
                output.put(base+layout.color()+component,(byte)((color*factor+127)/255));
            }
        }
        return output;
    }
}
