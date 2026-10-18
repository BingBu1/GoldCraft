package dev.goldcraft.waterservers;

import com.google.gson.JsonObject;
import com.mojang.brigadier.arguments.IntegerArgumentType;
import dev.goldcraft.GoldCraft;
import dev.goldcraft.world.NativePlayers;
import net.minecraft.commands.Commands;
import net.minecraft.core.BlockPos;
import net.minecraft.world.entity.ai.attributes.Attributes;
import net.minecraft.world.effect.MobEffectInstance;
import net.minecraft.world.effect.MobEffects;
import net.minecraft.world.level.block.Blocks;
import net.neoforged.fml.common.Mod;
import net.neoforged.neoforge.common.NeoForge;
import net.neoforged.neoforge.common.NeoForgeMod;
import net.neoforged.neoforge.event.RegisterCommandsEvent;
import java.nio.file.Files;
import java.nio.file.Path;

/** Create real MC water and grant effects. Fluid/state transport stays production. */
@Mod("goldcraft_native_water_servers_probe")
public final class NativeWaterServersProbe {
    static int calls;static boolean filled;
    public NativeWaterServersProbe(){NeoForge.EVENT_BUS.addListener(NativeWaterServersProbe::commands);}
    static void commands(RegisterCommandsEvent event){
        event.getDispatcher().register(Commands.literal("gc_water_probe").requires(s->s.hasPermission(4))
            .then(Commands.argument("slot",IntegerArgumentType.integer(1,64))
            .then(Commands.argument("mode",IntegerArgumentType.integer(0,3)).executes(context->{
                int slot=IntegerArgumentType.getInteger(context,"slot"),mode=IntegerArgumentType.getInteger(context,"mode");
                var actor=GoldCraft.HOST_WORLD.actors().stream().filter(a->a.slot()==slot).findFirst().orElseThrow();
                var owner=NativePlayers.hitbox(actor);if(owner==null)throw new IllegalStateException("Native owner not ready");
                if(!filled){
                    for(int x=13;x<=21;x++)for(int z=-10;z<=-1;z++)for(int y=63;y<=73;y++)
                        owner.level().setBlock(new BlockPos(x,y,z),Blocks.WATER.defaultBlockState(),3);
                    filled=true;
                }
                owner.removeEffect(MobEffects.DOLPHINS_GRACE);
                owner.getAttribute(Attributes.WATER_MOVEMENT_EFFICIENCY).setBaseValue(mode==2?1:0);
                owner.getAttribute(NeoForgeMod.SWIM_SPEED).setBaseValue(mode==2?2:1);
                if(mode==1||mode==3)owner.addEffect(new MobEffectInstance(MobEffects.DOLPHINS_GRACE,mode==3?20:10000));
                try{var report=new JsonObject();report.addProperty("calls",++calls);report.addProperty("mode",mode);report.addProperty("slot",slot);
                    Files.writeString(Path.of("native-water-live.json"),report.toString());}catch(Exception e){throw new RuntimeException(e);}
                return 1;
            }))));
    }
}
