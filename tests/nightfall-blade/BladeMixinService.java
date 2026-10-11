package probe;

import java.io.*;
import java.net.*;
import java.util.*;
import org.objectweb.asm.ClassReader;
import org.objectweb.asm.tree.ClassNode;
import org.spongepowered.asm.launch.platform.container.*;
import org.spongepowered.asm.service.*;
import org.spongepowered.asm.mixin.MixinEnvironment;

/** Isolated bytecode service: does not launch Minecraft or initialize NightFall. */
public final class BladeMixinService extends MixinServiceAbstract implements IClassProvider,IClassBytecodeProvider,ITransformerProvider,IClassTracker {
    public String getName(){return "GoldCraft isolated NightFall transformation";}
    public boolean isValid(){return true;}
    public MixinEnvironment.Phase getInitialPhase(){return MixinEnvironment.Phase.DEFAULT;}
    public IClassProvider getClassProvider(){return this;}
    public IClassBytecodeProvider getBytecodeProvider(){return this;}
    public ITransformerProvider getTransformerProvider(){return this;}
    public IClassTracker getClassTracker(){return this;}
    public IMixinAuditTrail getAuditTrail(){return null;}
    public Collection<String> getPlatformAgents(){return List.of();}
    public IContainerHandle getPrimaryContainer(){return new ContainerHandleVirtual("blade-probe");}
    public InputStream getResourceAsStream(String name){return getClass().getClassLoader().getResourceAsStream(name);}
    public URL[] getClassPath(){return new URL[0];}
    public Class<?> findClass(String name)throws ClassNotFoundException{return findClass(name,false);}
    public Class<?> findClass(String name,boolean initialize)throws ClassNotFoundException{return Class.forName(name,initialize,getClass().getClassLoader());}
    public Class<?> findAgentClass(String name,boolean initialize)throws ClassNotFoundException{return findClass(name,initialize);}
    public ClassNode getClassNode(String name)throws ClassNotFoundException,IOException{return getClassNode(name,false);}
    public ClassNode getClassNode(String name,boolean transform)throws ClassNotFoundException,IOException{return getClassNode(name,transform,0);}
    public ClassNode getClassNode(String name,boolean transform,int flags)throws ClassNotFoundException,IOException{
        try(var in=getResourceAsStream(name.replace('.','/')+".class")){
            if(in==null)throw new ClassNotFoundException(name);
            var node=new ClassNode();new ClassReader(in).accept(node,flags);return node;
        }
    }
    public Collection<ITransformer> getTransformers(){return List.of();}
    public Collection<ITransformer> getDelegatedTransformers(){return List.of();}
    public void addTransformerExclusion(String name){}
    public void registerInvalidClass(String name){}
    public boolean isClassLoaded(String name){return false;}
    public String getClassRestrictions(String name){return "";}
}
