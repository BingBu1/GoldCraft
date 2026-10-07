package dev.goldcraft;

import dev.goldcraft.bridge.BridgeLink;
import dev.goldcraft.bridge.Wire;
import dev.goldcraft.bridge.Performance;
import dev.goldcraft.bridge.HostWorldState;
import dev.goldcraft.net.BindingPayload;
import dev.goldcraft.net.HostPayload;
import dev.goldcraft.net.ControlPayload;
import dev.goldcraft.world.HostCollision;
import dev.goldcraft.world.TraceValidation;
import dev.goldcraft.world.HostSession;
import dev.goldcraft.world.HostWorldReset;
import dev.goldcraft.world.MinecraftObjects;
import dev.goldcraft.world.NativePlayers;
import dev.goldcraft.world.SharedVitals;
import net.minecraft.server.MinecraftServer;
import net.minecraft.server.network.ServerPlayerEntity;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import java.util.*;

@net.neoforged.fml.common.Mod("goldcraft")
public final class GoldCraft {
    public static final Logger LOGGER=LoggerFactory.getLogger("goldcraft");
    private static BridgeLink serverLink;
    private static long generation;
    public static final HostWorldState HOST_WORLD=new HostWorldState();
    public static final TraceValidation TRACE_VALIDATION=new TraceValidation();
    private static final Map<UUID,Binding> BINDINGS=new HashMap<>();
    private static final Map<UUID,Delivery> DELIVERIES=new HashMap<>();
    private static byte[] worldPayload,bspPayload,actorPayload,brushPayload;
    private record Binding(long world,int slot,int serial) {}
    private static final class Delivery {
        final ArrayDeque<Wire.Message> queue=new ArrayDeque<>();
        int offset;
        void enqueue(int type,byte[] bytes) {
            if(bytes==null)return;
            Wire.Message first=queue.peekFirst();
            queue.removeIf(m->m.type()==type&&(offset==0||m!=first));
            queue.addLast(new Wire.Message(type,bytes));
        }
        void flush(ServerPlayerEntity player) {
            for(int sent=0;sent<8&&!queue.isEmpty();sent++) {
                Wire.Message message=queue.peekFirst();byte[] data=message.payload();int end=Math.min(data.length,offset+HostPayload.FRAGMENT);
                net.neoforged.neoforge.network.PacketDistributor.sendToPlayer(player,new HostPayload(message.type(),data.length,offset,Arrays.copyOfRange(data,offset,end)));
                offset=end;if(offset==data.length){queue.removeFirst();offset=0;}
            }
        }
    }
    public static boolean hostConnected(){return serverLink!=null&&serverLink.connected();}
    public static boolean sendToHost(int type,byte[] payload){return hostConnected()&&serverLink.send(type,payload);}
    private static void reset(MinecraftServer server) {
        HostSession.clear(server);SharedVitals.clear();NativePlayers.clear();MinecraftObjects.clear();HOST_WORLD.clear();TRACE_VALIDATION.reset();BINDINGS.clear();DELIVERIES.clear();worldPayload=bspPayload=actorPayload=brushPayload=null;
    }
    private static void broadcast(int type,byte[] data) {
        for(var entry:DELIVERIES.entrySet())if(BINDINGS.containsKey(entry.getKey()))entry.getValue().enqueue(type,data);
    }
    private static void paired(MinecraftServer server,byte[] bytes) {
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
            LOGGER.info("Authoritative player paired: slot={} serial={} epoch={}",slot,serial,Long.toUnsignedString(world));
        }
        if(first||status!=0)delivery.enqueue(Wire.PAIR_RESULT,bytes);
    }
    public GoldCraft(net.neoforged.bus.api.IEventBus modBus) {
        dev.goldcraft.world.NativePlayerHitboxEntity.initialize(modBus);
        HostWorldReset.initialize();
        MinecraftObjects.initialize();
        modBus.addListener(this::registerPayloads);
        var events=net.neoforged.neoforge.common.NeoForge.EVENT_BUS;
        events.addListener((net.neoforged.neoforge.event.entity.player.PlayerEvent.PlayerRespawnEvent event)-> {
            if(event.getEntity() instanceof ServerPlayerEntity player)HostSession.respawned(player);
        });
        events.addListener((net.neoforged.neoforge.event.entity.player.PlayerEvent.PlayerLoggedInEvent event)-> {
            if(event.getEntity() instanceof ServerPlayerEntity player && serverLink!=null && player.networkHandler.hasChannel(HostPayload.ID))HostSession.hold(player);
        });
        events.addListener((net.neoforged.neoforge.event.entity.player.PlayerEvent.PlayerLoggedOutEvent event)-> {
            if(event.getEntity() instanceof ServerPlayerEntity player){BINDINGS.remove(player.getUuid());DELIVERIES.remove(player.getUuid());HostSession.disconnect(player);}
        });
        events.addListener((net.neoforged.neoforge.event.server.ServerStartedEvent event)-> {
            var server=event.getServer();
            try {
                BridgeLink.Config.environment("GOLDCRAFT_SERVER",Wire.FABRIC_SERVER,Wire.HOST_SERVER).ifPresent(config->{
                    serverLink=new BridgeLink(config);LOGGER.info("Authoritative host link configured on loopback port {}",config.port());
                });
            }catch(IllegalArgumentException e){LOGGER.error("Invalid host-server configuration: {}",e.getMessage());}
        });
        events.addListener((net.neoforged.neoforge.event.server.ServerStoppedEvent event)->{var server=event.getServer();if(serverLink!=null)serverLink.close();serverLink=null;generation=0;reset(server);HostSession.stopped();});
        events.addListener((net.neoforged.neoforge.event.tick.ServerTickEvent.Post event)->{
            var server=event.getServer();
            if(serverLink==null)return;
            if(!serverLink.connected()){if(HOST_WORLD.epoch()!=0)reset(server);return;}
            if(generation!=serverLink.generation()){generation=serverLink.generation();reset(server);LOGGER.info("Connected to authoritative CS host, generation {}",generation);}
            SharedVitals.capture(server,HOST_WORLD);
            for(Wire.Message message:serverLink.drain()) {
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
                        case Wire.ACTORS -> {HOST_WORLD.actors(message.payload());actorPayload=message.payload();broadcast(Wire.ACTORS,actorPayload);}
                        case Wire.BRUSHES -> {HOST_WORLD.brushes(message.payload());brushPayload=message.payload();broadcast(Wire.BRUSHES,brushPayload);}
                        case Wire.PAIR_RESULT -> paired(server,message.payload());
                        case Wire.TRACE_RESULT -> TRACE_VALIDATION.result(message.payload(),HOST_WORLD);
                        case Wire.OBJECT_ACTION -> MinecraftObjects.action(server,HOST_WORLD,message.payload());
                        case Wire.DAMAGE_RESULT -> NativePlayers.result(message.payload());
                        default -> { }
                    }
                }catch(IllegalArgumentException e){LOGGER.warn("Rejected host-server payload: {}",e.getMessage());}
            }
            for(var entry:DELIVERIES.entrySet()) {
                var player=server.getPlayerManager().getPlayer(entry.getKey());
                if(player!=null&&player.networkHandler.hasChannel(HostPayload.ID))entry.getValue().flush(player);
            }
            TRACE_VALIDATION.tick(HOST_WORLD);
            HostSession.tick(server,HOST_WORLD);
            NativePlayers.tick(server,HOST_WORLD);
            MinecraftObjects.tick(server,HOST_WORLD);
            Performance.flush();
        });
        LOGGER.info("GoldCraft initialized: Minecraft 1.21.1 / NeoForge 21.1.256, protocol {}",Wire.VERSION);
    }
    private void registerPayloads(net.neoforged.neoforge.network.event.RegisterPayloadHandlersEvent event) {
        var registrar=event.registrar(Integer.toString(Wire.VERSION));
        registrar.playToServer(BindingPayload.ID,BindingPayload.CODEC,(payload,context)-> {
            byte[] request=new Wire.Writer().bytes(payload.identity()).bytes(Wire.uuid(context.player().getUuid())).toByteArray();
            if(!sendToHost(Wire.PAIR_PLAYER,request))LOGGER.debug("Server pairing queue unavailable");
        });
        registrar.playToServer(ControlPayload.ID,ControlPayload.CODEC,(payload,context)->
            HostSession.ready(context.player().getUuid(),payload.epoch(),payload.serial(),payload.life(),payload.ready()));
        registrar.playToClient(HostPayload.ID,HostPayload.CODEC,dev.goldcraft.net.HostNetwork::receive);
    }

}
