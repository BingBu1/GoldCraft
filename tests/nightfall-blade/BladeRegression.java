package dev.goldcraft.client;

import java.nio.*;
import java.util.*;
import java.lang.reflect.*;
import com.hm.efn.client.particle.DMC.JCBladeTrail;
import com.mojang.blaze3d.vertex.VertexConsumer;
import org.joml.Matrix4f;

/** Exact transformed/original render methods and the production converter; CPU adapters are explicit. */
public final class BladeRegression {
    static int checks,cases,rejected;
    static final Object HOSTED=new Object();
    static final Method ORIGINAL;
    static {try{ORIGINAL=JCBladeTrail.class.getMethod("renderOriginal",VertexConsumer.class,net.minecraft.client.Camera.class,float.class);}
      catch(Exception e){throw new ExceptionInInitializerError(e);}}
    static final class Sink implements VertexConsumer {
        final ByteBuffer buffer=ByteBuffer.allocate(1024*1024).order(ByteOrder.nativeOrder());int vertices,at;
        public VertexConsumer addVertex(float x,float y,float z){at=vertices++*28;buffer.putFloat(at,x).putFloat(at+4,y).putFloat(at+8,z);return this;}
        public VertexConsumer setColor(float r,float g,float b,float a){buffer.put(at+12,(byte)(r*255)).put(at+13,(byte)(g*255)).put(at+14,(byte)(b*255)).put(at+15,(byte)(a*255));return this;}
        public VertexConsumer setUv(float u,float v){buffer.putFloat(at+16,u).putFloat(at+20,v);return this;}
        public VertexConsumer setLight(int light){buffer.putInt(at+24,light);return this;}
        byte[] bytes(){return Arrays.copyOf(buffer.array(),vertices*28);}
        boolean finite(){for(int v=0;v<vertices;v++)for(int a=0;a<3;a++)if(!Float.isFinite(buffer.getFloat(v*28+a*4)))return false;return true;}
        byte[] convert(){return EffectGeometry.convert(buffer.duplicate().limit(vertices*28),vertices,new EffectGeometry.Layout(28,0,12,16,24),EffectGeometry.Mode.QUADS,null,0,new Matrix4f(),0,0,0,false,65536);}
    }
    static void check(boolean ok,String why){checks++;if(!ok)throw new AssertionError(why);}
    static void render(JCBladeTrail p,float partial,Sink sink,boolean original)throws Exception {
        if(original)ORIGINAL.invoke(p,sink,new net.minecraft.client.Camera(),partial);else p.render(sink,new net.minecraft.client.Camera(),partial);
    }
    static JCBladeTrail particle(int age,float offset,int shape){var p=new JCBladeTrail();p.age=age;p.timeOffset=offset;
      if(shape==1){p.X=1;p.Y=p.Z=0;}if(shape==2)p.xd=0;return p;}
    static void one(JCBladeTrail p,float partial,String name)throws Exception {
        cases++;var original=new Sink();render(p,partial,original,true);
        var standalone=new Sink();VixPostCapture.active=null;long before=VixPostCapture.undefinedNightfallBlades;
        render(p,partial,standalone,false);
        check(Arrays.equals(original.bytes(),standalone.bytes()),name+" standalone exact original");
        check(before==VixPostCapture.undefinedNightfallBlades,name+" standalone no diagnostic mutation");
        var hosted=new Sink();VixPostCapture.active=HOSTED;render(p,partial,hosted,false);
        if(original.finite()) {
            check(Arrays.equals(original.bytes(),hosted.bytes()),name+" finite vertices/UV/color/light bit-identical");
            check(Arrays.equals(original.convert(),hosted.convert()),name+" exact production converted output");
            check(before==VixPostCapture.undefinedNightfallBlades,name+" finite no reject");
        } else {
            rejected++;check(hosted.vertices==0,name+" entire undefined particle omitted");
            check(before+1==VixPostCapture.undefinedNightfallBlades,name+" counted once per particle");
            check(hosted.convert().length==0,name+" empty conversion safe");
            try{original.convert();throw new AssertionError("original should fail");}
            catch(IllegalArgumentException e){check(e.getMessage().startsWith("Effect coordinate bounds"),name+" actual converter rejects original");}
        }
    }
    public static void main(String[] args)throws Exception {
        for(float offset:new float[]{0,.25f,.75f,1})for(int age:new int[]{0,1,4,8,9,10})
          for(float partial:new float[]{0,.001f,.25f,.5f,.75f,.999f})for(int shape=0;shape<3;shape++)
            one(particle(age,offset,shape),partial,"boundary-"+cases);
        var random=new Random(274391);
        for(int i=0;i<512;i++){var p=particle(1+random.nextInt(8),random.nextFloat(),0);p.X=random.nextFloat()*100-50;p.Y=random.nextFloat()*100-50;p.Z=random.nextFloat()*100-50;
          p.xd=random.nextFloat()*4-2;p.yd=random.nextFloat()*4-2;p.zd=random.nextFloat()*4-2;one(p,random.nextFloat(),"random-"+i);}
        var goodA=particle(4,.75f,0);var goodB=particle(8,.75f,0);goodB.Y=10;
        var bad=particle(9,.75f,0);var clean=new Sink();render(goodA,.5f,clean,true);render(goodB,.5f,clean,true);
        var mixedOriginal=new Sink();render(goodA,.5f,mixedOriginal,true);render(bad,.5f,mixedOriginal,true);render(goodB,.5f,mixedOriginal,true);
        try{mixedOriginal.convert();throw new AssertionError("mixed original should fail");}
        catch(IllegalArgumentException e){check(e.getMessage().contains("vertex=4/12"),"actual batch fails at bad particle after good quad");}
        var mixed=new Sink();VixPostCapture.active=HOSTED;long before=VixPostCapture.undefinedNightfallBlades;
        render(goodA,.5f,mixed,false);render(bad,.5f,mixed,false);render(goodB,.5f,mixed,false);
        check(mixed.vertices==8,"good/bad/good keeps two complete quads");check(before+1==VixPostCapture.undefinedNightfallBlades,"one bad mixed particle counted");
        check(Arrays.equals(mixed.bytes(),clean.bytes()),"neighbor raw bytes preserved");
        check(Arrays.equals(mixed.convert(),clean.convert()),"neighbor production capture bytes preserved");
        var finiteOutOfBounds=particle(4,.75f,0);finiteOutOfBounds.Y=40000000;var bounds=new Sink();render(finiteOutOfBounds,.5f,bounds,false);
        check(bounds.vertices==4,"guard does not relax finite world bounds");
        try{bounds.convert();throw new AssertionError("bounds must remain rejected");}
        catch(IllegalArgumentException e){check(e.getMessage().startsWith("Effect coordinate bounds"),"unrelated invalid geometry still fails converter");}
        System.out.println("PASS actual Sponge-transformed producer cases="+cases+" undefinedCases="+rejected+" checks="+checks+"; mixed good/bad/good keeps 8 bit-identical vertices; standalone unchanged; finite bounds unchanged");
    }
}
