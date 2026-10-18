package dev.goldcraft.bridge;

import org.junit.jupiter.api.Test;
import java.net.InetAddress;
import java.net.InetSocketAddress;
import java.net.ServerSocket;
import java.net.Socket;
import java.nio.ByteBuffer;
import java.time.Duration;
import java.util.Arrays;
import java.util.function.BooleanSupplier;
import static org.junit.jupiter.api.Assertions.*;

/** Real TCP negotiation, including protocol30 hosts that close on unknown bits. */
class TntHandshakeTest {
    private static final byte[] SESSION=Wire.key("00112233445566778899aabbccddeeff");
    private static final byte[] TOKEN=Wire.key("ffeeddccbbaa99887766554433221100");
    private static void until(BooleanSupplier condition) {
        assertTimeoutPreemptively(Duration.ofSeconds(4),()->{while(!condition.getAsBoolean())Thread.sleep(5);});
    }
    private static ServerSocket host() throws Exception {
        var host=new ServerSocket(0,4,InetAddress.getLoopbackAddress());host.setSoTimeout(4000);return host;
    }
    private static BridgeLink link(int port,boolean client) {
        return new BridgeLink(new BridgeLink.Config(port,SESSION,TOKEN,
            client?Wire.FABRIC_CLIENT:Wire.FABRIC_SERVER,client?Wire.HOST_CLIENT:Wire.HOST_SERVER));
    }
    private static Wire.Message read(Socket socket) throws Exception {
        socket.setSoTimeout(4000);
        var input=socket.getInputStream();byte[] bytes=input.readNBytes(Wire.HEADER_BYTES);
        assertEquals(Wire.HEADER_BYTES,bytes.length);var header=Wire.header(ByteBuffer.wrap(bytes));
        byte[] payload=input.readNBytes(header.size());assertEquals(header.size(),payload.length);
        return new Wire.Message(header.type(),payload);
    }
    private static int hello(Socket socket,boolean client) throws Exception {
        var message=read(socket);assertEquals(Wire.HELLO,message.type());var r=new Wire.Reader(message.payload());
        assertArrayEquals(SESSION,r.bytes(16));assertArrayEquals(TOKEN,r.bytes(16));
        assertEquals(client?Wire.FABRIC_CLIENT:Wire.FABRIC_SERVER,r.i32());int caps=r.i32();r.finish();return caps;
    }
    private static void welcome(Socket socket,long epoch,int caps,boolean client,byte[] session) throws Exception {
        byte[] payload=new Wire.Writer().bytes(session).i32(client?Wire.HOST_CLIENT:Wire.HOST_SERVER)
            .i32(caps).f32(Wire.UNITS_PER_BLOCK).f32(Wire.Y_OFFSET).toByteArray();
        var output=socket.getOutputStream();output.write(Wire.frameHeader(Wire.WELCOME,epoch,1,payload.length).array());
        output.write(payload);output.flush();
    }
    @Test void currentServerNegotiatesAndReconnectRejectsPreviousGeneration() throws Exception {
        try(var host=host();var link=link(host.getLocalPort(),false)) {
            long previous;
            try(var socket=host.accept()) {
                assertNotEquals(0,hello(socket,false)&Wire.TNT_DAMAGE_CAP);
                welcome(socket,19,Wire.TNT_DAMAGE_CAP,false,SESSION);until(link::connected);previous=link.generation();
                assertTrue(link.tntDamage(previous));assertFalse(link.tntDamage(0));
                byte[] payload={1,2,3};assertTrue(link.sendTntDamage(previous,payload));
                Wire.Message message;do{message=read(socket);}while(message.type()==Wire.HEARTBEAT);
                assertEquals(Wire.DAMAGE_REQUEST,message.type());assertArrayEquals(payload,message.payload());
            }
            until(()->!link.connected());assertFalse(link.sendTntDamage(previous,new byte[0]));
            try(var socket=host.accept()) {
                assertNotEquals(0,hello(socket,false)&Wire.TNT_DAMAGE_CAP);
                welcome(socket,20,Wire.TNT_DAMAGE_CAP,false,SESSION);until(link::connected);
                assertNotEquals(previous,link.generation());assertFalse(link.sendTntDamage(previous,new byte[0]));
                assertTrue(link.tntDamage(link.generation()));
            }
        }
    }
    @Test void oldHostCloseRetriesIdenticalCredentialsWithoutTnt() throws Exception {
        try(var host=host();var link=link(host.getLocalPort(),false)) {
            try(var socket=host.accept()){assertNotEquals(0,hello(socket,false)&Wire.NATIVE_WATER_CAP);}
            try(var socket=host.accept()){assertNotEquals(0,hello(socket,false)&Wire.NATIVE_IMPULSE_CAP);}
            try(var socket=host.accept()){assertNotEquals(0,hello(socket,false)&Wire.NATIVE_HIT_CAP);}
            try(var socket=host.accept()){assertNotEquals(0,hello(socket,false)&Wire.NATIVE_VIEW_CAP);}
            try(var socket=host.accept()){assertNotEquals(0,hello(socket,false)&Wire.STATUS_MELEE_CAP);}
            try(var socket=host.accept()){assertNotEquals(0,hello(socket,false)&Wire.STATUS_NAUSEA_CAP);}
            try(var socket=host.accept()){assertNotEquals(0,hello(socket,false)&Wire.STATUS_GLOW_CAP);}
            try(var socket=host.accept()){assertNotEquals(0,hello(socket,false)&Wire.STATUS_HEALTH_CAP);}
            try(var socket=host.accept()){assertNotEquals(0,hello(socket,false)&Wire.STATUS_FALL_CAP);}
            try(var socket=host.accept()){assertNotEquals(0,hello(socket,false)&Wire.SKILL_GUARD_CAP);}
            try(var socket=host.accept()){assertNotEquals(0,hello(socket,false)&Wire.STATUS_VISION_CAP);}
            try(var socket=host.accept()){assertNotEquals(0,hello(socket,false)&Wire.STATUS_VISIBILITY_CAP);}
            try(var socket=host.accept()){assertNotEquals(0,hello(socket,false)&Wire.STATUS_MOTION_CAP);}

            try(var socket=host.accept()){assertNotEquals(0,hello(socket,false)&Wire.STATUS_ENVIRONMENT_CAP);}
            int first;
            try(var socket=host.accept()){first=hello(socket,false);assertNotEquals(0,first&Wire.TNT_DAMAGE_CAP);}
            try(var socket=host.accept()){assertEquals(first&~Wire.PLAYER_GAMEPLAY_CAP,hello(socket,false));}
            try(var socket=host.accept()) {
                assertEquals(first&~(Wire.TNT_DAMAGE_CAP|Wire.PLAYER_GAMEPLAY_CAP),hello(socket,false));
                welcome(socket,21,Wire.HOST_EXPLOSIONS_CAP,false,SESSION);until(link::connected);
                assertTrue(link.hostExplosions(link.generation()));assertFalse(link.tntDamage(link.generation()));
                assertFalse(link.sendTntDamage(link.generation(),new byte[0]));
            }
        }
    }
    @Test void connectionRefusalBeforeHelloDoesNotDowngrade() throws Exception {
        int port;try(var reserved=host()){port=reserved.getLocalPort();}
        try(var link=link(port,false)) {
            until(()->!link.lastError().isEmpty());
            try(var host=new ServerSocket()) {
                host.bind(new InetSocketAddress(InetAddress.getLoopbackAddress(),port));host.setSoTimeout(4000);
                try(var socket=host.accept()) {
                    assertNotEquals(0,hello(socket,false)&Wire.TNT_DAMAGE_CAP);
                    welcome(socket,22,Wire.TNT_DAMAGE_CAP,false,SESSION);until(link::connected);
                    assertTrue(link.tntDamage(link.generation()));
                }
            }
        }
    }
    @Test void wrongIdentityIsRejectedWithoutTreatingItAsLegacyHost() throws Exception {
        try(var host=host();var link=link(host.getLocalPort(),false)) {
            try(var socket=host.accept()) {
                hello(socket,false);byte[] wrong=Arrays.copyOf(SESSION,16);wrong[0]^=1;
                welcome(socket,23,Wire.TNT_DAMAGE_CAP,false,wrong);
                until(()->link.lastError().contains("identity mismatch"));
                assertFalse(link.connected());assertEquals(0,link.generation());
            }
            try(var socket=host.accept()){assertNotEquals(0,hello(socket,false)&Wire.TNT_DAMAGE_CAP);}
        }
    }
    @Test void legacyRetryRejectsUnrequestedTntCapability() throws Exception {
        try(var host=host();var link=link(host.getLocalPort(),false)) {
            try(var socket=host.accept()){assertNotEquals(0,hello(socket,false)&Wire.NATIVE_WATER_CAP);}
            try(var socket=host.accept()){assertNotEquals(0,hello(socket,false)&Wire.NATIVE_IMPULSE_CAP);}
            try(var socket=host.accept()){assertNotEquals(0,hello(socket,false)&Wire.NATIVE_HIT_CAP);}
            try(var socket=host.accept()){assertNotEquals(0,hello(socket,false)&Wire.NATIVE_VIEW_CAP);}
            try(var socket=host.accept()){assertNotEquals(0,hello(socket,false)&Wire.STATUS_MELEE_CAP);}
            try(var socket=host.accept()){assertNotEquals(0,hello(socket,false)&Wire.STATUS_NAUSEA_CAP);}
            try(var socket=host.accept()){assertNotEquals(0,hello(socket,false)&Wire.STATUS_GLOW_CAP);}
            try(var socket=host.accept()){assertNotEquals(0,hello(socket,false)&Wire.STATUS_HEALTH_CAP);}
            try(var socket=host.accept()){assertNotEquals(0,hello(socket,false)&Wire.STATUS_FALL_CAP);}
            try(var socket=host.accept()){assertNotEquals(0,hello(socket,false)&Wire.SKILL_GUARD_CAP);}
            try(var socket=host.accept()){assertNotEquals(0,hello(socket,false)&Wire.STATUS_VISION_CAP);}
            try(var socket=host.accept()){assertNotEquals(0,hello(socket,false)&Wire.STATUS_VISIBILITY_CAP);}
            try(var socket=host.accept()){assertNotEquals(0,hello(socket,false)&Wire.STATUS_MOTION_CAP);}

            try(var socket=host.accept()){hello(socket,false);}
            try(var socket=host.accept()){assertEquals(0,hello(socket,false)&Wire.STATUS_ENVIRONMENT_CAP);}
            try(var socket=host.accept()){assertNotEquals(0,hello(socket,false)&Wire.TNT_DAMAGE_CAP);}
            try(var socket=host.accept()) {
                assertEquals(0,hello(socket,false)&Wire.TNT_DAMAGE_CAP);
                welcome(socket,24,Wire.TNT_DAMAGE_CAP,false,SESSION);
                until(()->link.lastError().contains("Unrequested TNT"));
                assertFalse(link.connected());assertEquals(0,link.generation());
                assertFalse(link.sendTntDamage(1,new byte[0]));
            }
        }
    }
    @Test void graphicalClientNeverOffersOrAcceptsServerTnt() throws Exception {
        try(var host=host();var link=link(host.getLocalPort(),true);var socket=host.accept()) {
            assertEquals(0,hello(socket,true)&Wire.TNT_DAMAGE_CAP);
            welcome(socket,25,Wire.TNT_DAMAGE_CAP,true,SESSION);
            until(()->link.lastError().contains("contract mismatch"));assertFalse(link.connected());
            assertEquals(0,link.generation());assertFalse(link.tntDamage(1));
        }
    }
}
