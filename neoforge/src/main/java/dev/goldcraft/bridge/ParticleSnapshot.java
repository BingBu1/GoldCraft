package dev.goldcraft.bridge;

import java.util.List;

/** Independent snapshots can replace queued frames without losing particle removals. */
public final class ParticleSnapshot {
    public static final int MAX_PARTICLES=4096,MAX_VERTICES=65536,MAX_BATCHES=128,MAX_TEXTURES=64;
    public static final int WHITE_TEXTURE=-1;
    /** Values use the fixed OpenGL enums, never process-local GL object names. */
    public record Material(int flags,int srcRgb,int dstRgb,int srcAlpha,int dstAlpha,
                           int equationRgb,int equationAlpha,int depthFunc,int colorMask,
                           float cutoff,float red,float green,float blue,float alpha) {
        public Material {
            if((flags&~511)!=0||(flags&48)==48||!factor(srcRgb,true)||!factor(dstRgb,false)
                ||!factor(srcAlpha,false)||!factor(dstAlpha,false)||!equation(equationRgb)||!equation(equationAlpha)
                ||depthFunc<512||depthFunc>519||colorMask<0||colorMask>15||!bounded(cutoff,1)
                ||!bounded(red,4)||!bounded(green,4)||!bounded(blue,4)||!bounded(alpha,4))
                throw new IllegalArgumentException("Effect material bounds");
        }
        public static Material vanilla(int flags) {
            if((flags&~1)!=0)throw new IllegalArgumentException("Legacy particle flags");
            return new Material(flags|6,770,771,1,771,32774,32774,515,15,.1f,1,1,1,1);
        }
        private static boolean bounded(float x,float limit){return Float.isFinite(x)&&x>=0&&x<=limit;}
        private static boolean factor(int x,boolean sourceRgb){return x==0||x==1||x>=768&&x<=775||sourceRgb&&x==776;}
        private static boolean equation(int x){return x==32774||x==32778||x==32779;}
        void write(Wire.Writer out) {
            out.i32(flags).i32(srcRgb).i32(dstRgb).i32(srcAlpha).i32(dstAlpha).i32(equationRgb).i32(equationAlpha)
                .i32(depthFunc).i32(colorMask).f32(cutoff).f32(red).f32(green).f32(blue).f32(alpha);
        }
    }
    public record Batch(int texture,Material material,byte[] vertices) {
        public Batch(int texture,int flags,byte[] vertices){this(texture,Material.vanilla(flags),vertices);}
    }
    private ParticleSnapshot() {}
    public static int encodedSize(long epoch,long generation,long revision,int life,int count,List<Batch> batches) {
        if(epoch==0||generation<=0||revision<=0||life==0||count<0||count>MAX_PARTICLES||batches.size()>MAX_BATCHES
            ||(count==0)!=batches.isEmpty())throw new IllegalArgumentException("Particle identity/count bounds");
        long size=36,total=0;
        for(Batch b:batches){
            if((b.texture!=WHITE_TEXTURE&&(b.texture<0||b.texture>MAX_TEXTURES))||b.material==null||b.vertices.length==0||b.vertices.length%96!=0)
                throw new IllegalArgumentException("Particle batch bounds");
            total+=b.vertices.length/24;
            if(total>MAX_VERTICES)throw new IllegalArgumentException("Particle vertex budget");
            size+=64L+b.vertices.length;
        }
        return Math.toIntExact(size);
    }
    public static byte[] encode(long epoch,long generation,long revision,int life,int count,List<Batch> batches) {
        byte[] payload=new byte[encodedSize(epoch,generation,revision,life,count,batches)];
        encodeInto(epoch,generation,revision,life,count,batches,payload,0,payload.length);
        return payload;
    }
    public static void encodeInto(long epoch,long generation,long revision,int life,int count,List<Batch> batches,
                                  byte[] destination,int offset,int length) {
        if(length!=encodedSize(epoch,generation,revision,life,count,batches))throw new IllegalArgumentException("Changed particle snapshot size");
        try(var out=Wire.Writer.bounded(destination,offset,length)){
            out.i64(epoch).i64(generation).i64(revision).i32(life).i32(count).i32(batches.size());
            for(Batch b:batches){
                out.i32(b.texture);b.material.write(out);out.i32(b.vertices.length/24).bytes(b.vertices);
            }
            out.finish();
        }
    }
}
