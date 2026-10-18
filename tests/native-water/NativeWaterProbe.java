package dev.goldcraft.healthprobe;

import com.google.gson.JsonArray;
import com.google.gson.JsonObject;
import dev.goldcraft.bridge.Wire;
import dev.goldcraft.world.NativePlayers;
import dev.goldcraft.world.NativeWater;
import net.minecraft.core.BlockPos;
import net.minecraft.world.entity.ai.attributes.Attributes;
import net.minecraft.world.effect.MobEffectInstance;
import net.minecraft.world.effect.MobEffects;
import net.minecraft.world.level.block.Blocks;
import net.minecraft.world.phys.Vec3;
import net.neoforged.fml.common.Mod;
import net.neoforged.neoforge.common.NeoForge;
import net.neoforged.neoforge.common.NeoForgeMod;
import net.neoforged.neoforge.event.server.ServerStartedEvent;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.UUID;
import static dev.goldcraft.healthprobe.HealthWorldFixture.*;

/** Real vanilla water travel supplies the reference velocities for native movement tests. */
@Mod("goldcraft_native_water_probe")
public final class NativeWaterProbe {
    static int checks;static JsonArray cases=new JsonArray();
    static void check(boolean ok,String name){checks++;if(!ok)throw new AssertionError(name);}
    public NativeWaterProbe(){NeoForge.EVENT_BUS.addListener(NativeWaterProbe::run);}
    static byte[] publish(HealthBridgeFixture peer,dev.goldcraft.bridge.HostWorldState.Actor actor,net.minecraft.world.entity.LivingEntity owner)throws Exception{
        NativeWater.update(actor,owner);
        var field=NativeWater.class.getDeclaredField("revision");field.setAccessible(true);long revision=field.getLong(null);
        while(true){var bytes=peer.take(102).payload();var reader=new Wire.Reader(bytes);reader.i64();if(reader.i64()==revision)return bytes;}
    }
    static void exercise(HealthBridgeFixture peer)throws Exception{
        fresh();var a=actor(NONE,3,7,11,1,1000,0);snapshot(a,200);NativePlayers.tick(server,host);
        var box=NativePlayers.hitbox(a);check(box!=null&&box.isNoGravity(),"native proxy has implementation no-gravity");
        var packet=publish(peer,a,box);var r=new Wire.Reader(packet);
        r.i64();r.i64();for(int i=0;i<4;i++)r.i32();check(r.i32()==0,"ordinary native state keeps CS movement");
        for(int i=0;i<4;i++)r.f32();check(Math.abs(r.f32()-.08f)<.000001,"proxy flag does not erase real player gravity");r.finish();
        // Test vanilla fluid travel in an ordinary MC world, independent of BSP
        // collision. Native hull collision is exercised by the C++/ReHLDS tests.
        var dim=host.getClass().getDeclaredField("dimension");dim.setAccessible(true);dim.set(host,"minecraft:the_nether");
        for(int x=-4;x<=4;x++)for(int z=-4;z<=4;z++)for(int y=67;y<=76;y++)
            world.setBlock(new BlockPos(x,y,z),Blocks.WATER.defaultBlockState(),3);
        var p=player(UUID.randomUUID());p.setNoGravity(false);
        for(int n=0;n<64;n++){
            boolean dolphin=(n&1)!=0,ground=(n&2)!=0,slow=(n&4)!=0;
            float efficiency=(n&8)!=0?1:0,swim=(n&16)!=0?2:1;
            p.removeAllEffects();if(dolphin)p.addEffect(new MobEffectInstance(MobEffects.DOLPHINS_GRACE,200));
            if(slow)p.addEffect(new MobEffectInstance(MobEffects.SLOW_FALLING,200));
            p.getAttribute(Attributes.WATER_MOVEMENT_EFFICIENCY).setBaseValue(efficiency);
            p.getAttribute(Attributes.MOVEMENT_SPEED).setBaseValue(.1);
            p.getAttribute(NeoForgeMod.SWIM_SPEED).setBaseValue(swim);
            p.getAttribute(Attributes.GRAVITY).setBaseValue((n&32)!=0?((n&8)!=0?0:.02):.08);
            p.setPos(0,70,0);p.setYRot(-90);p.setSprinting((n&48)==48);p.baseTick();p.setOnGround(ground);
            check(p.isInWater(),"actual water detection");
            Vec3 initial=new Vec3(.2,(n&32)!=0?.1:-.1,0);p.setDeltaMovement(initial);
            packet=publish(peer,a,p);check(packet.length==56,"actual water publication");
            p.travel(new Vec3(0,0,1));Vec3 actual=p.getDeltaMovement();
            check(actual.length()>0&&Double.isFinite(actual.length()),"actual vanilla travel");
            var item=new JsonObject();item.addProperty("packet",java.util.HexFormat.of().formatHex(packet));item.addProperty("ground",ground);
            item.addProperty("initialX",initial.x*640);item.addProperty("initialY",-initial.z*640);item.addProperty("initialZ",initial.y*640);
            item.addProperty("x",actual.x*640);item.addProperty("y",-actual.z*640);item.addProperty("z",actual.y*640);cases.add(item);
        }
        peer.authority(false);NativeWater.update(a,p);Thread.sleep(80);check(peer.messages.get(102).isEmpty(),"no disconnected publication");peer.authority(true);
    }
    static void run(ServerStartedEvent event){
        var result=new JsonObject();
        try{
            server=event.getServer();world=server.overworld();bsp=Files.readAllBytes(Path.of("cs_assault.bsp"));
            try(var peer=new HealthBridgeFixture(67108864)){exercise(peer);}result.addProperty("passed",true);
        }catch(Throwable e){result.addProperty("passed",false);result.addProperty("error",e.toString());e.printStackTrace();}
        finally{try{cleanup();}catch(Throwable e){result.addProperty("cleanupError",e.toString());}result.addProperty("checks",checks);result.add("cases",cases);
            try{Files.writeString(Path.of("native-water.json"),result.toString());}catch(Exception e){throw new RuntimeException(e);}}
    }
}
