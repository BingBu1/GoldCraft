package dev.goldcraft.bridge;

import org.junit.jupiter.api.Test;
import java.net.InetAddress;
import java.net.ServerSocket;
import java.net.Socket;
import java.nio.ByteBuffer;
import java.time.Duration;
import java.util.function.BooleanSupplier;
import static org.junit.jupiter.api.Assertions.*;

class NativeMovementHandshakeTest {
    private static final byte[] KEY=new byte[16];
    private static void until(BooleanSupplier condition){assertTimeoutPreemptively(Duration.ofSeconds(4),()->{while(!condition.getAsBoolean())Thread.sleep(5);});}
    private static ServerSocket host()throws Exception{var s=new ServerSocket(0,4,InetAddress.getLoopbackAddress());s.setSoTimeout(4000);return s;}
    private static BridgeLink link(ServerSocket host,boolean client){return new BridgeLink(new BridgeLink.Config(host.getLocalPort(),KEY,KEY,client?Wire.FABRIC_CLIENT:Wire.FABRIC_SERVER,client?Wire.HOST_CLIENT:Wire.HOST_SERVER));}
    private static int hello(Socket s)throws Exception{
        s.setSoTimeout(4000);var in=s.getInputStream();var h=Wire.header(ByteBuffer.wrap(in.readNBytes(Wire.HEADER_BYTES)));
        assertEquals(Wire.HELLO,h.type());var r=new Wire.Reader(in.readNBytes(h.size()));
        assertArrayEquals(KEY,r.bytes(16));assertArrayEquals(KEY,r.bytes(16));r.i32();int caps=r.i32();r.finish();return caps;
    }
    private static void welcome(Socket s,int caps,boolean client)throws Exception{
        var p=new Wire.Writer().bytes(KEY).i32(client?Wire.HOST_CLIENT:Wire.HOST_SERVER).i32(caps).f32(Wire.UNITS_PER_BLOCK).f32(Wire.Y_OFFSET).toByteArray();
        s.getOutputStream().write(Wire.frameHeader(Wire.WELCOME,41,1,p.length).array());s.getOutputStream().write(p);s.getOutputStream().flush();
    }
    @Test void currentServerGenerationOnly()throws Exception{
        try(var host=host();var link=link(host,false)){
            long previous;
            try(var s=host.accept()){
                assertNotEquals(0,hello(s)&Wire.NATIVE_MOVEMENT_CAP);welcome(s,Wire.NATIVE_MOVEMENT_CAP,false);until(link::connected);
                previous=link.generation();assertFalse(link.nativeMovement(0));assertTrue(link.nativeMovement(previous));
                byte[] payload={1,2,3};s.getOutputStream().write(Wire.frameHeader(Wire.NATIVE_MOVEMENT,41,2,3).array());s.getOutputStream().write(payload);s.getOutputStream().flush();
                var delivered=new java.util.ArrayList<BridgeLink.Received>();
                until(()->{var batch=link.drainWithContext();assertEquals(previous,batch.generation());delivered.addAll(batch.messages());return !delivered.isEmpty();});
                assertTrue(delivered.stream().anyMatch(m->m.message().type()==Wire.NATIVE_MOVEMENT&&java.util.Arrays.equals(payload,m.message().payload())));
            }
            until(()->!link.connected());assertFalse(link.nativeMovement(previous));
            try(var s=host.accept()){
                hello(s);welcome(s,Wire.NATIVE_VIEW_CAP,false);until(link::connected);
                assertFalse(link.nativeMovement(link.generation()));assertFalse(link.nativeMovement(previous));
            }
        }
    }
    @Test void oldHostRejectsUnknownBitThenAcceptsOriginalOffer()throws Exception{
        try(var host=host();var link=link(host,false)){
            try(var knockback=host.accept()){hello(knockback);}
            int offered;
            try(var s=host.accept()){offered=hello(s);assertNotEquals(0,offered&Wire.NATIVE_MOVEMENT_CAP);}
            try(var s=host.accept()){
                assertEquals(offered&~Wire.NATIVE_MOVEMENT_CAP,hello(s));welcome(s,Wire.NATIVE_WATER_CAP,false);until(link::connected);
                assertFalse(link.nativeMovement(link.generation()));assertTrue(link.nativeWater(link.generation()));
            }
        }
    }
    @Test void graphicalClientCannotNegotiateMovement()throws Exception{
        try(var host=host();var link=link(host,true);var s=host.accept()){
            assertEquals(0,hello(s)&Wire.NATIVE_MOVEMENT_CAP);welcome(s,Wire.NATIVE_MOVEMENT_CAP,true);
            until(()->link.lastError().contains("contract mismatch"));assertFalse(link.connected());assertFalse(link.nativeMovement(link.generation()));
        }
    }
}
