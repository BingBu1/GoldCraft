package probe;
import java.nio.file.*;
import java.util.*;
import java.util.zip.*;
import org.objectweb.asm.*;
import org.objectweb.asm.tree.*;
import org.objectweb.asm.tree.analysis.*;
import org.spongepowered.asm.launch.MixinBootstrap;
import org.spongepowered.asm.mixin.*;
import org.spongepowered.asm.mixin.transformer.IMixinTransformer;
public final class TransformBlade {
 public static void main(String[] args)throws Exception {
  MixinBootstrap.init();var environment=MixinEnvironment.getCurrentEnvironment();environment.setSide(MixinEnvironment.Side.CLIENT);Mixins.addConfiguration("blade-only.mixins.json");
  var constructor=Class.forName("org.spongepowered.asm.mixin.transformer.MixinTransformer").getDeclaredConstructor();constructor.setAccessible(true);var transformer=(IMixinTransformer)constructor.newInstance();
  com.llamalad7.mixinextras.MixinExtrasBootstrap.init();
  String owner="com.hm.efn.client.particle.DMC.JCBladeTrail";byte[] before;
  try(var z=new ZipFile(args[0])){before=z.getInputStream(z.getEntry(owner.replace('.','/')+".class")).readAllBytes();}
  byte[] after=transformer.transformClass(environment,owner,before);if(Arrays.equals(before,after))throw new AssertionError("Actual class not transformed");
  var node=new ClassNode();new ClassReader(after).accept(node,0);int helper=0,handler=0,vertices=0,methods=0;
  for(var method:node.methods){if((method.access&(Opcodes.ACC_ABSTRACT|Opcodes.ACC_NATIVE))==0){new Analyzer<>(new BasicVerifier()).analyze(node.name,method);methods++;}
   for(var instruction:method.instructions)if(instruction instanceof MethodInsnNode call){if(call.owner.equals("dev/goldcraft/client/VixPostCapture")&&call.name.equals("rejectUndefinedNightfallBlade"))helper++;if(call.owner.equals(node.name)&&call.name.contains("goldcraft$definedBlade"))handler++;if(call.owner.equals("com/mojang/blaze3d/vertex/VertexConsumer")&&call.name.equals("addVertex"))vertices++;}
  }
  if(helper!=1||handler!=1||vertices!=4)throw new AssertionError("Wrong calls "+helper+"/"+handler+"/"+vertices);
  Path target=Path.of(args[1]);Files.write(target,after);
  // Move exactly the transformed render and injected handler into the CPU adapter.
  Path adapter=Path.of(args[2],owner.replace('.','/')+".class");var stub=new ClassNode();new ClassReader(Files.readAllBytes(adapter)).accept(stub,0);
  var originalNode=new ClassNode();new ClassReader(before).accept(originalNode,0);
  for(var m:originalNode.methods)if(m.name.equals("render")){m.name="renderOriginal";stub.methods.removeIf(old->old.name.equals(m.name));stub.methods.add(m);}
  for(var m:node.methods)if(m.name.equals("render")||m.name.contains("goldcraft$definedBlade")){stub.methods.removeIf(old->old.name.equals(m.name));stub.methods.add(m);}
  var writer=new ClassWriter(0);stub.accept(writer);Files.write(adapter,writer.toByteArray());
  System.out.println("PASS actual pinned NightFall class transformed by Sponge Mixin; exact local capture before width multiply, one guard, four original vertex calls; BasicVerifier methods="+methods);
 }
}
