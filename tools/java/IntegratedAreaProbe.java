package dev.goldcraft.areaprobe;

import com.mojang.brigadier.arguments.StringArgumentType;
import com.mojang.brigadier.arguments.DoubleArgumentType;
import dev.goldcraft.GoldCraft;
import dev.goldcraft.world.NativePlayerHitboxEntity;
import dev.goldcraft.world.NativePlayers;
import net.minecraft.commands.Commands;
import net.minecraft.network.chat.Component;
import net.minecraft.world.entity.EntityType;
import net.minecraft.world.entity.LivingEntity;
import net.minecraft.world.entity.Entity;
import net.minecraft.world.entity.monster.Zombie;
import net.minecraft.server.level.ServerLevel;
import net.minecraft.world.InteractionHand;
import net.minecraft.core.registries.BuiltInRegistries;
import net.neoforged.fml.common.Mod;
import net.neoforged.neoforge.common.NeoForge;
import net.neoforged.neoforge.event.RegisterCommandsEvent;
import yesman.epicfight.world.capabilities.EpicFightCapabilities;
import yesman.epicfight.world.capabilities.entitypatch.LivingEntityPatch;
import yesman.epicfight.api.animation.types.AttackAnimation;
import yesman.epicfight.api.animation.types.EntityState;
import yesman.epicfight.api.asset.AssetAccessor;
import java.lang.invoke.*;
import java.util.*;

@Mod("goldcraft_area_probe")
public class IntegratedAreaProbe {
    private static Zombie owner;
    public IntegratedAreaProbe(){NeoForge.EVENT_BUS.addListener(IntegratedAreaProbe::commands);}
    static NativePlayerHitboxEntity target(ServerLevel level){
        for(Entity entity:level.getAllEntities())if(entity instanceof NativePlayerHitboxEntity h&&h.isAlive())return h;
        throw new IllegalStateException("Native target missing");
    }
    static ServerLevel level(net.minecraft.commands.CommandSourceStack s){
        for(var level:s.getServer().getAllLevels())if(level.dimension().location().toString().equals(GoldCraft.HOST_WORLD.dimension()))return level;
        throw new IllegalStateException("Host dimension unavailable");
    }
    static void commands(RegisterCommandsEvent event){
        event.getDispatcher().register(Commands.literal("area_prepare").requires(s->s.hasPermission(4)).then(Commands.argument("distance",DoubleArgumentType.doubleArg(1,32)).executes(c->{
            var level=level(c.getSource());var target=target(level);double distance=DoubleArgumentType.getDouble(c,"distance");
            if(owner!=null)owner.discard();owner=EntityType.ZOMBIE.create(level);owner.setNoAi(true);owner.setPersistenceRequired();
            owner.setPos(target.getX()-distance,target.getY(),target.getZ());owner.xo=owner.xOld=owner.getX();owner.yo=owner.yOld=owner.getY();owner.zo=owner.zOld=owner.getZ();
            owner.setYRot(-90);owner.yRotO=-90;owner.yBodyRot=owner.yBodyRotO=-90;owner.yHeadRot=owner.yHeadRotO=-90;
            level.addFreshEntity(owner);
            long logical=level.getEntitiesOfClass(LivingEntity.class,target.getBoundingBox().inflate(.01)).stream().filter(e->e.getClass().getName().contains("NativePlayer")).count();
            c.getSource().sendSuccess(()->Component.literal("prepared distance="+distance+" spatialNativeTargets="+logical+" health="+target.getHealth()),false);return 1;
        })));
        event.getDispatcher().register(Commands.literal("area_fire").requires(s->s.hasPermission(4)).then(Commands.argument("skill",StringArgumentType.word()).executes(c->{
            String skill=StringArgumentType.getString(c,"skill");var level=level(c.getSource());var target=target(level);
            if(owner==null||owner.isRemoved())throw new IllegalStateException("Prepare owner first");
            float before=target.getHealth();int selected=0;String result="";
            try{
                if(skill.equals("normal")||skill.equals("perfect")){
                    var type=BuiltInRegistries.ENTITY_TYPE.get(net.minecraft.resources.ResourceLocation.fromNamespaceAndPath("efn","judgementcut_"+skill));
                    var effect=type.create(level);effect.setPos(target.position());effect.getClass().getMethod("setOwner",LivingEntity.class).invoke(effect,owner);
                    effect.getClass().getMethod("setAttackDamage",float.class).invoke(effect,5f);level.addFreshEntity(effect);
                    var attack=effect.getClass().getDeclaredMethod("performAttack");attack.setAccessible(true);attack.invoke(effect);effect.discard();selected=1;
                }else if(skill.equals("plain")){
                    result=Boolean.toString(target.hurt(level.damageSources().mobAttack(owner),5));selected=1;
                }else if(skill.equals("endcut")){
                    LivingEntityPatch<?> patch=EpicFightCapabilities.getEntityPatch(owner,LivingEntityPatch.class);
                    patch.getCurrentlyActuallyHitEntities().add(target);
                    Class.forName("com.hm.efn.util.yamato.DMC_V_JC_Server").getMethod("postAttack",LivingEntityPatch.class).invoke(null,patch);
                    selected=1;patch.removeHurtEntities();
                }else if(skill.equals("mortal1")||skill.equals("mortal2")){
                    LivingEntityPatch<?> patch=EpicFightCapabilities.getEntityPatch(owner,LivingEntityPatch.class);
                    var accessor=(AssetAccessor<?>)Class.forName("com.hm.efn.gameasset.animations.EFNSekiroAnimations").getField(skill.equals("mortal1")?"MORTAL_BLADE_1":"MORTAL_BLADE_2").get(null);
                    var animation=(AttackAnimation)accessor.get();
                    patch.playAnimation((AssetAccessor)accessor,0);
                    var invoke=MethodHandles.privateLookupIn(AttackAnimation.class,MethodHandles.lookup()).findVirtual(AttackAnimation.class,"hurtCollidingEntities",MethodType.methodType(void.class,LivingEntityPatch.class,float.class,float.class,EntityState.class,EntityState.class,AttackAnimation.Phase.class));
                    patch.removeHurtEntities();
                    for(var phase:animation.phases)for(int n=0;n<10;n++){
                        float t0=phase.preDelay+(phase.contact-phase.preDelay)*n/10,t1=phase.preDelay+(phase.contact-phase.preDelay)*(n+1)/10;
                        selected+=phase.getCollidingEntities(patch,animation,t0,t1,1).contains(target)?1:0;
                        invoke.invoke(animation,patch,t0,t1,animation.getState(patch,t0),animation.getState(patch,t1),phase);
                    }
                    result="tried="+patch.getCurrentlyAttackTriedEntities().size()+" hit="+patch.getCurrentlyActuallyHitEntities().size();
                    // Stop the manually sampled animation before the server's
                    // next tick, so it cannot hit a freshly respawned fixture.
                    patch.playAnimation(yesman.epicfight.gameasset.Animations.BIPED_IDLE,0);
                }else throw new IllegalArgumentException("Unknown fixture skill");
            }catch(Throwable error){error.printStackTrace();c.getSource().sendFailure(Component.literal(error.toString()));return 0;}
            int picked=selected;String status=result;
            c.getSource().sendSuccess(()->Component.literal("fired skill="+skill+" selected="+picked+" localHealthBefore="+before+" localHealthAfter="+target.getHealth()+" "+status),false);return 1;
        })));
        event.getDispatcher().register(Commands.literal("area_identity").requires(s->s.hasPermission(4)).executes(c->{
            var level=level(c.getSource());var actor=GoldCraft.HOST_WORLD.actors().getFirst();
            var logical=NativePlayers.attacker(c.getSource().getServer(),GoldCraft.HOST_WORLD,actor);
            c.getSource().sendSuccess(()->Component.literal("uuidLookup="+(level.getEntity(logical.getUUID())==logical)+" playerLookup="+(level.getPlayerByUUID(logical.getUUID())==logical)+" spatialBody="+(level.getEntity(logical.getId())!=null)),false);return 1;
        }));
    }
}
