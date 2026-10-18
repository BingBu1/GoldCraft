package dev.goldcraft.hurtservers;

import com.google.gson.JsonObject;
import com.mojang.brigadier.arguments.IntegerArgumentType;
import dev.goldcraft.GoldCraft;
import dev.goldcraft.world.NativePlayers;
import net.minecraft.commands.Commands;
import net.minecraft.world.effect.MobEffectInstance;
import net.minecraft.world.effect.MobEffects;
import net.minecraft.world.effect.MobEffect;
import net.minecraft.world.effect.MobEffectCategory;
import net.minecraft.world.entity.LivingEntity;
import net.minecraft.world.entity.monster.Silverfish;
import net.minecraft.world.damagesource.DamageSource;
import net.minecraft.core.registries.Registries;
import net.neoforged.fml.common.Mod;
import net.neoforged.fml.loading.FMLPaths;
import net.neoforged.neoforge.common.NeoForge;
import net.neoforged.neoforge.event.RegisterCommandsEvent;
import net.neoforged.bus.api.IEventBus;
import net.neoforged.neoforge.registries.DeferredRegister;
import java.nio.file.Files;

/** Grants effects and records original callbacks; production bridges are untouched. */
@Mod("goldcraft_native_hurt_servers_probe")
public final class NativeHurtServersProbe {
    static int callbacks,slot,fish,mode,life;
    static float amount,absorption;
    static final DeferredRegister<MobEffect> EFFECTS=DeferredRegister.create(Registries.MOB_EFFECT,"goldcraft_native_hurt_servers_probe");
    static final net.neoforged.neoforge.registries.DeferredHolder<MobEffect,MobEffect> OBSERVER=EFFECTS.register("observer",()->new MobEffect(MobEffectCategory.NEUTRAL,0){
        @Override public void onMobHurt(LivingEntity owner,int amplifier,DamageSource source,float damage){
            callbacks++;amount=damage;record();
        }
    });
    public NativeHurtServersProbe(IEventBus bus){EFFECTS.register(bus);NeoForge.EVENT_BUS.addListener(NativeHurtServersProbe::commands);}
    static void record(){
        try{
            var r=new JsonObject();r.addProperty("callbacks",callbacks);r.addProperty("slot",slot);r.addProperty("amount",amount);r.addProperty("fish",fish);
            r.addProperty("mode",mode);r.addProperty("life",life);r.addProperty("absorption",absorption);
            Files.writeString(FMLPaths.GAMEDIR.get().resolve("native-hurt-live.json"),r.toString());
        }catch(Exception e){throw new RuntimeException(e);}
    }
    static void commands(RegisterCommandsEvent event){
        event.getDispatcher().register(Commands.literal("gc_hurt_probe").requires(s->s.hasPermission(4))
            .then(Commands.argument("slot",IntegerArgumentType.integer(1,64))
            .then(Commands.argument("mode",IntegerArgumentType.integer(0,2)).executes(context->{
                slot=IntegerArgumentType.getInteger(context,"slot");mode=IntegerArgumentType.getInteger(context,"mode");
                var actor=GoldCraft.HOST_WORLD.actors().stream().filter(a->a.slot()==slot).findFirst().orElseThrow();
                var owner=NativePlayers.hitbox(actor);if(owner==null)throw new IllegalStateException("Native owner not ready");
                life=actor.life();
                fish=owner.level().getEntitiesOfClass(Silverfish.class,owner.getBoundingBox().inflate(16)).size();
                if(mode>0){
                    owner.addEffect(new MobEffectInstance(OBSERVER,20000));
                    owner.getRandom().setSeed(1000016L);
                    if(mode==2)owner.addEffect(new MobEffectInstance(MobEffects.ABSORPTION,10000,1));
                }
                absorption=owner.getAbsorptionAmount();
                record();return 1;
            }))));
    }
}
