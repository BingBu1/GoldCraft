package dev.goldcraft.client;

import dev.goldcraft.GoldCraft;
import dev.goldcraft.bridge.BridgeLink;
import dev.goldcraft.bridge.Wire;
import dev.goldcraft.bridge.Performance;
import dev.goldcraft.net.BindingPayload;
import dev.goldcraft.net.HostPayload;
import dev.goldcraft.net.HostAssembler;
import dev.goldcraft.bridge.HostWorldState;
import dev.goldcraft.bridge.MapEdits;
import dev.goldcraft.net.EditRequestPayload;
import dev.goldcraft.world.HostCollision;
import net.minecraft.client.world.ClientWorld;

public final class GoldCraftClient {
    private static GoldCraftClient active;
    private BridgeLink link;
    private LocalClientDiscovery discovery;
    private LocalClientDiscovery.Endpoint discoveredEndpoint;
    private long discoveryWorld;
    private long discoveryDeadline;
    private Thread retiringLink;
    private String pairingProblem="The local CS bridge is not configured.";
    private WorldExporter exporter;
    private HudExporter hud;
    private byte[] binding;
    private long generation, lastBindingSend;
    private long nextEditQuery;
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
        dev.goldcraft.world.NativeHealth.clientOwnership(entity->{
            if(HOST.epoch()==0||entity.isRemoved())return false;
            if(entity instanceof dev.goldcraft.world.NativePlayerHitboxEntity box)
                return box.hostEpoch()==HOST.epoch()&&HOST.actors().stream().anyMatch(a->a.slot()==box.hostSlot()&&a.serial()==box.hostSerial()&&a.life()==box.hostLife());
            return entity instanceof net.minecraft.entity.player.PlayerEntity&&HOST.actor(entity.getUuid())!=null;
        });
        dev.goldcraft.net.HostNetwork.clientBinding(()->link!=null&&link.connected()&&link.generation()==generation&&binding!=null?binding.clone():null);
        dev.goldcraft.net.HostNetwork.clientPairingProblem(this::pairingProblem);
        dev.goldcraft.net.HostNetwork.clientReceiver(part-> {
            try {
                Wire.Message message=assembler.accept(part);if(message==null)return;
                switch(message.type()) {
                    case Wire.WORLD -> HOST.world(message.payload());
                    case Wire.BSP -> {HOST.bsp(message.payload());GoldCraft.LOGGER.info("Client received host BSP: map={} CRC32={} models={}",HOST.map(),HOST.geometry().crc(),HOST.geometry().modelCount());}
                    case Wire.ACTORS -> HOST.actors(message.payload());
                    case Wire.BRUSHES -> HOST.brushes(message.payload());
                    case Wire.MAP_MINING_POLICY -> HOST.mining(message.payload());
                    case Wire.MAP_EDIT_SNAPSHOT -> HOST.edits().accept(MapEdits.snapshot(message.payload()));
                    case Wire.MAP_EDIT_DELTA -> HOST.edits().accept(MapEdits.delta(message.payload()));
                    case Wire.MAP_EDIT_BATCH -> HOST.edits().accept(MapEdits.batch(message.payload()));
                    default -> { }
                }
            }catch(IllegalArgumentException e){
                GoldCraft.LOGGER.warn("Rejected host map stream: {}",e.getMessage());assembler.clear();
                if(part.type()==Wire.MAP_EDIT_SNAPSHOT||part.type()==Wire.MAP_EDIT_DELTA||part.type()==Wire.MAP_EDIT_BATCH)HOST.edits().invalidate();else HOST.clear();
            }
        });
        try {
            var config=BridgeLink.Config.environment("GOLDCRAFT_CLIENT",Wire.FABRIC_CLIENT,Wire.HOST_CLIENT);
            if(config.isPresent())attach(config.get());
            else discovery=new LocalClientDiscovery(Wire.VERSION);
        } catch (IllegalArgumentException e) {
            // Partial explicit configuration is an error, never permission to select another instance.
            pairingProblem="Invalid local CS pairing configuration; relaunch this CS/Minecraft pair together.";
            GoldCraft.LOGGER.error("Invalid local-client configuration: {}",e.getMessage());
        }
        var events=net.neoforged.neoforge.common.NeoForge.EVENT_BUS;
        events.addListener((net.neoforged.neoforge.event.GameShuttingDownEvent event)->{ if(discovery!=null)discovery.close();if(hud!=null)hud.close();DepthCopy.close();if(link!=null)link.close(); });
        events.addListener((net.neoforged.neoforge.client.event.RenderLevelStageEvent event)->{
            if(event.getStage()==net.neoforged.neoforge.client.event.RenderLevelStageEvent.Stage.AFTER_LEVEL&&exporter!=null)exporter.renderFrame();
        });
        events.addListener((net.neoforged.neoforge.client.event.ClientTickEvent.Pre event)->{
            var client=net.minecraft.client.MinecraftClient.getInstance();
            tickStarted=Performance.begin();
            discover();
            if(client.world!=attachedWorld){attachedWorld=client.world;if(attachedWorld!=null)HostCollision.attach(attachedWorld,HOST);else{assembler.clear();HOST.clear();}}
            if(HOST.epoch()!=0&&!HOST.edits().ready()&&client.getNetworkHandler()!=null&&
                client.getNetworkHandler().hasChannel(EditRequestPayload.ID)&&System.nanoTime()>=nextEditQuery){
                net.neoforged.neoforge.network.PacketDistributor.sendToServer(new EditRequestPayload(HOST.epoch()));
                nextEditQuery=System.nanoTime()+1_000_000_000L;
            }
            if (link==null) {dev.goldcraft.net.HostNetwork.tickAdmission();return;}
            if (!link.connected()) { binding=null;HostInput.clear(); exporter.reset();hud.reset();dev.goldcraft.net.HostNetwork.tickAdmission();return; }
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
            dev.goldcraft.net.HostNetwork.tickAdmission();
            if (binding!=null && client.player!=null && client.getNetworkHandler()!=null && client.getNetworkHandler().hasChannel(BindingPayload.ID)
                && System.nanoTime()-lastBindingSend>2_000_000_000L) {
                net.neoforged.neoforge.network.PacketDistributor.sendToServer(new BindingPayload(binding)); lastBindingSend=System.nanoTime();
            }
            // Client ticks still run with the Minecraft window behind CS or minimized.
            // This thread also owns the GL context; world rendering callbacks alone can stop.
            HostInput.tick(client);
            HostMiningInput.tick(client);
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
    private void attach(BridgeLink.Config config){
        link=new BridgeLink(config);HostInput.configure(link);HostAudio.configure();
        exporter=new WorldExporter(link);hud=new HudExporter(link);
        GoldCraft.LOGGER.info("Local CS client link configured on loopback port {}",config.port());
    }
    private void detach(){
        BridgeLink previous=link;link=null;binding=null;generation=lastBindingSend=0;
        HostInput.clear();HostInput.configure(null);HostUi.configure(null);
        if(exporter!=null){exporter.reset();exporter=null;}
        if(hud!=null){hud.close();hud=null;}
        if(previous!=null){
            // BridgeLink.close joins its worker. Never put that wait on the render thread.
            retiringLink=new Thread(previous::close,"GoldCraft-retire-local-link");
            retiringLink.setDaemon(true);retiringLink.start();
        }
        discoveredEndpoint=null;
    }
    private void discover(){
        if(discovery==null)return;
        long challenge=dev.goldcraft.net.HostNetwork.admissionWorld();
        if(challenge!=0){discoveryWorld=challenge;discoveryDeadline=System.nanoTime()+15_000_000_000L;}
        var client=net.minecraft.client.MinecraftClient.getInstance();
        boolean hosted=client.getNetworkHandler()!=null&&client.getNetworkHandler().hasChannel(BindingPayload.ID);
        if(challenge==0&&!hosted&&(!dev.goldcraft.net.HostNetwork.pairingConnectionOpen()||
            client.world!=null||System.nanoTime()-discoveryDeadline>=0)){
            discoveryWorld=0;discovery.request(0,null);if(link!=null)detach();return;
        }
        if(discoveryWorld==0)return;
        boolean keepTransport=hosted&&challenge==0;
        if(!keepTransport&&discoveredEndpoint!=null&&discoveredEndpoint.world()!=discoveryWorld)detach();
        discovery.request(discoveryWorld,discoveredEndpoint,keepTransport,dev.goldcraft.net.HostNetwork.pairingAttempt());
        var result=discovery.result();
        // A new request has no answer yet, not a confirmed loss of this pair.
        if(result.pending())return;
        var endpoint=result.endpoint();pairingProblem=result.reason();
        // A result from the previous server must not be used during a new admission.
        if(endpoint!=null&&endpoint.world()!=discoveryWorld&&
            !(keepTransport&&discoveredEndpoint!=null&&discoveredEndpoint.sameTransport(endpoint)))endpoint=null;
        if(discoveredEndpoint!=null&&(endpoint==null||!discoveredEndpoint.sameTransport(endpoint)))detach();
        if(link==null&&endpoint!=null&&(retiringLink==null||!retiringLink.isAlive())){
            attach(new BridgeLink.Config(endpoint.port(),Wire.key(endpoint.session()),Wire.key(endpoint.token()),Wire.FABRIC_CLIENT,Wire.HOST_CLIENT));
            discoveredEndpoint=endpoint;
        }
    }
    private String pairingProblem(){
        if(link==null)return pairingProblem;
        if(!link.connected())return "The matching local CS bridge is not connected; check that CS and Minecraft use matching GoldCraft versions.";
        if(binding==null)return "The CS bridge is connected, but CS has no current server pairing; join the matching GoldCraft CS server.";
        return "The CS client is paired with a different server or map session; reconnect to the matching server.";
    }
    public static void presentationFrame(net.minecraft.client.MinecraftClient client) {
        if(active!=null&&active.hud!=null){ViewDistortionExporter.frame(client,active.link);HostCamera.frame(client,active.link);active.hud.frame(client);active.exporter.presentationFrame();}
    }
}
