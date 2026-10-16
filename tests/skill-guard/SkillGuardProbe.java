package dev.goldcraft.skillguardprobe;

import com.google.gson.JsonObject;
import com.mojang.authlib.GameProfile;
import dev.goldcraft.world.NativeArmorDefense;
import dev.goldcraft.world.NativeSkillGuard;
import net.minecraft.network.Connection;
import net.minecraft.network.PacketSendListener;
import net.minecraft.network.protocol.Packet;
import net.minecraft.network.protocol.PacketFlow;
import net.minecraft.resources.ResourceLocation;
import net.minecraft.core.registries.BuiltInRegistries;
import net.minecraft.server.level.ClientInformation;
import net.minecraft.server.level.ServerPlayer;
import net.minecraft.server.network.CommonListenerCookie;
import net.minecraft.server.network.ServerGamePacketListenerImpl;
import net.minecraft.world.entity.EquipmentSlot;
import net.minecraft.world.effect.MobEffectInstance;
import net.minecraft.world.item.ItemStack;
import net.minecraft.world.item.Items;
import net.neoforged.fml.ModList;
import net.neoforged.fml.common.Mod;
import net.neoforged.fml.loading.FMLPaths;
import net.neoforged.neoforge.common.NeoForge;
import net.neoforged.neoforge.event.entity.living.LivingIncomingDamageEvent;
import net.neoforged.neoforge.event.server.ServerStartedEvent;
import java.nio.file.Files;
import java.util.UUID;

@Mod("goldcraft_skill_guard_probe")
public class SkillGuardProbe {
    static int checks,events;
    static ServerPlayer target;
    static boolean cancel,halve;
    static void check(boolean ok,String why){checks++;if(!ok)throw new AssertionError(why);}
    public SkillGuardProbe(){NeoForge.EVENT_BUS.addListener(SkillGuardProbe::run);NeoForge.EVENT_BUS.addListener(SkillGuardProbe::incoming);}
    static void incoming(LivingIncomingDamageEvent e){if(e.getEntity()==target){events++;if(cancel)e.setCanceled(true);if(halve)e.setAmount(e.getAmount()/2);}}
    static class QuietConnection extends Connection {
        final io.netty.channel.Channel transport=new io.netty.channel.embedded.EmbeddedChannel();
        QuietConnection(){super(PacketFlow.SERVERBOUND);try{var f=Connection.class.getDeclaredField("channel");f.setAccessible(true);f.set(this,transport);}catch(Exception e){throw new RuntimeException(e);}}
        @Override public io.netty.channel.Channel channel(){return transport;}
        @Override public boolean isConnected(){return true;}
        @Override public boolean isMemoryConnection(){return true;}
        @Override public java.net.SocketAddress getRemoteAddress(){return new java.net.InetSocketAddress("127.0.0.1",1);}
        @Override public void send(Packet<?> p){}
        @Override public void send(Packet<?> p,PacketSendListener l){}
        @Override public void send(Packet<?> p,PacketSendListener l,boolean f){}
    }
    static ServerPlayer player(net.minecraft.server.MinecraftServer server,String name,long id){
        var profile=new GameProfile(new UUID(0,id),name);var p=new ServerPlayer(server,server.overworld(),profile,ClientInformation.createDefault());
        p.connection=new ServerGamePacketListenerImpl(server,new QuietConnection(),p,CommonListenerCookie.createInitial(profile,false)){
            @Override public void send(Packet<?> packet){}
            @Override public void send(Packet<?> packet,PacketSendListener listener){}
        };
        p.setPos(0,70,0);server.overworld().addNewPlayer(p);
        for(int i=0;i<65;i++)p.tick();p.doTick();p.invulnerableTime=0;
        return p;
    }
    static void run(ServerStartedEvent e){
        var report=new JsonObject();ServerPlayer attacker=null;
        try{
            var server=e.getServer();server.overworld().getChunk(0,0);
            target=player(server,"GuardTarget",87271);attacker=player(server,"GuardAttacker",87272);
            boolean epic=ModList.get().isLoaded("epicfight");report.addProperty("epic",epic);
            var source=target.damageSources().playerAttack(attacker);
            check(!target.isInvulnerableTo(source),"fixture join immunity");
            check(!NativeSkillGuard.infectionProtected(target),"idle is not skill immune");
            float health=target.getHealth();
            check(NativeArmorDefense.reduce(target,source,40).afterArmor()==40,"normal unarmored damage");
            check(target.getHealth()==health,"bridge changes health");
            int before=events;cancel=true;
            check(NativeArmorDefense.reduce(target,source,40).afterArmor()==0,"real incoming cancellation");cancel=false;
            check(events==before+1,"incoming event must run once");
            halve=true;check(NativeArmorDefense.reduce(target,source,40).afterArmor()==20,"incoming amount reduction");halve=false;
            target.setItemSlot(EquipmentSlot.CHEST,new ItemStack(Items.DIAMOND_CHESTPLATE));target.doTick();
            var chest=target.getItemBySlot(EquipmentSlot.CHEST);chest.setDamageValue(0);
            cancel=true;check(NativeArmorDefense.reduce(target,source,40).afterArmor()==0,"cancel armored hit");cancel=false;
            check(chest.getDamageValue()==0,"cancel must not wear armor");
            if(epic)EpicProbe.exercise(target,attacker,report);
            else{
                check(!NativeSkillGuard.infectionProtected(target),"no Epic optional loading");
                check(NativeArmorDefense.reduce(target,source,40).afterArmor()>0,"no Epic normal equipment damage");
            }
            check(target.getHealth()==health,"accepted native defense never applies health twice");
            target.setHealth(0);check(!NativeSkillGuard.infectionProtected(target),"dead player must not publish immunity");
            report.addProperty("passed",true);
        }catch(Throwable t){report.addProperty("passed",false);report.addProperty("error",t.toString());t.printStackTrace();}
        finally{
            if(target!=null)target.discard();if(attacker!=null)attacker.discard();
            report.addProperty("checks",checks);report.addProperty("incomingEvents",events);
            try{Files.writeString(FMLPaths.GAMEDIR.get().resolve("skill-guard.json"),report.toString());}catch(Exception x){throw new RuntimeException(x);}
        }
    }
}

class EpicProbe {
    static void exercise(ServerPlayer target,ServerPlayer attacker,JsonObject report){
        var patch=yesman.epicfight.world.capabilities.EpicFightCapabilities.getServerPlayerPatch(target);
        SkillGuardProbe.check(patch!=null,"actual Epic server capability");
        var source=target.damageSources().playerAttack(attacker);
        var skill=com.hm.efn.gameasset.EFNAnimations.DMC5_V_JC;
        patch.playAnimationInstantly(skill);
        patch.updateEntityState();
        SkillGuardProbe.check(!patch.getEntityState().attackResult(source).dealtDamage(),"actual JCE animation predicate");
        SkillGuardProbe.check(NativeSkillGuard.infectionProtected(target),"JCE infection guard");
        var chest=target.getItemBySlot(EquipmentSlot.CHEST);chest.setDamageValue(0);
        SkillGuardProbe.check(NativeArmorDefense.reduce(target,source,40).afterArmor()==0,"JCE blocks native melee");
        SkillGuardProbe.check(chest.getDamageValue()==0,"JCE must not consume durability");
        var bullet=new net.minecraft.world.damagesource.DamageSource(target.registryAccess()
            .registryOrThrow(net.minecraft.core.registries.Registries.DAMAGE_TYPE)
            .getHolderOrThrow(net.minecraft.world.damagesource.DamageTypes.ARROW),attacker,attacker);
        SkillGuardProbe.check(NativeArmorDefense.reduce(target,bullet,40).afterArmor()==0,"JCE blocks native gun mapping");
        SkillGuardProbe.check(patch.getEntityState().attackResult(target.damageSources().fall()).dealtDamage(),"JCE preserves environmental source semantics");
        SkillGuardProbe.check(NativeArmorDefense.reduce(target,target.damageSources().fall(),40).afterArmor()>0,"JCE does not grant invented fall immunity");
        patch.playAnimationInstantly(yesman.epicfight.gameasset.Animations.BIPED_IDLE);
        patch.updateEntityState();
        SkillGuardProbe.check(!NativeSkillGuard.infectionProtected(target),"interrupt removes JCE guard");
        SkillGuardProbe.check(NativeArmorDefense.reduce(target,source,40).afterArmor()>0,"normal damage restored after skill");
        patch.playAnimationInstantly(skill);
        var animator=patch.getServerAnimator();
        for(int tick=0;tick<400&&animator.animationPlayer.getAnimation()==skill;tick++)animator.tick();
        animator.tick();patch.updateEntityState();
        SkillGuardProbe.check(animator.animationPlayer.getAnimation()!=skill,"skill naturally ends within test bound");
        SkillGuardProbe.check(!NativeSkillGuard.infectionProtected(target),"natural animation completion clears guard");
        var effect=BuiltInRegistries.MOB_EFFECT.getHolder(ResourceLocation.fromNamespaceAndPath("efn","invincibility")).orElseThrow();
        target.addEffect(new MobEffectInstance(effect,4,0));chest.setDamageValue(0);
        SkillGuardProbe.check(NativeSkillGuard.infectionProtected(target),"NightFall effect infection guard");
        SkillGuardProbe.check(NativeArmorDefense.reduce(target,source,40).afterArmor()==0,"NightFall actual event cancellation");
        SkillGuardProbe.check(NativeArmorDefense.reduce(target,target.damageSources().fall(),40).afterArmor()==0,"NightFall effect environmental immunity");
        SkillGuardProbe.check(chest.getDamageValue()==0,"NightFall effect no wear");
        target.removeEffect(effect);
        SkillGuardProbe.check(!NativeSkillGuard.infectionProtected(target),"effect removal clears guard");
        SkillGuardProbe.check(NativeArmorDefense.reduce(target,source,40).afterArmor()>0,"effect removal restores damage");
        report.addProperty("actualSkill","efn:biped/yamato_judgementcut_end");
    }
}
