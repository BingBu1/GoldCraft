package com.ysmef.geomodel.model.runtime;

import com.google.gson.*;
import com.ysmef.geomodel.ysm.script.Molang;
import java.lang.reflect.*;
import java.nio.file.*;
import java.util.*;

/** Calls the real candidate compile/eval/compose methods; no game client or server starts. */
public final class AnimatorIntegrationProbe {
    static int checks;
    static void check(boolean condition,String name) { checks++;if(!condition)throw new AssertionError(name); }
    static void close(double expected,double actual,String name) { check(Math.abs(expected-actual)<0.0002,name+" expected="+expected+" actual="+actual); }
    static Object call(Object instance,String name,Class<?>[] types,Object... args)throws Exception {
        Method method=instance.getClass().getDeclaredMethod(name,types);method.setAccessible(true);return method.invoke(instance,args);
    }
    static YSMRuntimeModel model(JsonObject data)throws Exception {
        Method compile=YSMRuntimeModel.class.getDeclaredMethod("compile",String.class,JsonObject.class);compile.setAccessible(true);
        return (YSMRuntimeModel)compile.invoke(null,"geckolib:zhiban_hanfu",data);
    }
    static void frame(YSMPlayerAnimator animator,YSMRuntimeModel.CompiledAnim anim,double time)throws Exception {
        call(animator,"resetScratch",new Class<?>[0]);
        call(animator,"evalAnim",new Class<?>[]{YSMRuntimeModel.CompiledAnim.class,double.class},anim,time);
        call(animator,"compose",new Class<?>[0]);
    }
    static BoneRotationState rotationState(YSMPlayerAnimator animator)throws Exception {
        Field field=YSMPlayerAnimator.class.getDeclaredField("boneRotations");field.setAccessible(true);return (BoneRotationState)field.get(animator);
    }
    public static void main(String[] args)throws Exception {
        net.neoforged.fml.loading.LoadingModList.of(List.of(),List.of(),List.of(),List.of(),Map.of());
        net.minecraft.SharedConstants.tryDetectVersion();
        net.minecraft.server.Bootstrap.bootStrap();
        List<String> errors=new ArrayList<>();Molang.setErrorReporter((source,error)->errors.add(source));
        JsonObject data=JsonParser.parseString(Files.readString(Path.of(args[0]))).getAsJsonObject();
        YSMRuntimeModel model=model(data);
        check(errors.isEmpty(),"all actual model scripts compiled");
        YSMPlayerAnimator animator=new YSMPlayerAnimator(model);
        YSMRuntimeModel.CompiledAnim anim=model.parallels.stream().filter(a->a.name.equals("parallel0")).findFirst().orElseThrow();
        // Execute the actual timeline and rotation channels. Locomotion input is
        // deliberately controlled; the model scripts and production evaluator are real.
        call(animator,"setQuery",new Class<?>[]{int.class,double.class},Molang.queryIdOf("query.ground_speed"),4.0);
        frame(animator,anim,0);
        close(0,Molang.compile("ysm.bone_rot('MLeftM1').x").eval(animator),"same frame query retains committed bind");
        BoneRotationState state=rotationState(animator);
        state.commit();
        double first=Molang.compile("ysm.bone_rot('MLeftM1').x").eval(animator);
        frame(animator,anim,1.0/60);state.commit();
        double second=Molang.compile("ysm.bone_rot('MLeftM1').x").eval(animator);
        frame(animator,anim,2.0/60);state.commit();
        double third=Molang.compile("ysm.bone_rot('MLeftM1').x").eval(animator);
        check(Double.isFinite(first)&&Double.isFinite(second)&&Double.isFinite(third),"real timeline finite");
        check(Math.abs(first)+Math.abs(second)+Math.abs(third)>0.001,"real timeline produces nonzero bone rotations");
        List<Double> sourceFrames=new ArrayList<>(List.of(first,second,third));
        List<Double> dependentFrames=new ArrayList<>();
        for(int i=3;i<30;i++) {
            double previous=Molang.compile("ysm.bone_rot('MLeftM1').x").eval(animator);
            frame(animator,anim,i/60.0);
            close(previous,Molang.compile("ysm.bone_rot('MLeftM1').x").eval(animator),"pending production frame is invisible "+i);
            state.commit();
            sourceFrames.add(Molang.compile("ysm.bone_rot('MLeftM1').x").eval(animator));
            dependentFrames.add(Molang.compile("ysm.bone_rot('LeftM1').x").eval(animator));
        }
        check(dependentFrames.stream().anyMatch(v->Math.abs(v)>0.0001),"real feedback timeline moves dependent LeftM1");
        int leftArm=model.boneIndex.get("LeftArm");
        check(model.bones[leftArm].mapped,"fixture left arm really mapped");
        close((float)Math.toDegrees(model.bones[leftArm].rz),Molang.compile("ysm.bone_rot('LeftArm').z").eval(animator),"mapped bone exposes its actual local bind, no EF pose invented");
        YSMPlayerAnimator other=new YSMPlayerAnimator(model);
        close(0,Molang.compile("ysm.bone_rot('MLeftM1').x").eval(other),"actual animator instances isolated");
        Molang.Env defaultEnv=(Molang.Env)call(model,"newDefaultEnv",new Class<?>[0]);
        float expected=-(float)Math.toDegrees(model.bones[model.boneIndex.get("HUDIEJIE2")].rx);
        close(expected,Molang.compile("ysm.bone_rot('HUDIEJIE2').x").eval(defaultEnv),"default visibility uses nonzero real bind");
        System.out.println("PASS production model/animator checks="+checks+" MLeftM1_frames="+sourceFrames+" LeftM1_frames="+dependentFrames+" realBindHUDIEJIE2="+expected);
    }
}
