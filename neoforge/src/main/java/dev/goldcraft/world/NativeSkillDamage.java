package dev.goldcraft.world;

import net.minecraft.entity.Entity;
import net.minecraft.entity.damage.DamageSource;
import java.lang.invoke.MethodHandle;
import java.lang.invoke.MethodHandles;
import java.lang.invoke.MethodType;

/** Optional Epic damage sources, tested only on the dedicated server hurt path. */
public final class NativeSkillDamage {
    private static final ClassValue<Boolean> EPIC_SOURCE=new ClassValue<>(){
        @Override protected Boolean computeValue(Class<?> type){
            for(Class<?> current=type;current!=null;current=current.getSuperclass())
                if(current.getName().equals("yesman.epicfight.world.damagesource.EpicFightDamageSource"))return true;
            return false;
        }
    };
    private static final ClassValue<MethodHandle> OWNER=new ClassValue<>(){
        @Override protected MethodHandle computeValue(Class<?> type){
            try{return MethodHandles.publicLookup().findVirtual(type,"getOriginal",MethodType.methodType(Entity.class));}
            catch(ReflectiveOperationException e){throw new IllegalStateException("Epic skill attacker API unavailable",e);}
        }
    };
    private static final class AttributedSkill extends DamageSource {
        AttributedSkill(DamageSource original,Entity attacker){super(original.getTypeRegistryEntry(),attacker);}
    }
    private NativeSkillDamage(){}
    static boolean isSkill(Object source){return source instanceof AttributedSkill||EPIC_SOURCE.get(source.getClass());}
    /** NightFall's server-only percentage hit uses generic damage without owner. */
    public static DamageSource attributed(DamageSource original,Object patch){
        try{return new AttributedSkill(original,(Entity)OWNER.get(patch.getClass()).invoke(patch));}
        catch(RuntimeException|Error e){throw e;}
        catch(Throwable e){throw new IllegalStateException("Epic skill attacker lookup failed",e);}
    }
}
