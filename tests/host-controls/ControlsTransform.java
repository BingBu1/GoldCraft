package probe;

import java.nio.file.*;
import java.util.*;
import java.util.zip.*;
import org.objectweb.asm.*;
import org.objectweb.asm.commons.*;
import org.objectweb.asm.tree.*;
import org.objectweb.asm.tree.analysis.*;
import org.spongepowered.asm.launch.MixinBootstrap;
import org.spongepowered.asm.mixin.*;
import org.spongepowered.asm.mixin.transformer.IMixinTransformer;

/** Real injection targets and mod bytecode; adapters supply only runtime boundaries. */
public final class ControlsTransform {
    static ClassNode read(byte[] bytes){var n=new ClassNode();new ClassReader(bytes).accept(n,0);return n;}
    static byte[] entry(Path jar,String name)throws Exception{
        try(var z=new ZipFile(jar.toFile())){return z.getInputStream(z.getEntry(name+".class")).readAllBytes();}
    }
    static void put(Path classes,String name,ClassNode node)throws Exception{
        var w=new ClassWriter(ClassWriter.COMPUTE_MAXS);node.accept(w);
        Path p=classes.resolve(name+".class");Files.createDirectories(p.getParent());Files.write(p,w.toByteArray());
    }
    static void transplant(Path classes,String owner,ClassNode actual,Set<String> names)throws Exception{
        var target=read(Files.readAllBytes(classes.resolve(owner+".class")));
        for(var m:actual.methods)if(names.contains(m.name)||m.name.contains("goldcraft$")){
            target.methods.removeIf(old->old.name.equals(m.name)&&old.desc.equals(m.desc));target.methods.add(m);
        }
        put(classes,owner,target);
    }
    public static void main(String[] args)throws Exception{
        Path game=Path.of(args[0]),classes=Path.of(args[1]),evidence=Path.of(args[2]);
        MixinBootstrap.init();var env=MixinEnvironment.getCurrentEnvironment();env.setSide(MixinEnvironment.Side.CLIENT);
        Mixins.addConfiguration("controls.mixins.json");
        var ctor=Class.forName("org.spongepowered.asm.mixin.transformer.MixinTransformer").getDeclaredConstructor();ctor.setAccessible(true);
        var transform=(IMixinTransformer)ctor.newInstance();int verified=0;
        var targets=Map.of("net/minecraft/client/MinecraftClient",Set.of("isWindowFocused"),
            "net/minecraft/client/Mouse",Set.of("isCursorLocked"),
            "net/minecraft/client/util/InputUtil",Set.of("isKeyPressed"));
        for(var e:targets.entrySet()){
            byte[] before=entry(game,e.getKey()),after=transform.transformClass(env,e.getKey().replace('/','.'),before);
            if(Arrays.equals(before,after))throw new AssertionError("Missing transform "+e.getKey());
            var n=read(after);int hooks=0;
            for(var m:n.methods){
                if((m.access&(Opcodes.ACC_ABSTRACT|Opcodes.ACC_NATIVE))==0){new Analyzer<>(new BasicVerifier()).analyze(n.name,m);verified++;}
                for(var i:m.instructions)if(i instanceof MethodInsnNode c&&c.owner.equals(n.name)&&c.name.contains("goldcraft$"))hooks++;
            }
            int expected=e.getKey().endsWith("Mouse")?4:1;
            if(hooks!=expected)throw new AssertionError("Wrong hook count "+e.getKey()+":"+hooks);
            Files.write(evidence.resolve(e.getKey().substring(e.getKey().lastIndexOf('/')+1)+".class"),after);
            transplant(classes,e.getKey(),n,e.getValue());
        }
        transplant(classes,"net/minecraft/client/gui/screen/Screen",read(entry(game,"net/minecraft/client/gui/screen/Screen")),
            Set.of("hasAltDown","hasShiftDown","hasControlDown"));
        // Only rename symbols between Mojang and Yarn; the actual gate/chord
        // instructions are kept intact, without compiling a rewritten predicate.
        var remap=new Remapper(){
            public String map(String n){return switch(n){
                case "net/minecraft/client/Minecraft"->"net/minecraft/client/MinecraftClient";
                case "net/minecraft/client/MouseHandler"->"net/minecraft/client/Mouse";
                case "net/minecraft/client/gui/screens/Screen"->"net/minecraft/client/gui/screen/Screen";
                case "net/minecraft/client/gui/screens/Overlay"->"net/minecraft/client/gui/screen/Overlay";
                case "net/minecraft/client/KeyMapping"->"net/minecraft/client/option/KeyBinding";
                default->n;};}
            public String mapMethodName(String o,String n,String d){return switch(n){
                case "isMouseGrabbed"->"isCursorLocked";case "isWindowActive"->"isWindowFocused";case "matches"->"matchesKey";default->n;};}
            public String mapFieldName(String o,String n,String d){return switch(n){case "mouseHandler"->"mouse";case "screen"->"currentScreen";default->n;};}
        };
        for(int i=0;i<2;i++){
            String owner=i==0?"com/tacz/guns/util/InputExtraCheck":"com/elfmcys/yesstevemodel/OOoOOoo00oOo0OOOoOooooO0";
            var renamed=new ClassNode();new ClassReader(entry(Path.of(args[3+i]),owner)).accept(new ClassRemapper(renamed,remap),0);
            String target="dev/goldcraft/client/Actual"+(i==0?"Tacz":"Ysm");
            var adapter=read(Files.readAllBytes(classes.resolve(target+".class")));
            for(var m:renamed.methods)if((m.access&Opcodes.ACC_STATIC)!=0){
                m.name=m.desc.equals("()Z")?"gate":"matches";
                adapter.methods.removeIf(old->old.name.equals(m.name));adapter.methods.add(m);
            }
            put(classes,target,adapter);
        }
        System.out.println("PASS real Sponge on three pinned Minecraft classes; 6 hook calls; verified methods="+verified+"; actual TACZ/YSM gates/chord and vanilla Screen modifier getters transplanted");
    }
}
