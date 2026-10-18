package dev.goldcraft.impulseservers;

import com.google.gson.JsonObject;
import com.mojang.brigadier.arguments.IntegerArgumentType;
import dev.goldcraft.GoldCraft;
import dev.goldcraft.world.NativePlayers;
import net.minecraft.commands.Commands;
import net.minecraft.world.entity.Entity;
import net.minecraft.world.entity.EntityType;
import net.minecraft.world.effect.MobEffectInstance;
import net.minecraft.world.effect.MobEffects;
import net.minecraft.world.phys.Vec3;
import net.neoforged.fml.common.Mod;
import net.neoforged.neoforge.common.NeoForge;
import net.neoforged.neoforge.event.RegisterCommandsEvent;
import net.neoforged.neoforge.event.level.ExplosionEvent;
import net.neoforged.neoforge.event.level.ExplosionKnockbackEvent;
import java.nio.file.Files;
import java.nio.file.Path;

/** Trigger actual wind effects; record final vanilla/Mod vectors without changing bridge packets. */
@Mod("goldcraft_native_impulse_servers_probe")
public final class NativeImpulseServersProbe {
    static int mode,slot,commands,events;static Entity owner;static Vec3 impulse=Vec3.ZERO;
    public NativeImpulseServersProbe(){
        NeoForge.EVENT_BUS.addListener(NativeImpulseServersProbe::commands);
        NeoForge.EVENT_BUS.addListener((ExplosionEvent.Start e)->{if(mode==3)e.setCanceled(true);});
        NeoForge.EVENT_BUS.addListener((ExplosionKnockbackEvent e)->{
            if(e.getAffectedEntity()==owner){events++;if(mode==2)e.setKnockbackVelocity(Vec3.ZERO);impulse=e.getKnockbackVelocity();}
        });
    }
    static void commands(RegisterCommandsEvent event){
        event.getDispatcher().register(Commands.literal("gc_impulse_probe").requires(s->s.hasPermission(4))
            .then(Commands.argument("slot",IntegerArgumentType.integer(1,64))
            .then(Commands.argument("mode",IntegerArgumentType.integer(0,3)).executes(context->{
                slot=IntegerArgumentType.getInteger(context,"slot");mode=IntegerArgumentType.getInteger(context,"mode");
                var actor=GoldCraft.HOST_WORLD.actors().stream().filter(a->a.slot()==slot).findFirst().orElseThrow();
                owner=NativePlayers.hitbox(actor);if(owner==null)throw new IllegalStateException("Native owner not ready");
                events=0;impulse=Vec3.ZERO;
                if(mode>0){
                    var source=EntityType.ZOMBIE.create(owner.level());source.setPos(owner.getX()+1,owner.getY(),owner.getZ());
                    owner.level().addFreshEntity(source);source.addEffect(new MobEffectInstance(MobEffects.WIND_CHARGED,100));source.remove(Entity.RemovalReason.KILLED);
                }
                try{
                    var r=new JsonObject();r.addProperty("commands",++commands);r.addProperty("events",events);r.addProperty("slot",slot);r.addProperty("mode",mode);
                    r.addProperty("x",impulse.x);r.addProperty("y",impulse.y);r.addProperty("z",impulse.z);
                    Files.writeString(Path.of("native-impulse-live.json"),r.toString());
                }catch(Exception e){throw new RuntimeException(e);}return 1;
            }))));
    }
}
