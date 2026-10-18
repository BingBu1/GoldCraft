package dev.goldcraft.conduitservers;

import com.mojang.brigadier.arguments.IntegerArgumentType;
import dev.goldcraft.GoldCraft;
import dev.goldcraft.world.NativePlayers;
import net.minecraft.commands.Commands;
import net.minecraft.world.effect.MobEffectInstance;
import net.minecraft.world.effect.MobEffects;
import net.neoforged.fml.common.Mod;
import net.neoforged.neoforge.common.NeoForge;
import net.neoforged.neoforge.event.RegisterCommandsEvent;

/** Grant/remove an actual effect; all bridge input, ticking and output remain production. */
@Mod("goldcraft_conduit_servers_probe")
public final class ConduitServersProbe {
    public ConduitServersProbe(){NeoForge.EVENT_BUS.addListener(ConduitServersProbe::commands);}
    static void commands(RegisterCommandsEvent event){
        event.getDispatcher().register(Commands.literal("gc_conduit_probe")
            .requires(source->source.hasPermission(4))
            .then(Commands.argument("duration",IntegerArgumentType.integer(0,20000)).executes(context->{
                var actor=GoldCraft.HOST_WORLD.actors().stream().filter(a->(a.flags()&5)==1&&a.health()>0).findFirst().orElseThrow();
                var owner=NativePlayers.hitbox(actor);
                if(owner==null)throw new IllegalStateException("Native owner not ready");
                int duration=IntegerArgumentType.getInteger(context,"duration");
                owner.removeEffect(MobEffects.CONDUIT_POWER);
                if(duration>0)owner.addEffect(new MobEffectInstance(MobEffects.CONDUIT_POWER,duration));
                System.out.println("[GC Conduit] effect duration="+duration+" slot="+actor.slot());return 1;
            })));
    }
}
