package dev.goldcraft.bridge;

import java.util.List;

/** An atomic typed post sequence and its own mesh; ordinary particles are never mask inputs. */
public final class PostEffectPlan {
    public static final int BLOOM=1,SPACE_BROKEN=2,CHROMATIC=3,AIR=4,SCREEN_DISTORTION=5,COLOR_DISPERSION=6;
    public static final int MAX_STAGES=32;
    /** Bloom uses strength=intensive, frequency=base, extra0=upper, extra1=lower; space uses strength=InterScale.
     * Color dispersion's eight floats are Level RGB, Center XY, then ColorModulate RGB. */
    public record Stage(int kind,int priority,int firstBatch,int batchCount,float time,float directionX,float directionY,
                        float strength,float frequency,float progress,float extra0,float extra1) {
        public Stage {
            if(kind<BLOOM||kind>COLOR_DISPERSION||priority<-32768||priority>32767||firstBatch<0||batchCount<0
                ||firstBatch>ParticleSnapshot.MAX_BATCHES||batchCount>ParticleSnapshot.MAX_BATCHES-firstBatch
                ||(maskless(kind)?batchCount!=0:batchCount==0)
                ||(kind==COLOR_DISPERSION&&(time<=0||directionX<=0||directionY<=0))
                ||!finite(time)||!finite(directionX)||!finite(directionY)||!finite(strength)
                ||!finite(frequency)||!finite(progress)||!finite(extra0)||!finite(extra1))
                throw new IllegalArgumentException("Post-effect stage bounds");
        }
        private static boolean finite(float value){return Float.isFinite(value)&&Math.abs(value)<=1000000f;}
    }
    public static boolean maskless(int kind){return kind==SCREEN_DISTORTION||kind==COLOR_DISPERSION;}
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
            out.u8(stage.kind()).i32(stage.priority()).u8(stage.firstBatch()).u8(stage.batchCount())
                .f32(stage.time()).f32(stage.directionX()).f32(stage.directionY()).f32(stage.strength())
                .f32(stage.frequency()).f32(stage.progress()).f32(stage.extra0()).f32(stage.extra1());
        }
        out.u8(depthResets.size());for(int index:depthResets)out.u8(index);
        return out.i32(mesh.length).bytes(mesh).toByteArray();
    }
}
