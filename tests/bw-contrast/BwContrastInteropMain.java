import dev.goldcraft.bridge.ParticleSnapshot;
import dev.goldcraft.bridge.PostEffectPlan;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;

/** Cross-language fixture. Identity and all float literals are shared with an independent native decoder oracle. */
public final class BwContrastInteropMain {
    public static void main(String[] args)throws Exception {
        var impact=new PostEffectPlan.Impact(2.25f,.65f,.42f,.85f,1,1,.45f,.12f,.2f,.8f,.03f,-.25f,
            .02f,.03f,.08f,.95f,.96f,1,.1f,1,.02f,17,.1f,.2f,.3f,.4f,.8f,.9f,.7f,.6f);
        var stages=List.of(new PostEffectPlan.Stage(1,20,0,1,0,1,0,1.65f,0,0,1.35f,.28f),
            PostEffectPlan.Stage.impact(9999,1,impact),
            new PostEffectPlan.Stage(4,-7,1,1,.05f,1,.3f,.1f,0,0,0,0));
        var batch=new ParticleSnapshot.Batch(ParticleSnapshot.WHITE_TEXTURE,0,new byte[96]);
        byte[] bytes=PostEffectPlan.encode(123,7,13,2,2,stages,List.of(0),List.of(batch,batch));
        Files.write(Path.of(args[0]),bytes);
        System.out.println("INTEROP bytes="+bytes.length+" stages=1,7,4 epoch=123 generation=7 revision=13 life=2");
    }
}
