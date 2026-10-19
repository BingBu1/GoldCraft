package dev.goldcraft.bridge;

import java.util.List;

/** An atomic typed post sequence and its own mesh; ordinary particles are never mask inputs. */
public final class PostEffectPlan {
    public static final int BLOOM=1,SPACE_BROKEN=2,CHROMATIC=3,AIR=4,SCREEN_DISTORTION=5,COLOR_DISPERSION=6,BW_CONTRAST=7,HSV_FILTER=8;
    public static final int MAX_STAGES=32;
    /** Bloom uses strength=intensive, frequency=base, extra0=upper, extra1=lower; space uses strength=InterScale.
     * Color dispersion's eight floats are Level RGB, Center XY, then ColorModulate RGB. */
    public record Stage(int kind,int priority,int firstBatch,int batchCount,float time,float directionX,float directionY,
                        float strength,float frequency,float progress,float extra0,float extra1,Impact impact) {
        public Stage(int kind,int priority,int firstBatch,int batchCount,float time,float directionX,float directionY,
                     float strength,float frequency,float progress,float extra0,float extra1) {
            this(kind,priority,firstBatch,batchCount,time,directionX,directionY,strength,frequency,progress,extra0,extra1,null);
        }
        public static Stage impact(int priority,int firstBatch,Impact impact) {
            return new Stage(BW_CONTRAST,priority,firstBatch,0,0,0,0,0,0,0,0,0,impact);
        }
        public static Stage impact(int priority,Impact impact) {
            return impact(priority,0,impact);
        }
        /** Hue_W XY, Alpha, pre-blit ColorModulate RGBA and one reserved zero. */
        public static Stage hsv(int priority,int firstBatch,float hue,float width,float alpha,
                                float blitR,float blitG,float blitB,float blitA) {
            return new Stage(HSV_FILTER,priority,firstBatch,0,hue,width,alpha,blitR,blitG,blitB,blitA,0);
        }
        public Stage {
            if(kind<BLOOM||kind>HSV_FILTER||priority<-32768||priority>32767||firstBatch<0||batchCount<0
                ||firstBatch>ParticleSnapshot.MAX_BATCHES||batchCount>ParticleSnapshot.MAX_BATCHES-firstBatch
                ||(maskless(kind)?batchCount!=0:batchCount==0)
                ||(kind==BW_CONTRAST?(impact==null||time!=0||directionX!=0||directionY!=0
                    ||strength!=0||frequency!=0||progress!=0||extra0!=0||extra1!=0):impact!=null)
                ||(kind==COLOR_DISPERSION&&(time<=0||directionX<=0||directionY<=0))
                ||(kind==HSV_FILTER&&extra1!=0)
                ||!finite(time)||!finite(directionX)||!finite(directionY)||!finite(strength)
                ||!finite(frequency)||!finite(progress)||!finite(extra0)||!finite(extra1))
                throw new IllegalArgumentException("Post-effect stage bounds: kind="+kind+" priority="+priority
                    +" firstBatch="+firstBatch+" batchCount="+batchCount+" parameters=["+time+","+directionX+","+directionY
                    +","+strength+","+frequency+","+progress+","+extra0+","+extra1+"] impact="+(impact!=null));
        }
    }
    /** Effective BlackWhiteContrastSimple uniforms, in their distinct kind7 wire order. */
    public record Impact(float contrast,float brightness,float time,float intensity,float speed,float mode,
                         float impactThreshold,float impactThresholdLerp,float focalU,float focalV,
                         float chromaticStrength,float lensDistortStrength,float darkR,float darkG,float darkB,
                         float lightR,float lightG,float lightB,float invertFactor,float cappedGrayscale,
                         float radialStrength,int samples,float clearR,float clearG,float clearB,float clearA,
                         float blitR,float blitG,float blitB,float blitA) {
        public Impact {
            if(!finite(contrast)||!finite(brightness)||!unit(time)||!finite(intensity)||!finite(speed)||!finite(mode)
                ||!finite(impactThreshold)||!finite(impactThresholdLerp)||!unit(focalU)||!unit(focalV)
                ||!finite(chromaticStrength)||!finite(lensDistortStrength)||!finite(darkR)||!finite(darkG)||!finite(darkB)
                ||!finite(lightR)||!finite(lightG)||!finite(lightB)||!finite(invertFactor)||!finite(cappedGrayscale)
                ||!finite(radialStrength)||samples < -1000000||samples > 1000000
                ||!finite(clearR)||!finite(clearG)||!finite(clearB)||!finite(clearA)
                ||!finite(blitR)||!finite(blitG)||!finite(blitB)||!finite(blitA))
                throw new IllegalArgumentException("Black-white impact bounds");
        }
        void write(Wire.Writer out) {
            out.f32(contrast).f32(brightness).f32(time).f32(intensity).f32(speed).f32(mode)
                .f32(impactThreshold).f32(impactThresholdLerp).f32(focalU).f32(focalV)
                .f32(chromaticStrength).f32(lensDistortStrength).f32(darkR).f32(darkG).f32(darkB)
                .f32(lightR).f32(lightG).f32(lightB).f32(invertFactor).f32(cappedGrayscale)
                .f32(radialStrength).f32(samples).f32(clearR).f32(clearG).f32(clearB).f32(clearA)
                .f32(blitR).f32(blitG).f32(blitB).f32(blitA);
        }
    }
    private static boolean finite(float value){return Float.isFinite(value)&&Math.abs(value)<=1000000f;}
    private static boolean unit(float value){return Float.isFinite(value)&&value>=0&&value<=1;}
    public static boolean maskless(int kind){return kind==SCREEN_DISTORTION||kind==COLOR_DISPERSION||kind==BW_CONTRAST||kind==HSV_FILTER;}
    private PostEffectPlan() {}
    public static byte[] encode(long epoch,long generation,long revision,int life,int count,List<Stage> stages,
                                List<Integer> depthResets,List<ParticleSnapshot.Batch> batches) {
        if(stages.size()>MAX_STAGES||batches.size()>ParticleSnapshot.MAX_BATCHES||depthResets.size()>ParticleSnapshot.MAX_BATCHES)
            throw new IllegalArgumentException("Post-effect plan budget");
        boolean[] covered=new boolean[batches.size()];
        boolean[] bloom=new boolean[batches.size()],resets=new boolean[batches.size()];
        for(Stage stage:stages) {
            if(stage.firstBatch()>batches.size()||stage.batchCount()>batches.size()-stage.firstBatch())
                throw new IllegalArgumentException("Post-effect range exceeds mesh");
            for(int i=stage.firstBatch();i<stage.firstBatch()+stage.batchCount();i++) {
                if(covered[i])throw new IllegalArgumentException("Overlapping post-effect ranges");
                covered[i]=true;
                bloom[i]=stage.kind()==BLOOM;
            }
        }
        for(boolean present:covered)if(!present)throw new IllegalArgumentException("Unowned post-effect batch");
        for(int index:depthResets) {
            if(index<0||index>=batches.size()||resets[index]||!bloom[index])
                throw new IllegalArgumentException("Invalid VIX mask depth-reset boundary");
            resets[index]=true;
        }
        byte[] mesh=ParticleSnapshot.encode(epoch,generation,revision,life,count,batches);
        Wire.Writer out=new Wire.Writer().u8(stages.size());
        for(Stage stage:stages) {
            out.u8(stage.kind()).i32(stage.priority()).u8(stage.firstBatch()).u8(stage.batchCount());
            if(stage.kind()==BW_CONTRAST)stage.impact().write(out);
            else out.f32(stage.time()).f32(stage.directionX()).f32(stage.directionY()).f32(stage.strength())
                .f32(stage.frequency()).f32(stage.progress()).f32(stage.extra0()).f32(stage.extra1());
        }
        out.u8(depthResets.size());for(int index:depthResets)out.u8(index);
        return out.i32(mesh.length).bytes(mesh).toByteArray();
    }
}
