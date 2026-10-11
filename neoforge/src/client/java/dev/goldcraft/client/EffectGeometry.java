package dev.goldcraft.client;

import dev.goldcraft.bridge.ParticleSnapshot;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import org.joml.Matrix4fc;
import org.joml.Vector3f;

/** CPU conversion of a completed draw, preserving its index order and winding. */
final class EffectGeometry {
    record Layout(int stride,int position,int color,int uv,int light) {}
    enum Mode {QUADS,TRIANGLES,TRIANGLE_STRIP,TRIANGLE_FAN}

    static byte[] convert(ByteBuffer source,int count,Layout layout,Mode mode,ByteBuffer indices,int indexSize,
                          Matrix4fc transform,double x,double y,double z,boolean lit,int budget) {
        if(count<0||count>ParticleSnapshot.MAX_VERTICES||layout.stride<12
            ||!field(layout.position,12,layout.stride,false)||!field(layout.color,4,layout.stride,true)
            ||!field(layout.uv,8,layout.stride,true)||!field(layout.light,4,layout.stride,true)
            ||(long)count*layout.stride>source.remaining())throw new IllegalArgumentException("Effect vertex layout/budget");
        int output=switch(mode) {
            case QUADS -> {if(count%4!=0)throw new IllegalArgumentException("Incomplete effect quad");yield count;}
            case TRIANGLES -> {if(count%3!=0)throw new IllegalArgumentException("Incomplete effect triangle");yield count/3*4;}
            case TRIANGLE_STRIP,TRIANGLE_FAN -> Math.max(0,count-2)*4;
        };
        if(output>budget||output>ParticleSnapshot.MAX_VERTICES)throw new IllegalArgumentException("Effect vertex budget");
        ByteBuffer input=source.slice().order(ByteOrder.nativeOrder()),sorted=null;
        if(indices!=null) {
            if(mode!=Mode.QUADS||(indexSize!=2&&indexSize!=4)||indices.remaining()!=count/4*6*indexSize)
                throw new IllegalArgumentException("Effect index layout");
            sorted=indices.slice().order(ByteOrder.nativeOrder());
        }
        ByteBuffer out=ByteBuffer.allocate(output*24).order(ByteOrder.LITTLE_ENDIAN);
        Vector3f point=new Vector3f();
        for(int primitive=0;primitive<output/4;primitive++) {
            int a,b,c,d;
            switch(mode) {
                case QUADS -> {
                    if(sorted==null){a=primitive*4;b=a+1;c=a+2;d=a+3;}
                    else {
                        a=index(sorted,primitive*6,indexSize);b=index(sorted,primitive*6+1,indexSize);
                        c=index(sorted,primitive*6+2,indexSize);d=index(sorted,primitive*6+4,indexSize);
                        if(index(sorted,primitive*6+3,indexSize)!=c||index(sorted,primitive*6+5,indexSize)!=a)
                            throw new IllegalArgumentException("Non-quad sorted index topology");
                    }
                }
                case TRIANGLES -> {a=primitive*3;b=a+1;c=a+2;d=c;}
                case TRIANGLE_STRIP -> {a=primitive+(primitive%2);b=primitive+1-(primitive%2);c=primitive+2;d=c;}
                case TRIANGLE_FAN -> {a=0;b=primitive+1;c=primitive+2;d=c;}
                default -> throw new AssertionError();
            }
            vertex(out,input,a,count,layout,transform,point,x,y,z,lit);
            vertex(out,input,b,count,layout,transform,point,x,y,z,lit);
            vertex(out,input,c,count,layout,transform,point,x,y,z,lit);
            vertex(out,input,d,count,layout,transform,point,x,y,z,lit);
        }
        return out.array();
    }
    private static boolean field(int offset,int bytes,int stride,boolean optional){return optional&&offset==-1||offset>=0&&offset<=stride-bytes;}
    private static int index(ByteBuffer b,int i,int size){return size==2?Short.toUnsignedInt(b.getShort(i*2)):b.getInt(i*4);}
    private static void vertex(ByteBuffer out,ByteBuffer in,int index,int count,Layout l,Matrix4fc matrix,Vector3f p,
                               double x,double y,double z,boolean lit) {
        if(index<0||index>=count)throw new IllegalArgumentException("Effect index outside vertices");
        int base=index*l.stride,at=base+l.position;
        matrix.transformPosition(in.getFloat(at),in.getFloat(at+4),in.getFloat(at+8),p);
        float wx=(float)(p.x+x),wy=(float)(p.y+y),wz=(float)(p.z+z);
        float u=l.uv<0?0:in.getFloat(base+l.uv),v=l.uv<0?0:in.getFloat(base+l.uv+4);
        if(!finite(wx,32000000)||!finite(wy,32000000)||!finite(wz,32000000)||!finite(u,16)||!finite(v,16))
            throw new IllegalArgumentException("Effect coordinate bounds");
        out.putFloat(wx).putFloat(wy).putFloat(wz).putFloat(u).putFloat(v);
        float light=1;
        if(lit&&l.light>=0)light=.55f+.45f*Math.clamp(Math.max(Short.toUnsignedInt(in.getShort(base+l.light)),
            Short.toUnsignedInt(in.getShort(base+l.light+2)))/240f,0,1);
        for(int component=0;component<4;component++) {
            int value=l.color<0?255:Byte.toUnsignedInt(in.get(base+l.color+component));
            out.put((byte)(component==3?value:Math.round(value*light)));
        }
    }
    private static boolean finite(float value,float maximum){return Float.isFinite(value)&&Math.abs(value)<=maximum;}
}
