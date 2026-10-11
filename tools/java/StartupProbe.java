package dev.goldcraft.feedbackprobe;
import com.google.gson.JsonObject;
import com.mojang.authlib.GameProfile;
import net.minecraft.network.Connection;
import net.minecraft.network.PacketSendListener;
import net.minecraft.network.protocol.Packet;
import net.minecraft.network.protocol.PacketFlow;
import net.minecraft.network.protocol.common.ClientboundCustomPayloadPacket;
import net.minecraft.network.protocol.game.ClientboundBundlePacket;
import net.minecraft.server.level.ClientInformation;
import net.minecraft.server.level.ServerPlayer;
import net.minecraft.server.network.CommonListenerCookie;
import net.minecraft.server.network.ServerGamePacketListenerImpl;
import net.neoforged.fml.common.Mod;
import net.neoforged.fml.ModList;
import net.neoforged.fml.loading.FMLPaths;
import net.neoforged.neoforge.common.NeoForge;
import net.neoforged.neoforge.event.server.ServerStartedEvent;
import java.nio.file.Files;
import java.util.UUID;

@Mod("goldcraft_feedback_probe")
public class StartupProbe {
    static int bindings;
    static void record(Packet<?> p){
        if(p instanceof ClientboundBundlePacket b){for(var child:b.subPackets())record(child);}
        if(p instanceof ClientboundCustomPayloadPacket c&&c.payload().type().id().toString().equals("epicfight:client_bound_change_skill"))bindings++;
    }
    public StartupProbe(){NeoForge.EVENT_BUS.addListener(StartupProbe::run);}
    static void run(ServerStartedEvent e){
        var result=new JsonObject();boolean epic=ModList.get().isLoaded("epicfight");result.addProperty("epicPresent",epic);
        try{
            var server=e.getServer();var level=server.overworld();level.getChunk(0,0);
            var profile=new GameProfile(new UUID(0,7257),"StartupProbe");
            var player=new ServerPlayer(server,level,profile,ClientInformation.createDefault());
            player.connection=new ServerGamePacketListenerImpl(server,new Connection(PacketFlow.SERVERBOUND),player,CommonListenerCookie.createInitial(profile,false)){
                @Override public void send(Packet<?> p){record(p);}
                @Override public void send(Packet<?> p,PacketSendListener l){record(p);}
            };
            player.setPos(0,64,0);level.addNewPlayer(player);player.discard();
            if(epic&&bindings<5)throw new AssertionError("Expected transient bindings without NightFall");
            if(!epic&&bindings!=0)throw new AssertionError("Unexpected Epic packet without Epic");
            result.addProperty("passed",true);
        }catch(Throwable t){result.addProperty("passed",false);result.addProperty("error",t.toString());t.printStackTrace();}
        result.addProperty("skillBindings",bindings);
        try{Files.writeString(FMLPaths.GAMEDIR.get().resolve("skill-feedback.json"),result.toString());}catch(Exception x){throw new RuntimeException(x);}
    }
}
