package probe;
import java.nio.file.*;
import java.util.*;
import java.util.zip.*;
import java.security.*;
import org.objectweb.asm.*;
import org.objectweb.asm.tree.*;
import org.objectweb.asm.tree.analysis.*;
import org.spongepowered.asm.launch.MixinBootstrap;
import org.spongepowered.asm.mixin.*;
import org.spongepowered.asm.mixin.transformer.IMixinTransformer;

public final class TransformMurasama {
 public static void main(String[] args)throws Exception {
  MixinBootstrap.init();var environment=MixinEnvironment.getCurrentEnvironment();environment.setSide(MixinEnvironment.Side.CLIENT);Mixins.addConfiguration("murasama-only.mixins.json");
  var constructor=Class.forName("org.spongepowered.asm.mixin.transformer.MixinTransformer").getDeclaredConstructor();constructor.setAccessible(true);var transformer=(IMixinTransformer)constructor.newInstance();
  com.llamalad7.mixinextras.MixinExtrasBootstrap.init();
  String owner="com.hm.efn.client.renderer.RenderMurasama";byte[] before;
  try(var z=new ZipFile(args[0])){before=z.getInputStream(z.getEntry(owner.replace('.','/')+".class")).readAllBytes();}
  byte[] after=transformer.transformClass(environment,owner,before);if(Arrays.equals(before,after))throw new AssertionError("Actual class not transformed");
  var node=new ClassNode();new ClassReader(after).accept(node,0);int calls=0,methods=0;
  for(var method:node.methods){if((method.access&(Opcodes.ACC_ABSTRACT|Opcodes.ACC_NATIVE))==0){new Analyzer<>(new BasicVerifier()).analyze(node.name,method);methods++;}
   if(method.name.equals("renderItemInHand"))for(var instruction:method.instructions)if(instruction instanceof MethodInsnNode call&&call.name.contains("goldcraft$optionalMesh"))calls++;
  }
  if(calls!=4)throw new AssertionError("Wrong wrapper count "+calls);
  var original=new ClassNode();new ClassReader(before).accept(original,0);
  var oc=original.methods.stream().filter(m->m.name.equals("<init>")).findFirst().orElseThrow();
  var ac=node.methods.stream().filter(m->m.name.equals("<init>")).findFirst().orElseThrow();
  if(!Arrays.equals(canonical(oc),canonical(ac)))throw new AssertionError("Constructor changed");
  for(var m:original.methods)if(m.name.equals("renderItemInHand")){m.name="renderOriginal";node.methods.add(m);}
  var writer=new ClassWriter(0);node.accept(writer);Path dest=Path.of(args[1],owner.replace('.','/')+".class");Files.createDirectories(dest.getParent());Files.write(dest,writer.toByteArray());
  System.out.println("PASS original pinned class transformed by Sponge; 4 exact accessor wrappers; constructor bytecode identical; verified methods="+methods+" constructorSHA256="+HexFormat.of().formatHex(MessageDigest.getInstance("SHA-256").digest(canonical(oc))));
 }
 static byte[] canonical(MethodNode method){var w=new ClassWriter(0);w.visit(Opcodes.V21,Opcodes.ACC_PUBLIC,"Identity",null,"java/lang/Object",null);method.accept(w);w.visitEnd();return w.toByteArray();}
}
