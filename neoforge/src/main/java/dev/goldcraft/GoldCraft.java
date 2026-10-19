package dev.goldcraft;

import dev.goldcraft.bridge.BridgeLink;
import dev.goldcraft.bridge.Wire;
import dev.goldcraft.bridge.Performance;
import dev.goldcraft.bridge.HostWorldState;
import dev.goldcraft.bridge.HostEffectGrant;
import dev.goldcraft.bridge.HostDeliveryQueue;
import dev.goldcraft.bridge.MapEdits;
import dev.goldcraft.net.BindingPayload;
import dev.goldcraft.net.CsAdmission;
import dev.goldcraft.net.HostPayload;
import dev.goldcraft.net.ControlPayload;
import dev.goldcraft.world.HostCollision;
import dev.goldcraft.world.TraceValidation;
import dev.goldcraft.world.HostSession;
import dev.goldcraft.world.HostWorldReset;
import dev.goldcraft.world.HostGameplayRules;
import dev.goldcraft.world.MinecraftObjects;
import dev.goldcraft.world.MinecraftFluids;
import dev.goldcraft.world.NativePlayers;
import dev.goldcraft.world.NativeHurt;
import dev.goldcraft.world.SharedVitals;
import dev.goldcraft.world.HostMining;
import dev.goldcraft.world.HostExplosions;
import dev.goldcraft.net.MiningPayload;
import dev.goldcraft.net.EditRequestPayload;
import net.minecraft.server.MinecraftServer;
import net.minecraft.server.network.ServerPlayerEntity;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import java.util.*;

@net.neoforged.fml.common.Mod("goldcraft")
public final class GoldCraft {
    public static final Logger LOGGER=LoggerFactory.getLogger("goldcraft");
    private static BridgeLink serverLink;
    // This exact link object and generation own the decoded server state.
    // A replacement BridgeLink can independently start again at generation one.
    private static BridgeLink authorityLink;
    private static long generation;
    private static long nextEditQuery;
    public static final HostWorldState HOST_WORLD=new HostWorldState();
    public static final TraceValidation TRACE_VALIDATION=new TraceValidation();
    private static final Map<UUID,Binding> BINDINGS=new HashMap<>();
    private static final Map<UUID,Delivery> DELIVERIES=new HashMap<>();
    private static byte[] worldPayload,bspPayload,actorPayload,brushPayload,miningPayload;
    private record Binding(long world,int slot,int serial) {}
    private static final class Delivery {
        final HostDeliveryQueue queue=new HostDeliveryQueue();
        long nextEditRequest;
        void enqueue(int type,byte[] bytes) {
            if(type==Wire.MAP_EDIT_SNAPSHOT)queue.editSnapshot(bytes);
            else if(type==Wire.MAP_EDIT_DELTA)queue.editDelta(bytes,()->HOST_WORLD.edits().snapshot().encode());
            else queue.snapshot(type,bytes);
        }
        void flush(ServerPlayerEntity player) {
            for(int sent=0;sent<8&&queue.first()!=null;sent++) {
                Wire.Message message=queue.first();byte[] data=message.payload();int offset=queue.offset(),end=Math.min(data.length,offset+HostPayload.FRAGMENT);
                net.neoforged.neoforge.network.PacketDistributor.sendToPlayer(player,new HostPayload(message.type(),data.length,offset,Arrays.copyOfRange(data,offset,end)));
                queue.advance(end-offset);
            }
        }
    }
    public static boolean hostConnected(){
        BridgeLink link=serverLink;
        return link!=null&&link==authorityLink&&link.connected()&&link.generation()==generation;
    }
    public static boolean sendToHost(int type,byte[] payload){
        BridgeLink link=serverLink;
        // The worker checks this generation under its queue lock. Old poses,
        // vitals and effect responses cannot cross into a reconnected socket.
        return link!=null&&link==authorityLink&&link.sendForGeneration(generation,type,payload);
    }
    public static long currentAuthorityGeneration(){return hostConnected()?generation:0;}
    public static long currentAuthorityClockFrequency(){
        BridgeLink link=serverLink;
        return link!=null&&link==authorityLink?link.localQpcFrequency(generation):0;
    }
    public static long authorityGeneration(){return currentAuthorityGeneration();}
    public static boolean visionAvailable(){
        var link=serverLink;return link!=null&&link==authorityLink&&link.vision(generation);
    }
    public static boolean sendVision(byte[] payload){
        var link=serverLink;return link!=null&&link==authorityLink&&link.sendVision(generation,payload);
    }
    public static boolean nativeAbsorbedHitAvailable(){
        var link=serverLink;return link!=null&&link==authorityLink&&link.nativeAbsorbedHit(generation);
    }
    public static boolean sendNativeAbsorbedHit(byte[] payload){
        var link=serverLink;return link!=null&&link==authorityLink&&link.sendNativeAbsorbedHit(generation,payload);
    }
    public static boolean nativeKnockbackAvailable(){
        var link=serverLink;return link!=null&&link==authorityLink&&link.nativeKnockback(generation);
    }
    public static boolean sendNativeKnockback(byte[] payload){
        var link=serverLink;return link!=null&&link==authorityLink&&link.sendNativeKnockback(generation,payload);
    }
    public static boolean nativeMovementAvailable(){
        var link=serverLink;return link!=null&&link==authorityLink&&link.nativeMovement(generation);
    }
    public static boolean nativeViewAvailable(){
        var link=serverLink;return link!=null&&link==authorityLink&&link.nativeView(generation);
    }
    public static boolean waterAvailable(){
        var link=serverLink;return link!=null&&link==authorityLink&&link.nativeWater(generation);
    }
    public static boolean sendWater(byte[] payload){
        var link=serverLink;return link!=null&&link==authorityLink&&link.sendWater(generation,payload);
    }
    public static boolean nativeImpulseAvailable(){
        var link=serverLink;return link!=null&&link==authorityLink&&link.nativeImpulse(generation);
    }
    public static boolean sendNativeImpulse(byte[] payload){
        var link=serverLink;return link!=null&&link==authorityLink&&link.sendNativeImpulse(generation,payload);
    }
    public static boolean nativeHitAvailable(){
        var link=serverLink;return link!=null&&link==authorityLink&&link.nativeHit(generation);
    }
    public static boolean meleeAvailable(){
        var link=serverLink;return link!=null&&link==authorityLink&&link.melee(generation);
    }
    public static boolean sendMelee(byte[] payload){
        var link=serverLink;return link!=null&&link==authorityLink&&link.sendMelee(generation,payload);
    }
    public static boolean nauseaAvailable(){
        var link=serverLink;return link!=null&&link==authorityLink&&link.nausea(generation);
    }
    public static boolean sendNausea(byte[] payload){
        var link=serverLink;return link!=null&&link==authorityLink&&link.sendNausea(generation,payload);
    }
    public static boolean glowAvailable(){
        var link=serverLink;return link!=null&&link==authorityLink&&link.glow(generation);
    }
    public static boolean sendGlow(byte[] payload){
        var link=serverLink;return link!=null&&link==authorityLink&&link.sendGlow(generation,payload);
    }
    public static boolean visibilityAvailable(){
        var link=serverLink;return link!=null&&link==authorityLink&&link.visibility(generation);
    }
    public static boolean sendVisibility(byte[] payload){
        var link=serverLink;return link!=null&&link==authorityLink&&link.sendVisibility(generation,payload);
    }
    public static boolean fallAvailable(){
        var link=serverLink;return link!=null&&link==authorityLink&&link.fall(generation);
    }
    public static boolean sendFall(byte[] payload){
        var link=serverLink;return link!=null&&link==authorityLink&&link.sendFall(generation,payload);
    }
    public static boolean motionAvailable(){
        var link=serverLink;return link!=null&&link==authorityLink&&link.motion(generation);
    }
    public static boolean sendMotion(byte[] payload){
        var link=serverLink;return link!=null&&link==authorityLink&&link.sendMotion(generation,payload);
    }
    public static boolean healthLimitsAvailable(){
        var link=serverLink;return link!=null&&link==authorityLink&&link.healthLimits(generation);
    }
    public static boolean sendHealthLimit(byte[] payload){
        var link=serverLink;return link!=null&&link==authorityLink&&link.sendHealthLimit(generation,payload);
    }
    public static boolean environmentAvailable(){
        var link=serverLink;return link!=null&&link==authorityLink&&link.environment(generation);
    }
    public static boolean sendEnvironment(byte[] payload){
        var link=serverLink;return link!=null&&link==authorityLink&&link.sendEnvironment(generation,payload);
    }
    public static boolean statusAttributesAvailable(){
        var link=serverLink;return link!=null&&link==authorityLink&&link.statusAttributes(generation);
    }
    public static boolean sendAttributesToHost(int type,byte[] payload){
        var link=serverLink;return link!=null&&link==authorityLink&&link.sendAttributes(generation,type,payload);
    }
    public static boolean nativeArmorAvailable(){
        var link=serverLink;return link!=null&&link==authorityLink&&link.nativeArmor(generation);
    }
    public static boolean fluidsAvailable(){
        var link=serverLink;return link!=null&&link==authorityLink&&link.fluids(generation);
    }
    public static boolean sendFluidsToHost(byte[] payload){
        var link=serverLink;return link!=null&&link==authorityLink&&link.sendFluids(generation,payload);
    }
    public static boolean statusEffectsAvailable(){
        var link=serverLink;return link!=null&&link==authorityLink&&link.statusEffects(generation);
    }
    public static boolean gameplayRulesAvailable(){
        var link=serverLink;return link!=null&&link==authorityLink&&link.gameplayRules(generation);
    }
    public static boolean hostExplosionsAvailable(){
        var link=serverLink;return link!=null&&link==authorityLink&&link.hostExplosions(generation);
    }
    public static boolean skillGuardAvailable(){
        var link=serverLink;return link!=null&&link==authorityLink&&link.skillGuard(generation);
    }
    public static boolean playerGameplayAvailable(){
        var link=serverLink;return link!=null&&link==authorityLink&&link.playerGameplay(generation);
    }
    public static boolean sendPlayerGameplay(int type,byte[] payload){
        var link=serverLink;return link!=null&&link==authorityLink&&link.sendPlayerGameplay(generation,type,payload);
    }
    public static boolean tntDamageAvailable(){
        var link=serverLink;return link!=null&&link==authorityLink&&link.tntDamage(generation);
    }
    public static boolean sendTntDamage(byte[] payload){
        var link=serverLink;return link!=null&&link==authorityLink&&link.sendTntDamage(generation,payload);
    }
    public static boolean sendHostExplosion(byte[] payload){
        var link=serverLink;return link!=null&&link==authorityLink&&link.hostExplosions(generation)&&link.sendForGeneration(generation,Wire.HOST_EXPLOSION,payload);
    }
    public static boolean sendStatusToHost(int type,byte[] payload){
        var link=serverLink;return link!=null&&link==authorityLink&&link.sendStatus(generation,type,payload);
    }
    public static boolean effectsNegotiated(){
        BridgeLink link=serverLink;
        return link!=null&&link==authorityLink&&link.effectPositionHold(generation);
    }
    private static void retireAuthority(MinecraftServer server){
        authorityLink=null;generation=0;reset(server);
    }
    private static void reset(MinecraftServer server) {
        CsAdmission.reset();
        dev.goldcraft.world.PlayerGameplay.reset();
        HostGameplayRules.reset();
        nextEditQuery=0;
        dev.goldcraft.world.NativeArmorServer.clear();NativeHurt.clear();HostSession.clear(server);SharedVitals.clear();NativePlayers.clear();MinecraftObjects.clear();MinecraftFluids.clear();HostMining.clear();HostExplosions.clear();HOST_WORLD.clear();TRACE_VALIDATION.reset();BINDINGS.clear();DELIVERIES.clear();worldPayload=bspPayload=actorPayload=brushPayload=miningPayload=null;
    }
    private static void broadcast(int type,byte[] data) {
        for(var entry:DELIVERIES.entrySet())if(BINDINGS.containsKey(entry.getKey()))entry.getValue().enqueue(type,data);
    }
    private static void paired(MinecraftServer server,byte[] bytes) {
        if(!CsAdmission.deliverResult(bytes))return;
        Wire.Reader r=new Wire.Reader(bytes);long world=r.i64();int slot=r.i32(),serial=r.i32(),status=r.i32();UUID uuid=Wire.uuid(r.bytes(16));r.finish();
        ServerPlayerEntity player=server.getPlayerManager().getPlayer(uuid);if(player==null||world!=HOST_WORLD.epoch())return;
        Binding binding=new Binding(world,slot,serial);boolean first=!binding.equals(BINDINGS.get(uuid));
        Delivery delivery=DELIVERIES.computeIfAbsent(uuid,ignored->new Delivery());
        if(status==0&&first) {
            BINDINGS.put(uuid,binding);
            HostSession.hold(player);
            HostSession.paired(uuid,world);
            delivery.enqueue(Wire.WORLD,worldPayload);delivery.enqueue(Wire.BSP,bspPayload);
            delivery.enqueue(Wire.ACTORS,actorPayload);delivery.enqueue(Wire.BRUSHES,brushPayload);
            delivery.enqueue(Wire.MAP_MINING_POLICY,miningPayload);
            if(HOST_WORLD.edits().ready())delivery.enqueue(Wire.MAP_EDIT_SNAPSHOT,HOST_WORLD.edits().snapshot().encode());
            LOGGER.info("Authoritative player paired: slot={} serial={} epoch={}",slot,serial,Long.toUnsignedString(world));
        }
        if(first||status!=0)delivery.enqueue(Wire.PAIR_RESULT,bytes);
    }
    public GoldCraft(net.neoforged.bus.api.IEventBus modBus) {
        dev.goldcraft.world.NativePlayerHitboxEntity.initialize(modBus);
        HostWorldReset.initialize();
        dev.goldcraft.world.HostSupportUpdates.initialize();
        MinecraftObjects.initialize();
        MinecraftFluids.initialize();
        modBus.addListener(this::registerPayloads);
        modBus.addListener(CsAdmission::registerTasks);
        var events=net.neoforged.neoforge.common.NeoForge.EVENT_BUS;
        events.addListener(net.neoforged.bus.api.EventPriority.LOWEST,
            (net.neoforged.neoforge.event.entity.living.LivingDropsEvent event)->{
                if(event.getEntity() instanceof ServerPlayerEntity player&&HostGameplayRules.suppressPlayerDrops(player))
                    event.setCanceled(true);
            });
        events.addListener((net.neoforged.neoforge.event.entity.player.PlayerEvent.PlayerRespawnEvent event)-> {
            if(event.getEntity() instanceof ServerPlayerEntity player)HostSession.respawned(player);
        });
        events.addListener((net.neoforged.neoforge.event.entity.player.PlayerEvent.PlayerLoggedInEvent event)-> {
            if(event.getEntity() instanceof ServerPlayerEntity admitted&&!CsAdmission.joined(admitted))return;
            if(event.getEntity() instanceof ServerPlayerEntity player && serverLink!=null && player.networkHandler.hasChannel(HostPayload.ID))HostSession.hold(player);
        });
        events.addListener((net.neoforged.neoforge.event.entity.player.PlayerEvent.PlayerLoggedOutEvent event)-> {
            if(event.getEntity() instanceof ServerPlayerEntity admitted)CsAdmission.disconnected(admitted);
            if(event.getEntity() instanceof ServerPlayerEntity player){BINDINGS.remove(player.getUuid());DELIVERIES.remove(player.getUuid());HostMining.disconnect(player.getUuid());HostSession.disconnect(player);}
        });
        events.addListener((net.neoforged.neoforge.event.server.ServerStartedEvent event)-> {
            var server=event.getServer();
            CsAdmission.started(server);
            try {
                BridgeLink.Config.environment("GOLDCRAFT_SERVER",Wire.FABRIC_SERVER,Wire.HOST_SERVER).ifPresent(config->{
                    serverLink=new BridgeLink(config);LOGGER.info("Authoritative host link configured on loopback port {}",config.port());
                });
            }catch(IllegalArgumentException e){LOGGER.error("Invalid host-server configuration: {}",e.getMessage());}
        });
        events.addListener((net.neoforged.neoforge.event.server.ServerStoppedEvent event)->{var server=event.getServer();if(serverLink!=null)serverLink.close();serverLink=null;retireAuthority(server);HostSession.stopped();CsAdmission.stopped();});
        events.addListener((net.neoforged.neoforge.event.tick.ServerTickEvent.Post event)->{
            var server=event.getServer();
            CsAdmission.tick();
            BridgeLink link=serverLink;
            if(link==null){if(authorityLink!=null)retireAuthority(server);return;}
            var incoming=link.drainWithContext();
            if(!incoming.connected()){
                if(authorityLink!=null||HOST_WORLD.epoch()!=0)retireAuthority(server);
                return;
            }
            if(authorityLink!=link||generation!=incoming.generation()){
                retireAuthority(server);
                authorityLink=link;generation=incoming.generation();
                LOGGER.info("Connected to authoritative CS host, generation {}",generation);
            }
            if(!hostConnected()){retireAuthority(server);return;}
            SharedVitals.capture(server,HOST_WORLD);
            for(var received:incoming.messages()) {
                if(received.generation()!=generation||received.epoch()!=incoming.epoch()||!hostConnected()){
                    retireAuthority(server);return;
                }
                Wire.Message message=received.message();
                try {
                    switch(message.type()) {
                        case Wire.WORLD -> {
                            long epoch=new Wire.Reader(message.payload()).i64();
                            if(HOST_WORLD.epoch()!=0&&HOST_WORLD.epoch()!=epoch)reset(server);
                            HOST_WORLD.world(message.payload());worldPayload=message.payload();
                            LOGGER.info("Host map {} epoch {}",HOST_WORLD.map(),Long.toUnsignedString(HOST_WORLD.epoch()));
                        }
                        case Wire.BSP -> {
                            HOST_WORLD.bsp(message.payload());bspPayload=message.payload();broadcast(Wire.BSP,bspPayload);
                            LOGGER.info("Authoritative BSP loaded: {} bytes, {} planes, {} models, CRC32 {}",HOST_WORLD.bspBytes().length,HOST_WORLD.geometry().planeCount(),HOST_WORLD.geometry().modelCount(),HOST_WORLD.geometry().crc());
                        }
                        case Wire.HOST_MATERIALS -> {
                            if(!hostExplosionsAvailable())throw new IllegalArgumentException("Unnegotiated host materials");
                            HostExplosions.materials(HOST_WORLD,message.payload());
                        }
                        case Wire.ACTORS -> {HOST_WORLD.actors(message.payload());actorPayload=message.payload();broadcast(Wire.ACTORS,actorPayload);}
                        case Wire.NATIVE_HIT -> NativeHurt.handle(server,message.payload());
                        case Wire.NATIVE_MOVEMENT -> {
                            if(!nativeMovementAvailable())throw new IllegalArgumentException("Unnegotiated native movement");
                            HOST_WORLD.nativeMovement().accept(HOST_WORLD,message.payload(),server.getTicks());
                        }
                        case Wire.NATIVE_VIEW -> {
                            if(!nativeViewAvailable())throw new IllegalArgumentException("Unnegotiated native view");
                            HOST_WORLD.nativeView().accept(HOST_WORLD,message.payload(),server.getTicks());
                        }
                        case Wire.HEALTH_LIMITS -> {
                            if(!healthLimitsAvailable())throw new IllegalArgumentException("Unnegotiated health limits");
                            HOST_WORLD.healthLimits().accept(HOST_WORLD,message.payload());
                        }
                        case Wire.BRUSHES -> {HOST_WORLD.brushes(message.payload());brushPayload=message.payload();broadcast(Wire.BRUSHES,brushPayload);}
                        case Wire.PAIR_RESULT -> paired(server,message.payload());
                        case Wire.GAMEPLAY_RULES -> {
                            if(!gameplayRulesAvailable())throw new IllegalArgumentException("Unnegotiated gameplay rules");
                            HostGameplayRules.accept(HOST_WORLD,message.payload());
                        }
                        case Wire.EFFECT_GRANT -> {
                            if(!effectsNegotiated())throw new IllegalArgumentException("Unnegotiated effect grant");
                            HostSession.effectGrant(server,HOST_WORLD,HostEffectGrant.decode(message.payload()));
                        }
                        case Wire.TRACE_RESULT -> TRACE_VALIDATION.result(message.payload(),HOST_WORLD);
                        case Wire.OBJECT_ACTION -> MinecraftObjects.action(server,HOST_WORLD,message.payload());
                        case Wire.DAMAGE_RESULT -> NativePlayers.result(message.payload());
                        case dev.goldcraft.bridge.NativeArmorProtocol.REQUEST -> dev.goldcraft.world.NativeArmorServer.handle(server,message.payload(),nativeArmorAvailable());
                        case Wire.STATUS_SPEND -> dev.goldcraft.world.NativeStatusAttributes.spend(server,message.payload());
                        case Wire.MAP_MINING_POLICY -> {
                            var previous=HOST_WORLD.mining();HOST_WORLD.mining(message.payload());
                            if(!previous.equals(HOST_WORLD.mining())){
                                miningPayload=HOST_WORLD.mining().encode();broadcast(Wire.MAP_MINING_POLICY,miningPayload);
                            }
                        }
                        case Wire.MAP_MINING_RESULT -> HostMining.result(server,HOST_WORLD,message.payload());
                        case Wire.MAP_MINING_SURFACE -> HostMining.surface(server,HOST_WORLD,message.payload());
                        case Wire.MAP_EDIT_SNAPSHOT,Wire.MAP_EDIT_DELTA -> {
                            var result=message.type()==Wire.MAP_EDIT_SNAPSHOT?HOST_WORLD.edits().accept(MapEdits.snapshot(message.payload())):
                                HOST_WORLD.edits().accept(MapEdits.delta(message.payload()));
                            if(result==MapEdits.Applied.CHANGED)broadcast(message.type(),message.payload());
                        }
                        default -> { }
                    }
                }catch(IllegalArgumentException e){
                    if(message.type()==Wire.MAP_EDIT_SNAPSHOT||message.type()==Wire.MAP_EDIT_DELTA)HOST_WORLD.edits().invalidate();
                    LOGGER.warn("Rejected host-server payload: {}",e.getMessage());
                }
            }
            // A batch is a historical snapshot, not a connection lease. Avoid
            // ticking old world state if the worker retired it during dispatch.
            // A later race remains fenced at the actual outgoing queue lock.
            if(!hostConnected()){retireAuthority(server);return;}
            if(HOST_WORLD.epoch()!=0&&!HOST_WORLD.edits().ready()&&System.nanoTime()>=nextEditQuery){
                sendToHost(Wire.MAP_EDIT_QUERY,new Wire.Writer().i64(HOST_WORLD.epoch()).toByteArray());
                nextEditQuery=System.nanoTime()+1_000_000_000L;
            }
            for(var entry:DELIVERIES.entrySet()) {
                var player=server.getPlayerManager().getPlayer(entry.getKey());
                if(player!=null&&player.networkHandler.hasChannel(HostPayload.ID))entry.getValue().flush(player);
            }
            dev.goldcraft.world.PlayerGameplay.tick(server,actor->{
                var binding=BINDINGS.get(actor.minecraftPlayer());
                return binding!=null&&binding.world()==HOST_WORLD.epoch()&&binding.slot()==actor.slot()&&binding.serial()==actor.serial();
            });
            TRACE_VALIDATION.tick(HOST_WORLD);
            HostSession.tick(server,HOST_WORLD);
            HostGameplayRules.tick(server,HOST_WORLD);
            dev.goldcraft.world.HostSupportUpdates.tick(server,HOST_WORLD);
            HostMining.tick(server,HOST_WORLD);
            NativePlayers.tick(server,HOST_WORLD);
            dev.goldcraft.world.NativeArmorServer.flush(nativeArmorAvailable());
            MinecraftObjects.tick(server,HOST_WORLD);
            MinecraftFluids.tick(server,HOST_WORLD);
            Performance.flush();
        });
        LOGGER.info("GoldCraft initialized: Minecraft 1.21.1 / NeoForge 21.1.256, protocol {}",Wire.VERSION);
    }
    private void registerPayloads(net.neoforged.neoforge.network.event.RegisterPayloadHandlersEvent event) {
        CsAdmission.registerPayloads(event);
        var registrar=event.registrar(Integer.toString(Wire.VERSION));
        registrar.playToServer(BindingPayload.ID,BindingPayload.CODEC,(payload,context)-> {
            if(context.player() instanceof ServerPlayerEntity player&&CsAdmission.playBinding(player,payload.identity()))return;
            byte[] request=new Wire.Writer().bytes(payload.identity()).bytes(Wire.uuid(context.player().getUuid())).toByteArray();
            if(!sendToHost(Wire.PAIR_PLAYER,request))LOGGER.debug("Server pairing queue unavailable");
        });
        registrar.playToServer(ControlPayload.ID,ControlPayload.CODEC,(payload,context)->
            HostSession.ready(context.player().getUuid(),payload.epoch(),payload.serial(),payload.life(),payload.ready()));
        registrar.playToServer(MiningPayload.ID,MiningPayload.CODEC,(payload,context)->{
            if(context.player() instanceof ServerPlayerEntity player)HostMining.intent(player,payload);
        });
        registrar.playToClient(HostPayload.ID,HostPayload.CODEC,dev.goldcraft.net.HostNetwork::receive);
        registrar.playToServer(EditRequestPayload.ID,EditRequestPayload.CODEC,(payload,context)->{
            var id=context.player().getUuid();var binding=BINDINGS.get(id);var delivery=DELIVERIES.get(id);
            if(binding==null||delivery==null||binding.world()!=payload.epoch()||payload.epoch()!=HOST_WORLD.epoch()||
                !HOST_WORLD.edits().ready()||System.nanoTime()<delivery.nextEditRequest)return;
            delivery.enqueue(Wire.MAP_EDIT_SNAPSHOT,HOST_WORLD.edits().snapshot().encode());
            delivery.nextEditRequest=System.nanoTime()+1_000_000_000L;
        });
    }

}
