package dev.goldcraft.world;

import dev.goldcraft.GoldCraft;
import net.minecraft.entity.damage.DamageSource;
import net.minecraft.registry.Registries;
import net.minecraft.server.network.ServerPlayerEntity;
import net.minecraft.util.Identifier;
import java.lang.invoke.MethodHandle;
import java.lang.invoke.MethodHandles;
import java.lang.invoke.MethodType;

/** Read current server immunity without dispatching a speculative attack event. */
public final class NativeSkillGuard {
    private NativeSkillGuard(){}
    private static final class Epic {
        static final Epic INSTANCE=resolve();
        final MethodHandle patch,state,result,dealt;
        Epic() throws ReflectiveOperationException {
            var capabilities=Class.forName("yesman.epicfight.world.capabilities.EpicFightCapabilities");
            var playerPatch=Class.forName("yesman.epicfight.world.capabilities.entitypatch.player.ServerPlayerPatch");
            var entityState=Class.forName("yesman.epicfight.api.animation.types.EntityState");
            var resultType=Class.forName("yesman.epicfight.api.utils.AttackResult$ResultType");
            var lookup=MethodHandles.publicLookup();
            patch=lookup.findStatic(capabilities,"getServerPlayerPatch",MethodType.methodType(playerPatch,ServerPlayerEntity.class));
            state=lookup.findVirtual(playerPatch,"getEntityState",MethodType.methodType(entityState));
            result=lookup.findVirtual(entityState,"attackResult",MethodType.methodType(resultType,DamageSource.class));
            dealt=lookup.findVirtual(resultType,"dealtDamage",MethodType.methodType(boolean.class));
        }
        static Epic resolve(){
            try{return new Epic();}
            catch(ClassNotFoundException absent){return null;}
            catch(ReflectiveOperationException mismatch){
                GoldCraft.LOGGER.warn("Epic infection immunity API unavailable",mismatch);return null;
            }
        }
    }
    private static boolean reported;
    public static boolean infectionProtected(ServerPlayerEntity player){
        if(!player.isAlive()||player.isRemoved())return false;
        var effect=Registries.STATUS_EFFECT.getEntry(Identifier.of("efn","invincibility"));
        if(effect.isPresent()&&player.hasStatusEffect(effect.get()))return true;
        var api=Epic.INSTANCE;
        if(api==null)return false;
        try{
            Object patch=api.patch.invoke(player);
            if(patch==null)return false;
            // Infection is a player-melee threat. Only inspect the animation's
            // current source predicate; tryHurt would execute parry callbacks.
            var source=player.getDamageSources().playerAttack(player);
            return !(boolean)api.dealt.invoke(api.result.invoke(api.state.invoke(patch),source));
        }catch(Throwable failure){
            if(!reported){reported=true;GoldCraft.LOGGER.warn("Epic infection immunity query failed",failure);}
            return false;
        }
    }
}
