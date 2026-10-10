package com.ysmef.geomodel.model.runtime;

import com.google.gson.*;
import com.ysmef.geomodel.ysm.script.Molang;
import java.lang.reflect.*;
import java.nio.file.*;
import java.util.*;

public final class BoneRotationProbe {
    static int checks;
    static final List<String> errors = new ArrayList<>();
    static void check(boolean condition, String name) {
        checks++;
        if (!condition) throw new AssertionError(name);
    }
    static void close(double expected, double actual, String name) {
        check(Math.abs(expected-actual)<0.0001, name+" expected="+expected+" actual="+actual);
    }
    static final class Env implements Molang.Env {
        final BoneRotationState state;
        final Map<Integer,Double> vars = new HashMap<>();
        int componentCalls;
        Env(BoneRotationState state) { this.state=state; }
        public double getVarById(int id) { return vars.getOrDefault(id,0.0); }
        public boolean hasVarById(int id) { return vars.containsKey(id); }
        public void setVarById(int id,double value) { vars.put(id,value); }
        public double getQueryById(int id) { return 0; }
        public double callStringFunction(String name,String[] args) { return name.equals("ctrl.hold") ? 1 : 0; }
        public double callStringFunctionComponent(String name,String[] args,int axis) {
            componentCalls++;
            return state.component(name,args,axis);
        }
        public double callFunction(String name,double[] args,int n) {
            return switch(name) {
                case "math.abs" -> Math.abs(args[0]);
                case "math.min" -> n<2?args[0]:Math.min(args[0],args[1]);
                case "math.max" -> Math.max(args[0],args[1]);
                case "math.pi" -> Math.PI;
                default -> throw new AssertionError("unexpected math function "+name);
            };
        }
    }
    static double eval(String code,Env env) { return Molang.compile(code).eval(env); }
    static float radians(float degrees) { return (float)Math.toRadians(degrees); }
    static void parserAndState() throws Exception {
        List<String> missing=new ArrayList<>();
        float[] angles={radians(-30),radians(45),radians(75)};
        BoneRotationState state=new BoneRotationState(Map.of("MLeftM1",0),angles,missing::add);
        Env env=new Env(state);
        close(30,eval("v.probe=ysm.bone_rot('MLeftM1').x;",env),"baseline assignment repaired");
        check(env.hasVar("v.probe")&&env.componentCalls==1,"assignment and dispatch happened");
        close(-45,eval("(ysm.bone_rot('MLeftM1')).y",env),"parenthesized result");
        close(75,eval("ysm.bone_rot('MLeftM1') . z",env),"spaced member");
        close(61,eval("2*ysm.bone_rot('MLeftM1').x+ctrl.hold('mainhand',':sword')",env),"precedence and scalar calls");
        close(45,eval("math.abs(ysm.bone_rot('MLeftM1').y)",env),"component in numeric argument");
        close(30,eval("1?ysm.bone_rot('MLeftM1').x:0",env),"component in ternary");
        close(14,eval("2+3*4",env),"ordinary arithmetic");
        close(Math.PI,eval("math.pi",env),"bare math.pi constant");
        close(Math.PI,eval("math.pi()",env),"existing math.pi call");
        close(2*Math.PI+1,eval("2*math.pi+1",env),"constant arithmetic precedence");
        close(7,eval("v.math.pi=7;v.math.pi",env),"ordinary variable path unchanged");
        close(1,eval("'left' != 'right'",env),"ordinary string comparison");
        state.stage(0,radians(-90),radians(-12),radians(9));
        close(30,eval("ysm.bone_rot('MLeftM1').x",env),"pending frame invisible");
        state.commit();
        close(90,eval("ysm.bone_rot('MLeftM1').x",env),"commit changes source");
        close(12,eval("ysm.bone_rot('MLeftM1').y",env),"commit y sign");
        close(9,eval("ysm.bone_rot('MLeftM1').z",env),"commit z sign");
        Env other=new Env(new BoneRotationState(Map.of("MLeftM1",0),angles,missing::add));
        close(30,eval("ysm.bone_rot('MLeftM1').x",other),"second animator isolation");
        close(0,eval("ysm.bone_rot('absent').x",env),"missing follows pinned null conversion");
        close(0,eval("ysm.bone_rot('absent').z",env),"repeat missing value");
        check(missing.equals(List.of("absent")),"missing source diagnosed once");
        int before=errors.size();
        eval("ysm.bone_rot('MLeftM1').w",env);
        eval("ctrl.hold('mainhand',':sword').x.y",env);
        check(errors.size()==before+2,"invalid properties diagnosed");
        // Compare all axes with the real private TLM BoneRotationStruct implementation.
        Class<?> ibone=Class.forName("com.github.tartaricacid.touhoulittlemaid.geckolib3.core.processor.IBone");
        Object bone=Proxy.newProxyInstance(ibone.getClassLoader(),new Class<?>[]{ibone},(proxy,method,args)->switch(method.getName()) {
            case "getRotationX" -> angles[0];
            case "getRotationY" -> angles[1];
            case "getRotationZ" -> angles[2];
            default -> throw new AssertionError(method.toString());
        });
        Class<?> structure=Class.forName("com.github.tartaricacid.touhoulittlemaid.client.animation.gecko.molang.functions.BoneRotation$BoneRotationStruct");
        Constructor<?> ctor=structure.getDeclaredConstructor(ibone);ctor.setAccessible(true);
        Object object=ctor.newInstance(bone);
        for(int axis=0;axis<3;axis++) {
            Method method=structure.getDeclaredMethod("get"+"XYZ".charAt(axis));method.setAccessible(true);
            close(((Number)method.invoke(object)).doubleValue(),other.state.component("ysm.bone_rot",new String[]{"MLeftM1"},axis),"exact TLM axis oracle "+axis);
        }
        System.out.println("PASS parser/state/TLM axis oracle; nonzero=[30,-45,75], committed=[90,12,9], missing="+missing);
    }
    static void walk(JsonElement node,List<String> code) {
        if(node.isJsonObject())node.getAsJsonObject().entrySet().forEach(e->walk(e.getValue(),code));
        else if(node.isJsonArray())node.getAsJsonArray().forEach(e->walk(e,code));
        else if(node.isJsonPrimitive()&&node.getAsJsonPrimitive().isString()&&node.getAsString().contains("ysm.bone_rot("))code.add(node.getAsString());
    }
    static void actualResources(Path evidence) throws Exception {
        for(String model:List.of("zhiban_hanfu","winefox_hanfu")) {
            JsonObject data=JsonParser.parseString(Files.readString(evidence.resolve(model+".runtime.json"))).getAsJsonObject();
            JsonArray bones=data.getAsJsonArray("bones");
            Map<String,Integer> indices=new HashMap<>();float[] bind=new float[bones.size()*3];
            for(int i=0;i<bones.size();i++) {
                JsonObject b=bones.get(i).getAsJsonObject();indices.put(b.get("name").getAsString(),i);
                for(int axis=0;axis<3;axis++)bind[i*3+axis]=b.getAsJsonArray("rot").get(axis).getAsFloat();
            }
            List<String> missing=new ArrayList<>();BoneRotationState state=new BoneRotationState(indices,bind,missing::add);Env env=new Env(state);
            List<String> code=new ArrayList<>();walk(data.get("animations"),code);
            int before=errors.size();
            for(String expression:code)Molang.compile(expression);
            check(errors.size()==before,"actual resource expressions compile: "+model);
            if(model.equals("zhiban_hanfu")) {
                // Real source channels, not made-up angle returns. Evaluate their literal
                // model expressions with a nonzero local script state, commit the same
                // radians the animator uses, then evaluate dependent model channels.
                JsonObject anim=data.getAsJsonObject("animations").getAsJsonObject("parallel0").getAsJsonObject("bones");
                env.setVar("v.MHair_ga0",4);env.setVar("v.MHair_va0",2);env.setVar("v.MHair_ya0",7);
                env.setVar("v.bone_rot_xa0",60);env.setVar("v.bone_rot_fxa0",20);env.setVar("v.bone_rot_za0",30);
                for(String name:List.of("MLeftM1","LeftM1")) {
                    JsonArray axes=anim.getAsJsonObject(name).getAsJsonArray("rotation").get(0).getAsJsonObject().getAsJsonArray("post");
                    float[] val=new float[3];for(int axis=0;axis<3;axis++)val[axis]=(float)eval(axes.get(axis).getAsString(),env);
                    state.stage(indices.get(name),radians(-val[0]),radians(-val[1]),radians(val[2]));
                }
                close(0,eval("ysm.bone_rot('LeftM1').x",env),"same evaluation still sees bind");
                state.commit();
                close(18,eval("ysm.bone_rot('MLeftM1').x",env),"real MLeftM1 channel is nonzero");
                close(7,eval("ysm.bone_rot('LeftM1').x",env),"real LeftM1 channel is nonzero");
                env.setVar("v.bone_rot_x1a0",20);
                String dependent=anim.getAsJsonObject("LeftM2").getAsJsonArray("rotation").get(0).getAsJsonObject().getAsJsonArray("post").get(0).getAsString();
                close(11.6,eval(dependent,env),"real dependent LeftM2 channel uses committed rotation");
            }
            for(String expression:code)eval(expression,env);
            check(errors.size()==before,"actual expressions evaluate without compile failures: "+model);
            if(model.equals("zhiban_hanfu"))check(missing.equals(List.of("FLeftM1")),"real zhiban missing-name evidence");
            else check(new HashSet<>(missing).equals(Set.of("FLeftM1","MLeftM1","LeftM1","LeftM2","LeftM3","LeftM4","LeftM5")),"real winefox missing-name evidence");
            System.out.println("PASS resource="+model+" expressions="+code.size()+" componentCalls="+env.componentCalls+" missing="+missing);
        }
    }
    public static void main(String[] args) throws Exception {
        Molang.setErrorReporter((source,error)->errors.add(source+": "+error));
        parserAndState();actualResources(Path.of(args[0]));
        check(errors.size()==2,"only deliberately invalid members reported");
        System.out.println("PASS checks="+checks+" expectedInvalidExpressions="+errors.size());
    }
}
