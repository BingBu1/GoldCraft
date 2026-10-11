package dev.goldcraft.feedbackprobe;

import com.google.gson.*;
import com.mojang.authlib.GameProfile;
import net.minecraft.core.registries.BuiltInRegistries;
import net.minecraft.network.Connection;
import net.minecraft.network.PacketSendListener;
import net.minecraft.network.protocol.Packet;
import net.minecraft.network.protocol.PacketFlow;
import net.minecraft.network.protocol.common.ClientboundCustomPayloadPacket;
import net.minecraft.network.protocol.game.ClientboundBundlePacket;
import net.minecraft.network.protocol.game.ClientboundRespawnPacket;
import net.minecraft.server.MinecraftServer;
import net.minecraft.server.level.ClientInformation;
import net.minecraft.server.level.ServerPlayer;
import net.minecraft.server.network.CommonListenerCookie;
import net.minecraft.server.network.ServerGamePacketListenerImpl;
import net.minecraft.world.entity.Entity;
import net.minecraft.world.InteractionHand;
import net.minecraft.world.item.ItemStack;
import net.neoforged.fml.common.Mod;
import net.neoforged.fml.loading.FMLPaths;
import net.neoforged.neoforge.common.NeoForge;
import net.neoforged.neoforge.event.server.ServerStartedEvent;
import net.neoforged.neoforge.event.tick.ServerTickEvent;
import yesman.epicfight.world.capabilities.EpicFightCapabilities;
import yesman.epicfight.world.capabilities.entitypatch.player.ServerPlayerPatch;
import yesman.epicfight.skill.SkillSlots;
import yesman.epicfight.network.server.*;
import java.nio.file.Files;
import java.util.*;

@Mod("goldcraft_feedback_probe")
public class SkillFeedbackProbe {
    static final JsonArray packets=new JsonArray();
    static final JsonObject report=new JsonObject();
    static ServerPlayer pending;
    static int ticks;
    static String phase="setup";
    static class RecordingConnection extends Connection {
        final io.netty.channel.Channel transport=new io.netty.channel.embedded.EmbeddedChannel();
        RecordingConnection(){super(PacketFlow.SERVERBOUND);try{var f=Connection.class.getDeclaredField("channel");f.setAccessible(true);f.set(this,transport);}catch(Exception e){throw new RuntimeException(e);}}
        @Override public io.netty.channel.Channel channel(){return transport;}
        @Override public boolean isConnected(){return true;}
        @Override public boolean isMemoryConnection(){return true;}
        @Override public java.net.SocketAddress getRemoteAddress(){return new java.net.InetSocketAddress("127.0.0.1",1);}
        @Override public void send(Packet<?> p){record(p);}
        @Override public void send(Packet<?> p,PacketSendListener l){record(p);}
        @Override public void send(Packet<?> p,PacketSendListener l,boolean f){record(p);}
    }
    static class RecordingHandler extends ServerGamePacketListenerImpl {
        RecordingHandler(MinecraftServer server,ServerPlayer player,GameProfile profile){super(server,new RecordingConnection(),player,CommonListenerCookie.createInitial(profile,false));}
        // Capture just before NeoForge's negotiated-channel transport boundary.
        // This fixture tests real producer ordering, not a network handshake.
        @Override public void send(Packet<?> p){record(p);}
        @Override public void send(Packet<?> p,PacketSendListener l){record(p);}
    }
    static void record(Packet<?> p){
        if(p instanceof ClientboundBundlePacket bundle){for(var child:bundle.subPackets())record(child);return;}
        JsonObject row=new JsonObject();row.addProperty("phase",phase);row.addProperty("packet",p.getClass().getSimpleName());
        if(p instanceof ClientboundCustomPayloadPacket custom){
            var d=custom.payload();row.addProperty("payload",d.type().id().toString());
            if(d instanceof SPSkillFeedback f){row.addProperty("slot",f.skillSlot().toString());row.addProperty("type",f.feedbackType().toString());row.addProperty("arguments",String.valueOf(f.arguments()));}
            if(d instanceof SPChangeSkill s){row.addProperty("slot",s.skillSlot().toString());row.addProperty("entityId",s.entityId());row.addProperty("skill",s.skill()==null?"null":s.skill().value().toString());}
            if(d instanceof SPSetSkillContainerValue s){row.addProperty("slot",s.skillSlot().toString());row.addProperty("entityId",s.entityId());row.addProperty("target",s.target().toString());row.addProperty("boolVal",s.boolVal());}
            if(d instanceof SPInitSkills s){row.addProperty("skills",s.serializedSkill().toString());var slots=new JsonObject();var nbt=s.serializedSkill().getCompound("playerSkills");for(var key:nbt.getAllKeys())if(nbt.contains(key,8))slots.addProperty(key.toUpperCase(java.util.Locale.ROOT),nbt.getString(key));row.add("slots",slots);}
        }
        if(p instanceof ClientboundRespawnPacket r)row.addProperty("keepAllData",r.shouldKeep((byte)3));
        packets.add(row);
    }
    public SkillFeedbackProbe(){NeoForge.EVENT_BUS.addListener(SkillFeedbackProbe::run);NeoForge.EVENT_BUS.addListener(SkillFeedbackProbe::tick);}
    static void finish(){report.add("packets",packets);try{Files.writeString(FMLPaths.GAMEDIR.get().resolve("skill-feedback.json"),report.toString());}catch(Exception e){throw new RuntimeException(e);}}
    static void equip(ServerPlayer player,String itemName){
        var patch=EpicFightCapabilities.getEntityPatch(player,ServerPlayerPatch.class);
        var previous=player.getMainHandItem();
        var item=BuiltInRegistries.ITEM.get(net.minecraft.resources.ResourceLocation.fromNamespaceAndPath("efn",itemName));
        if(item==net.minecraft.world.item.Items.AIR)throw new AssertionError("Unknown weapon "+itemName);
        var weapon=new ItemStack(item);player.setItemInHand(InteractionHand.MAIN_HAND,weapon);
        patch.updateHeldItem(EpicFightCapabilities.getItemStackCapability(previous),EpicFightCapabilities.getItemStackCapability(weapon),previous,weapon,InteractionHand.MAIN_HAND);
    }
    static void tick(ServerTickEvent.Post event){
        if(pending==null)return;
        ticks++;phase="natural-tick-"+ticks;
        try{
            var patch=EpicFightCapabilities.getEntityPatch(pending,ServerPlayerPatch.class);
            if(ticks==20){
                report.addProperty("after20TicksSkill",String.valueOf(patch.getSkill(SkillSlots.WEAPON_INNATE).getSkill()));
                report.addProperty("after20TicksDisabled",patch.getSkill(SkillSlots.WEAPON_INNATE).isDisabled());
                report.addProperty("after20TicksItem",pending.getMainHandItem().toString());
                phase="swap-b";equip(pending,"hf_murasama");phase="swap-a";equip(pending,"yamato_dmc");
                var c=patch.getSkill(SkillSlots.WEAPON_INNATE);
                phase="disabled-join";c.setDisabled(true);patch.onJoinWorld(pending,pending.level(),false);
                phase="forced-init";c.setSkill(c.getSkill(),true);
                report.addProperty("initializeResetsDisabled",!c.isDisabled());
                phase="null-join";c.setSkill(null);patch.onJoinWorld(pending,pending.level(),false);
                phase="equip-before-dimension";equip(pending,"yamato_dmc");
                c.setDisabled(true);c.setResource(7);c.setStack(1);
                phase="disable-before-dimension";
                yesman.epicfight.network.EpicFightNetworkManager.sendToPlayer(SPSetSkillContainerValue.enable(c.getSlot(),true,pending.getId()),pending);
                phase="dimension";
                pending=(ServerPlayer)pending.changeDimension(new net.minecraft.world.level.portal.DimensionTransition(pending.server.getLevel(net.minecraft.world.level.Level.NETHER),new net.minecraft.world.phys.Vec3(0,80,0),net.minecraft.world.phys.Vec3.ZERO,0,0,net.minecraft.world.level.portal.DimensionTransition.DO_NOTHING));
                var after=EpicFightCapabilities.getEntityPatch(pending,ServerPlayerPatch.class).getSkill(SkillSlots.WEAPON_INNATE);
                report.addProperty("dimensionSkill",String.valueOf(after.getSkill()));report.addProperty("dimensionDisabled",after.isDisabled());
            }else if(ticks==40){
                phase="no-keep-skills";
                yesman.epicfight.world.gamerule.EpicFightGameRules.KEEP_SKILLS.setRuleValue(pending.level(),false);
                pending=pending.server.getPlayerList().respawn(pending,false,Entity.RemovalReason.KILLED);pending.connection.player=pending;
                report.addProperty("noKeepSkillsInnate",String.valueOf(EpicFightCapabilities.getEntityPatch(pending,ServerPlayerPatch.class).getSkill(SkillSlots.WEAPON_INNATE).getSkill()));
            }else if(ticks==60){
                finish();pending.server.getPlayerList().remove(pending);pending=null;
            }
        }catch(Throwable e){report.addProperty("passed",false);report.addProperty("error",e.toString());e.printStackTrace();finish();pending=null;}
    }
    static void run(ServerStartedEvent event){
        try{
            MinecraftServer server=event.getServer();var level=server.overworld();level.getChunk(0,0);
            var profile=new GameProfile(new UUID(0,7256),"FeedbackProbe");
            var player=new ServerPlayer(server,level,profile,ClientInformation.createDefault());
            player.connection=new RecordingHandler(server,player,profile);
            player.setPos(0,64,0);level.addNewPlayer(player);
            report.addProperty("playerId",player.getId());
            var patch=EpicFightCapabilities.getEntityPatch(player,ServerPlayerPatch.class);
            if(patch==null)throw new AssertionError("Missing Epic server patch");
            var item=BuiltInRegistries.ITEM.entrySet().stream().filter(e->e.getKey().location().getNamespace().equals("efn")&&e.getKey().location().getPath().equals("yamato_dmc")).findFirst().orElseThrow();
            var weapon=new ItemStack(item.getValue());
            phase="equip";player.setItemInHand(InteractionHand.MAIN_HAND,weapon);
            patch.updateHeldItem(EpicFightCapabilities.getItemStackCapability(ItemStack.EMPTY),EpicFightCapabilities.getItemStackCapability(weapon),ItemStack.EMPTY,weapon,InteractionHand.MAIN_HAND);
            report.addProperty("initialSkill",String.valueOf(patch.getSkill(SkillSlots.WEAPON_INNATE).getSkill()));
            report.addProperty("initialDisabled",patch.getSkill(SkillSlots.WEAPON_INNATE).isDisabled());
            phase="respawn";var replacement=server.getPlayerList().respawn(player,false,Entity.RemovalReason.KILLED);
            replacement.connection.player=replacement;
            var nextPatch=EpicFightCapabilities.getEntityPatch(replacement,ServerPlayerPatch.class);
            report.addProperty("respawnSkill",String.valueOf(nextPatch.getSkill(SkillSlots.WEAPON_INNATE).getSkill()));
            report.addProperty("respawnDisabled",nextPatch.getSkill(SkillSlots.WEAPON_INNATE).isDisabled());
            report.addProperty("respawnItem",replacement.getMainHandItem().toString());
            phase="idle";
            var f=yesman.epicfight.world.capabilities.entitypatch.player.PlayerPatch.class.getDeclaredField("tickSinceLastAction");f.setAccessible(true);f.setInt(nextPatch,200);
            var c=nextPatch.getSkill(SkillSlots.WEAPON_INNATE);
            if(c.getSkill()!=null)for(int n=0;n<4;n++)c.getSkill().updateContainer(c);
            report.addProperty("passed",true);
            report.add("packets",packets);Files.writeString(FMLPaths.GAMEDIR.get().resolve("skill-feedback-partial.json"),report.toString());
            pending=replacement;
        }catch(Throwable e){report.addProperty("passed",false);report.addProperty("error",e.toString());e.printStackTrace();finish();}
    }
}
