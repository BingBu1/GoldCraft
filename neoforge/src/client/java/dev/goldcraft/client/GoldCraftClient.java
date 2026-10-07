package dev.goldcraft.client;

import dev.goldcraft.GoldCraft;
import dev.goldcraft.bridge.BridgeLink;
import dev.goldcraft.bridge.Wire;
import dev.goldcraft.bridge.Performance;
import dev.goldcraft.net.BindingPayload;
import dev.goldcraft.net.HostPayload;
import dev.goldcraft.net.HostAssembler;
import dev.goldcraft.bridge.HostWorldState;
import dev.goldcraft.world.HostCollision;
import net.minecraft.client.world.ClientWorld;

public final class GoldCraftClient {
    private static GoldCraftClient active;
    private BridgeLink link;
    private WorldExporter exporter;
    private HudExporter hud;
    private byte[] binding;
    private long generation, lastBindingSend;
    public static final HostWorldState HOST=new HostWorldState();
    private final HostAssembler assembler=new HostAssembler();
    private ClientWorld attachedWorld;
    private long tickStarted;
    private static long skippedStandaloneFrames;
    public static boolean presentationOnly(net.minecraft.client.MinecraftClient client){
        return active!=null&&active.hud!=null&&HostInput.hosted()&&!HostAudio.standaloneFocused(client)
            &&client.getOverlay()==null&&client.world!=null&&client.player!=null;
    }
    public static void skippedStandaloneFrame(){skippedStandaloneFrames++;}
    public static void presentationDiagnostics(com.google.gson.JsonObject data){
        data.addProperty("presentationOnly",presentationOnly(net.minecraft.client.MinecraftClient.getInstance()));
        data.addProperty("skippedStandaloneFrames",skippedStandaloneFrames);
        data.addProperty("standaloneFocused",HostAudio.standaloneFocused(net.minecraft.client.MinecraftClient.getInstance()));
        data.addProperty("standaloneLoaded",net.minecraft.client.MinecraftClient.getInstance().isFinishedLoading());
    }
    public void initialize() {
        active=this;
        dev.goldcraft.net.HostNetwork.clientReceiver(part-> {
            try {
                Wire.Message message=assembler.accept(part);if(message==null)return;
                switch(message.type()) {
                    case Wire.WORLD -> HOST.world(message.payload());
                    case Wire.BSP -> {HOST.bsp(message.payload());GoldCraft.LOGGER.info("Client received host BSP: map={} CRC32={} models={}",HOST.map(),HOST.geometry().crc(),HOST.geometry().modelCount());}
                    case Wire.ACTORS -> HOST.actors(message.payload());
                    case Wire.BRUSHES -> HOST.brushes(message.payload());
                    default -> { }
                }
            }catch(IllegalArgumentException e){GoldCraft.LOGGER.warn("Rejected host map stream: {}",e.getMessage());assembler.clear();HOST.clear();}
        });
        try {
            BridgeLink.Config.environment("GOLDCRAFT_CLIENT",Wire.FABRIC_CLIENT,Wire.HOST_CLIENT).ifPresent(config->{
                link=new BridgeLink(config);HostInput.configure(link);HostAudio.configure();exporter=new WorldExporter(link);hud=new HudExporter(link); GoldCraft.LOGGER.info("Local CS client link configured on loopback port {}",config.port());
            });
        } catch (IllegalArgumentException e) { GoldCraft.LOGGER.error("Invalid local-client configuration: {}",e.getMessage()); }
        var events=net.neoforged.neoforge.common.NeoForge.EVENT_BUS;
        events.addListener((net.neoforged.neoforge.event.GameShuttingDownEvent event)->{ if(hud!=null)hud.close();if(link!=null)link.close(); });
        events.addListener((net.neoforged.neoforge.client.event.RenderLevelStageEvent event)->{
            if(event.getStage()==net.neoforged.neoforge.client.event.RenderLevelStageEvent.Stage.AFTER_LEVEL&&exporter!=null)exporter.renderFrame();
        });
        events.addListener((net.neoforged.neoforge.client.event.ClientTickEvent.Pre event)->{
            var client=net.minecraft.client.MinecraftClient.getInstance();
            tickStarted=Performance.begin();
            if(client.world!=attachedWorld){attachedWorld=client.world;if(attachedWorld!=null)HostCollision.attach(attachedWorld,HOST);else{assembler.clear();HOST.clear();}}
            if (link==null) return;
            if (!link.connected()) { binding=null;HostInput.clear(); exporter.reset();hud.reset(); return; }
            if (generation!=link.generation()) { generation=link.generation(); binding=null;HostInput.clear(); exporter.reset();hud.reset(); GoldCraft.LOGGER.info("Paired local CS client transport, generation {}",generation); }
            for (Wire.Message message:link.drain()) {
                try {
                    if(message.type()==Wire.CLIENT_BINDING&&message.payload().length==32) {binding=message.payload();exporter.bind(new Wire.Reader(binding).i64());lastBindingSend=0;}
                    else if(message.type()==Wire.INPUT)HostInput.input(message.payload());
                    else if(message.type()==Wire.CONTROL)HostInput.action(message.payload());
                    else if(message.type()==Wire.KEY_INPUT)HostKeys.input(message.payload());
                    else if(message.type()==Wire.VIEWPORT)hud.viewport(message.payload());
                    else if(message.type()==Wire.UI_INPUT)HostUi.input(message.payload());
                    else if(message.type()==Wire.SCENE_RESET&&message.payload().length==8)exporter.resendScene(new Wire.Reader(message.payload()).i64());
                }catch(IllegalArgumentException e){GoldCraft.LOGGER.warn("Rejected local host input: {}",e.getMessage());}
            }
            // Retry across Minecraft server reconnects. Native binding is idempotent for the same player UUID.
            if (binding!=null && client.player!=null && client.getNetworkHandler()!=null && client.getNetworkHandler().hasChannel(BindingPayload.ID)
                && System.nanoTime()-lastBindingSend>2_000_000_000L) {
                net.neoforged.neoforge.network.PacketDistributor.sendToServer(new BindingPayload(binding)); lastBindingSend=System.nanoTime();
            }
            // Client ticks still run with the Minecraft window behind CS or minimized.
            // This thread also owns the GL context; world rendering callbacks alone can stop.
            HostInput.tick(client);
        });
        events.addListener((net.neoforged.neoforge.client.event.ClientTickEvent.Post event)->{
            var client=net.minecraft.client.MinecraftClient.getInstance();
            if(link!=null)HostFootsteps.tick(client);
            // Send the completed simulation pose before potentially expensive GPU/mesh export.
            if(exporter!=null){HostInput.sendPose(client);exporter.renderFrame();HostInput.writeDiagnostics(client,hud);}
            Performance.end("clientTick",tickStarted);Performance.flush();
        });
        GoldCraft.LOGGER.info("GoldCraft client initialized");
    }
    public static void presentationFrame(net.minecraft.client.MinecraftClient client) {
        if(active!=null&&active.hud!=null){HostCamera.frame(client,active.link);active.hud.frame(client);active.exporter.presentationFrame();}
    }
}
