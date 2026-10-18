package dev.goldcraft.bridge;

import org.junit.jupiter.api.Test;
import java.net.InetAddress;
import java.net.ServerSocket;
import java.net.Socket;
import java.nio.ByteBuffer;
import java.time.Duration;
import java.util.function.BooleanSupplier;
import static org.junit.jupiter.api.Assertions.*;

/** Real loopback transport; no game server or client process is touched. */
class PlayerGameplayHandshakeTest {
    private static final byte[] SESSION=Wire.key("00112233445566778899aabbccddeeff");
    private static final byte[] TOKEN=Wire.key("ffeeddccbbaa99887766554433221100");
    private static void until(BooleanSupplier condition){
        assertTimeoutPreemptively(Duration.ofSeconds(4),()->{while(!condition.getAsBoolean())Thread.sleep(5);});
    }
    private static ServerSocket host() throws Exception{
        var host=new ServerSocket(0,4,InetAddress.getLoopbackAddress());host.setSoTimeout(4000);return host;
    }
    private static BridgeLink link(int port,boolean client){
        return new BridgeLink(new BridgeLink.Config(port,SESSION,TOKEN,
            client?Wire.FABRIC_CLIENT:Wire.FABRIC_SERVER,client?Wire.HOST_CLIENT:Wire.HOST_SERVER));
    }
    private static Wire.Message read(Socket socket) throws Exception{
        socket.setSoTimeout(4000);var input=socket.getInputStream();var bytes=input.readNBytes(Wire.HEADER_BYTES);
        assertEquals(Wire.HEADER_BYTES,bytes.length);var header=Wire.header(ByteBuffer.wrap(bytes));
        var payload=input.readNBytes(header.size());assertEquals(header.size(),payload.length);
        return new Wire.Message(header.type(),payload);
    }
    private static int hello(Socket socket,boolean client) throws Exception{
        var message=read(socket);assertEquals(Wire.HELLO,message.type());var r=new Wire.Reader(message.payload());
        assertArrayEquals(SESSION,r.bytes(16));assertArrayEquals(TOKEN,r.bytes(16));
        assertEquals(client?Wire.FABRIC_CLIENT:Wire.FABRIC_SERVER,r.i32());int caps=r.i32();r.finish();return caps;
    }
    private static void welcome(Socket socket,long epoch,int caps,boolean client) throws Exception{
        var payload=new Wire.Writer().bytes(SESSION).i32(client?Wire.HOST_CLIENT:Wire.HOST_SERVER).i32(caps)
            .f32(Wire.UNITS_PER_BLOCK).f32(Wire.Y_OFFSET).toByteArray();
        var out=socket.getOutputStream();out.write(Wire.frameHeader(Wire.WELCOME,epoch,1,payload.length).array());
        out.write(payload);out.flush();
    }
    @Test void negotiatedMessagesUseCurrentGenerationOnly() throws Exception{
        try(var host=host();var link=link(host.getLocalPort(),false)){
            long old;
            try(var socket=host.accept()){
                assertNotEquals(0,hello(socket,false)&Wire.PLAYER_GAMEPLAY_CAP);
                welcome(socket,41,Wire.PLAYER_GAMEPLAY_CAP,false);until(link::connected);old=link.generation();
                assertTrue(link.playerGameplay(old));assertFalse(link.playerGameplay(0));
                assertFalse(link.sendPlayerGameplay(old,Wire.DAMAGE_REQUEST,new byte[0]));
                for(int type:new int[]{Wire.PLAYER_GAMEPLAY,Wire.PLAYER_ATTACK}){
                    assertTrue(link.sendPlayerGameplay(old,type,new byte[]{7,8}));
                    Wire.Message message;do{message=read(socket);}while(message.type()==Wire.HEARTBEAT);
                    assertEquals(type,message.type());assertArrayEquals(new byte[]{7,8},message.payload());
                }
            }
            until(()->!link.connected());assertFalse(link.sendPlayerGameplay(old,Wire.PLAYER_ATTACK,new byte[0]));
            try(var socket=host.accept()){
                assertNotEquals(0,hello(socket,false)&Wire.PLAYER_GAMEPLAY_CAP);
                welcome(socket,42,Wire.PLAYER_GAMEPLAY_CAP,false);until(link::connected);
                assertNotEquals(old,link.generation());assertFalse(link.playerGameplay(old));
                assertTrue(link.playerGameplay(link.generation()));
            }
        }
    }
    @Test void legacyTntHostRetainsTntWithoutGameplay() throws Exception{
        try(var host=host();var link=link(host.getLocalPort(),false)){
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
            int first;try(var socket=host.accept()){first=hello(socket,false);assertEquals(0,first&Wire.STATUS_ENVIRONMENT_CAP);}
            try(var socket=host.accept()){
                assertEquals(first&~Wire.PLAYER_GAMEPLAY_CAP,hello(socket,false));
                welcome(socket,43,Wire.TNT_DAMAGE_CAP,false);until(link::connected);
                assertTrue(link.tntDamage(link.generation()));assertFalse(link.playerGameplay(link.generation()));
                assertFalse(link.sendPlayerGameplay(link.generation(),Wire.PLAYER_GAMEPLAY,new byte[0]));
            }
        }
    }
    @Test void retryCannotAcceptCapabilityItDidNotOffer() throws Exception{
        try(var host=host();var link=link(host.getLocalPort(),false)){
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
            try(var socket=host.accept()){
                assertEquals(0,hello(socket,false)&Wire.PLAYER_GAMEPLAY_CAP);
                welcome(socket,44,Wire.PLAYER_GAMEPLAY_CAP,false);
                until(()->link.lastError().contains("Unrequested player gameplay"));
                assertFalse(link.connected());assertEquals(0,link.generation());
            }
        }
    }
    @Test void graphicalClientCannotClaimServerGameplay() throws Exception{
        try(var host=host();var link=link(host.getLocalPort(),true);var socket=host.accept()){
            assertEquals(0,hello(socket,true)&Wire.PLAYER_GAMEPLAY_CAP);
            welcome(socket,45,Wire.PLAYER_GAMEPLAY_CAP,true);
            until(()->link.lastError().contains("contract mismatch"));
            assertFalse(link.connected());assertFalse(link.playerGameplay(link.generation()));
        }
    }
}
