package dev.goldcraft.client;

import com.hm.efn.EFNClientConfig;
import com.hm.efn.client.screeneffect.BlackWhiteFlashEffect;
import com.hm.efn.client.screeneffect.BlackWhiteFlashEffect.ImpactMode;
import com.hm.efn.registries.PostPasses;
import dev.goldcraft.bridge.PostEffectPlan;
import java.lang.reflect.*;
import java.nio.*;
import java.util.*;
import net.minecraft.client.Minecraft;
import net.minecraft.client.renderer.EffectInstance;
import net.minecraft.world.phys.Vec3;
import probe.Clock;

public final class BwContrastRegression {
    private static int checks,cases;
    private static final String[] NAMES={"Contrast","Brightness","Time","Intensity","Speed","Mode",
        "ImpactThreshold","ImpactThresholdLerp","FocalUV","FocalUV","ChromaticStrength","LensDistortStrength",
        "ColorDark","ColorDark","ColorDark","ColorLight","ColorLight","ColorLight","InvertFactor",
        "CappedGrayscale","RadialStrength","Samples"};
    private static final int[] COMPONENT={0,0,0,0,0,0,0,0,0,1,0,0,0,1,2,0,1,2,0,0,0,0};
    private static final float[] BLIT={.8f,.9f,.7f,.6f};
    private static final Field START;
    static {
        try{START=Class.forName(BlackWhiteImpactCapture.EFFECT+"$BWC_Pipeline").getDeclaredField("startNanos");START.setAccessible(true);}
        catch(Exception e){throw new ExceptionInInitializerError(e);}
    }
    private static void check(boolean value,String detail){checks++;if(!value)throw new AssertionError(detail);}
    private static void equal(float a,float b,String detail){check(Float.floatToRawIntBits(a)==Float.floatToRawIntBits(b),detail+" actual="+a+" expected="+b);}
    private static EffectInstance uniforms(int variant) {
        var result=new EffectInstance();
        put(result,"ColorDark",.02f+variant*.01f,-.03f,.08f);put(result,"ColorLight",.95f,1.3f,.77f);
        put(result,"InvertFactor",.21f);put(result,"CappedGrayscale",.83f);put(result,"RadialStrength",-.02f);
        put(result,"Samples",variant%3==0?-9:variant%3==1?17:55);
        return result;
    }
    private static void put(EffectInstance p,String name,float...values){p.safeGetUniform(name).values=values;}
    private static final class Values implements BlackWhiteImpactCapture.Uniforms {
        final EffectInstance program;int reads,writes;
        Values(EffectInstance program){this.program=program;}
        public float get(String name,int component,int size){reads++;float[] v=program.safeGetUniform(name).values;check(v.length==size,"uniform size "+name);return v[component];}
        public int integer(String name){reads++;return (int)program.safeGetUniform(name).values[0];}
        public float clear(int component){reads++;return Minecraft.getInstance().target.clearChannels[component];}
        public float blit(int component){reads++;return BLIT[component];}
        public void set(String name,float value){writes++;program.safeGetUniform(name).set(value);}
        public void set(String name,float x,float y){writes++;program.safeGetUniform(name).set(x,y);}
    }
    private static float[] array(PostEffectPlan.Impact impact)throws Exception {
        var components=PostEffectPlan.Impact.class.getRecordComponents();var result=new float[components.length];
        for(int i=0;i<result.length;i++)result[i]=((Number)components[i].getAccessor().invoke(impact)).floatValue();
        return result;
    }
    private static BlackWhiteFlashEffect make(ImpactMode mode,int variant) {
        return new BlackWhiteFlashEffect(new Vec3(2+variant%9,-1+variant%5,8+variant%11),mode,2.25f+variant*.01f,.65f-variant*.001f);
    }
    private static void compare(BlackWhiteImpactCapture capture,BlackWhiteFlashEffect original,BlackWhiteFlashEffect candidate,
                                int variant,long now,boolean enabled,boolean render)throws Exception {
        EFNClientConfig.IMPACT_FRAME.value=enabled;original.should_render=candidate.should_render=render;
        Clock.now=now;Clock.reads=0;
        EffectInstance program=uniforms(variant);PostPasses.black_white_contrast.effect=program;
        int draws=PostPasses.black_white_contrast.draws,blits=PostPasses.blit.draws;
        original.ppl.PostEffectHandler();int reads=Clock.reads;Clock.reads=0;
        var values=new Values(uniforms(variant));
        var result=capture.capture(candidate.ppl,values);
        check(Clock.reads==reads,"same clock gate");
        check(START.getLong(original.ppl)==START.getLong(candidate.ppl),"real clock field advancement");
        boolean drew=PostPasses.black_white_contrast.draws>draws;
        check(drew==(result!=null),"same should_render/config/time gate");
        check(PostPasses.blit.draws-blits==(drew?1:0),"producer preceding blit count");
        if(result==null){check(values.reads==0&&values.writes==0,"no uniform access when skipped");cases++;return;}
        check(values.writes==11,"exact effective uniform write count");
        var actual=array(result);check(actual.length==30,"typed30");
        for(int i=0;i<22;i++)equal(actual[i],program.safeGetUniform(NAMES[i]).values[COMPONENT[i]],"exact producer uniform "+i);
        for(int i=0;i<4;i++){equal(actual[22+i],Minecraft.getInstance().target.clearChannels[i],"effective clear "+i);equal(actual[26+i],BLIT[i],"effective blit "+i);}
        for(var entry:values.program.uniforms.entrySet()) {
            var expected=program.uniforms.get(entry.getKey());check(expected!=null,"effective field exists");
            for(int i=0;i<entry.getValue().values.length;i++)equal(entry.getValue().values[i],expected.values[i],"CPU mirror "+entry.getKey());
        }
        cases++;
    }
    private static void invalid(Object[] args,String label)throws Exception {
        var components=PostEffectPlan.Impact.class.getRecordComponents();
        var types=Arrays.stream(components).map(RecordComponent::getType).toArray(Class<?>[]::new);
        boolean rejected=false;
        try{PostEffectPlan.Impact.class.getDeclaredConstructor(types).newInstance(args);}
        catch(InvocationTargetException expected){rejected=expected.getCause() instanceof IllegalArgumentException;}
        check(rejected,label);
    }
    private static void bounds()throws Exception {
        Object[] good={2.25f,.65f,.42f,.85f,1f,1f,.45f,.12f,.2f,.8f,.03f,-.25f,.02f,.03f,.08f,.95f,.96f,1f,.1f,1f,.02f,17,.1f,.2f,.3f,.4f,.8f,.9f,.7f,.6f};
        for(int i=0;i<30;i++) {
            if(i==21){for(int v:new int[]{-1000001,1000001,Integer.MIN_VALUE,Integer.MAX_VALUE}){var args=good.clone();args[i]=v;invalid(args,"samples bounds");}}
            else for(float v:new float[]{Float.NaN,Float.POSITIVE_INFINITY,Float.NEGATIVE_INFINITY,1000001,-1000001}) {
                var args=good.clone();args[i]=v;invalid(args,"finite envelope field"+i);
            }
        }
        for(int i:new int[]{2,8,9})for(float v:new float[]{-.0001f,1.0001f}){var args=good.clone();args[i]=v;invalid(args,"unit range"+i);}
        boolean rejected=false;
        try{new PostEffectPlan.Stage(7,9999,0,0,0,0,0,0,0,0,0,0);}catch(IllegalArgumentException expected){rejected=true;}
        check(rejected,"kind7 cannot use legacy constructor");
        var noMesh=List.<dev.goldcraft.bridge.ParticleSnapshot.Batch>of();
        for(int kind=5;kind<=6;kind++) {
            var stage=new PostEffectPlan.Stage(kind,9,0,0,1,2,3,4,5,6,7,8);
            byte[] encoded=PostEffectPlan.encode(123,7,13,2,0,List.of(stage),List.of(),noMesh);
            var bytes=ByteBuffer.wrap(encoded).order(ByteOrder.LITTLE_ENDIAN);check(encoded.length==81,"old8 layout length");
            check(bytes.get()==1&&bytes.get()==kind&&bytes.getInt()==9&&bytes.get()==0&&bytes.get()==0,"legacy header");
            for(int i=1;i<=8;i++)equal(bytes.getFloat(),i,"legacy exact8 order");
        }
    }
    public static void main(String[] args)throws Exception {
        var capture=new BlackWhiteImpactCapture(Clock::nanoTime);var mc=Minecraft.getInstance();
        for(ImpactMode mode:ImpactMode.values()) {
            var a=make(mode,0);var b=make(mode,0);
            check(a.ppl.priority==9999,"actual priority9999");
            check(a.lifetime==(mode==ImpactMode.LIGHT?5:11),"actual ceil(float duration*20)+1 lifetime");
            compare(capture,a,b,0,1_000_000_000L,false,true);
            check(START.getLong(a.ppl)==0,"disabled never starts clock");
            compare(capture,a,b,0,1_000_000_000L,true,false);
            check(START.getLong(a.ppl)==1_000_000_000L,"enabled starts despite should_render false");
            compare(capture,a,b,0,1_010_000_000L,true,true);
            compare(capture,a,b,0,1_020_000_000L,false,true);
            compare(capture,a,b,0,1_090_000_000L,true,true);
            compare(capture,a,b,0,2_000_000_000L,true,true);
            var tick=make(mode,0);for(int i=0;i<tick.lifetime;i++)tick.tick();check(tick.should_render,"tick retains through lifetime");
            tick.tick();check(!tick.should_render,"tick disables after lifetime");
            for(int i=0;i<240;i++) {
                mc.gameRenderer.camera.position=new Vec3(i%7,-i%3,i%11);
                mc.gameRenderer.camera.yaw=(i*47)%360-180;mc.gameRenderer.camera.pitch=(i*31)%170-85;
                mc.window.width=new int[]{1920,1080,3440,640}[i%4];mc.window.height=new int[]{1080,1920,1440,480}[i%4];
                mc.level=i%29==0?null:new net.minecraft.client.multiplayer.ClientLevel();
                var p=make(mode,i);var q=make(mode,i);long start=3_000_000_000L+i*1_000_000L;
                START.setLong(p.ppl,start);START.setLong(q.ppl,start);
                compare(capture,p,q,i,start+new long[]{0,1,25_000_000,74_999_999,149_999_999,150_000_000,250_000_000,499_999_999,500_000_000,5_000_000_000L}[i%10],true,i%13!=0);
            }
        }
        bounds();
        System.out.println("PASS cases="+cases+" checks="+checks+" exact producer handler/projection/process/constructor/tick; LIGHT+HEAVY, gates, boundaries, CPU overrides, typed wire");
    }
}
