package dev.goldcraft.healthprobe;

import com.google.gson.JsonObject;
import dev.goldcraft.GoldCraft;
import dev.goldcraft.bridge.Wire;
import dev.goldcraft.world.*;
import static dev.goldcraft.healthprobe.HealthWorldFixture.*;
import net.minecraft.core.BlockPos;
import net.minecraft.resources.ResourceLocation;
import net.minecraft.world.effect.MobEffectInstance;
import net.minecraft.world.effect.MobEffects;
import net.minecraft.world.entity.EquipmentSlot;
import net.minecraft.world.entity.ai.attributes.AttributeModifier;
import net.minecraft.world.entity.ai.attributes.Attributes;
import net.minecraft.world.item.ItemStack;
import net.minecraft.world.item.Items;
import net.minecraft.world.level.block.Blocks;
import net.neoforged.fml.common.Mod;
import net.neoforged.fml.loading.FMLPaths;
import net.neoforged.neoforge.common.NeoForge;
import net.neoforged.neoforge.event.entity.player.PlayerEvent;
import net.neoforged.neoforge.event.server.ServerStartedEvent;
import java.nio.file.Files;
import java.util.UUID;
import java.util.function.Consumer;

/** Real effect attributes and the original block speed path; no mocked effects. */
@Mod("goldcraft_native_melee_probe")
public final class NativeMeleeProbe {
    static int checks,packets;static long revision;
    static void check(boolean value,String label){checks++;if(!value)throw new AssertionError(label);}
    static void near(float actual,float expected,String label){check(Math.abs(actual-expected)<.00001f*Math.max(1,Math.abs(expected)),label+": "+actual+" != "+expected);}
    public NativeMeleeProbe(){NeoForge.EVENT_BUS.addListener(NativeMeleeProbe::run);}
    static void state(HealthBridgeFixture peer,float add,float base,float total)throws Exception {
        var a=host.actors().getFirst();var bytes=peer.take(Wire.STATUS_MELEE).payload();check(bytes.length==44,"exact size");
        var r=new Wire.Reader(bytes);check(r.i64()==host.epoch(),"world");long next=r.i64();check(next>revision,"revision");revision=next;
        check(r.i32()==a.slot()&&r.i32()==a.serial()&&r.i32()==a.spawn()&&r.i32()==a.life(),"identity");
        near(r.f32(),add,"add");near(r.f32(),base,"base multiplier");near(r.f32(),total,"total multiplier");r.finish();packets++;
    }
    static void exercise(HealthBridgeFixture peer)throws Exception {
        fresh();revision=0;var a=actor(NONE,3,7,11,1,100,0);snapshot(a,100);NativePlayers.tick(server,host);
        var box=NativePlayers.hitbox(a);check(box!=null&&box.isAlive(),"native proxy");state(peer,0,0,1);
        box.addEffect(new MobEffectInstance(MobEffects.DAMAGE_BOOST,100,0));NativeMelee.update(a,box);state(peer,3,0,1);
        box.addEffect(new MobEffectInstance(MobEffects.DAMAGE_BOOST,100,1));NativeMelee.update(a,box);state(peer,6,0,1);
        box.addEffect(new MobEffectInstance(MobEffects.WEAKNESS,100,0));NativeMelee.update(a,box);state(peer,2,0,1);
        box.removeEffect(MobEffects.DAMAGE_BOOST);check(box.getAttributeValue(Attributes.ATTACK_DAMAGE)==0,"vanilla clamps proxy attack to zero");
        NativeMelee.update(a,box);state(peer,-4,0,1); // export modifier, not already-clamped value
        box.removeAllEffects();NativeMelee.update(a,box);state(peer,0,0,1);
        var attr=box.getAttribute(Attributes.ATTACK_DAMAGE);
        attr.addTransientModifier(new AttributeModifier(ResourceLocation.fromNamespaceAndPath("goldcraft","melee_add"),3,AttributeModifier.Operation.ADD_VALUE));
        attr.addTransientModifier(new AttributeModifier(ResourceLocation.fromNamespaceAndPath("goldcraft","melee_base"),.5,AttributeModifier.Operation.ADD_MULTIPLIED_BASE));
        attr.addTransientModifier(new AttributeModifier(ResourceLocation.fromNamespaceAndPath("goldcraft","melee_total"),.2,AttributeModifier.Operation.ADD_MULTIPLIED_TOTAL));
        NativeMelee.update(a,box);state(peer,3,.5f,1.2f);
        near((float)attr.getValue(),7.2f,"actual vanilla modifier order");
        var invalid=ResourceLocation.fromNamespaceAndPath("goldcraft","melee_invalid");
        attr.addTransientModifier(new AttributeModifier(invalid,Double.NaN,AttributeModifier.Operation.ADD_VALUE));
        NativeMelee.update(a,box);attr.removeModifier(invalid);NativeMelee.update(a,box);state(peer,3,.5f,1.2f);
        attr.removeModifiers();box.addEffect(new MobEffectInstance(MobEffects.DAMAGE_BOOST,1));
        NativeMelee.update(a,box);state(peer,3,0,1);step(box,1);NativeMelee.update(a,box);state(peer,0,0,1);
        peer.authority(false);box.addEffect(new MobEffectInstance(MobEffects.DAMAGE_BOOST,100));NativeMelee.update(a,box);
        peer.authority(true);NativeMelee.update(a,box);state(peer,3,0,1);

        fresh();revision=0;var p=player(new UUID(0,9071));a=actor(p.getUUID(),4,8,12,113,100,0);snapshot(a,100);
        p.addEffect(new MobEffectInstance(MobEffects.DAMAGE_BOOST,100));NativePlayers.tick(server,host);state(peer,0,0,1); // MC owns its own damage
        a=actor(p.getUUID(),4,8,13,17,100,0);snapshot(a,100);NativePlayers.tick(server,host);box=NativePlayers.hitbox(a);state(peer,3,0,1);
        check(box.hasEffect(MobEffects.DAMAGE_BOOST)&&!p.hasEffect(MobEffects.DAMAGE_BOOST),"single native effect owner");
        a=actor(p.getUUID(),4,8,14,113,100,0);snapshot(a,100);NativePlayers.tick(server,host);state(peer,0,0,1);
        check(p.hasEffect(MobEffects.DAMAGE_BOOST)&&box.isRemoved(),"MC handoff returns effect");
        mining(p);
    }
    static void mining(net.minecraft.server.level.ServerPlayer p){
        p.removeAllEffects();p.setOnGround(true);p.setItemSlot(EquipmentSlot.MAINHAND,new ItemStack(Items.DIAMOND_PICKAXE));
        var pos=new BlockPos(0,70,0);var stone=Blocks.STONE.defaultBlockState();
        float base=stone.getDestroyProgress(p,world,pos);check(base>0,"actual vanilla material/tool baseline");
        p.addEffect(new MobEffectInstance(MobEffects.DIG_SPEED,100,1));near(stone.getDestroyProgress(p,world,pos),base*1.4f,"haste II");
        p.addEffect(new MobEffectInstance(MobEffects.DIG_SLOWDOWN,100,0));near(stone.getDestroyProgress(p,world,pos),base*1.4f*.3f,"fatigue stacks after haste");
        p.removeAllEffects();p.addEffect(new MobEffectInstance(MobEffects.CONDUIT_POWER,100,0));near(stone.getDestroyProgress(p,world,pos),base*1.2f,"conduit mining");
        p.addEffect(new MobEffectInstance(MobEffects.DIG_SPEED,100,2));near(stone.getDestroyProgress(p,world,pos),base*1.6f,"max haste/conduit amplifier, not two multipliers");
        p.removeAllEffects();int[] events={0};Consumer<PlayerEvent.BreakSpeed> listener=e->{if(e.getEntity()==p){events[0]++;e.setNewSpeed(e.getOriginalSpeed()*.5f);}};
        NeoForge.EVENT_BUS.addListener(listener);
        try{near(stone.getDestroyProgress(p,world,pos),base*.5f,"actual NeoForge BreakSpeed consumer");check(events[0]==1,"one Mod callback");}
        finally{NeoForge.EVENT_BUS.unregister(listener);}
        p.setOnGround(false);near(stone.getDestroyProgress(p,world,pos),base/5,"airborne penalty retained");p.setOnGround(true);
    }
    static void run(ServerStartedEvent event){
        var result=new JsonObject();server=event.getServer();world=server.overworld();
        try{world.getChunk(0,0);bsp=Files.readAllBytes(FMLPaths.GAMEDIR.get().resolve("cs_assault.bsp"));
            try(var peer=new HealthBridgeFixture(Wire.STATUS_MELEE_CAP)){check(GoldCraft.meleeAvailable(),"negotiated capability");exercise(peer);}
            result.addProperty("passed",true);
        }catch(Throwable failure){result.addProperty("passed",false);result.addProperty("error",failure.toString());failure.printStackTrace();}
        finally{result.addProperty("checks",checks);result.addProperty("packets",packets);cleanup();
            try{Files.writeString(FMLPaths.GAMEDIR.get().resolve("native-melee.json"),result.toString());}
            catch(Exception failure){throw new RuntimeException(failure);}}
    }
}
