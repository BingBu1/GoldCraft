package dev.goldcraft.bridge;

import org.junit.jupiter.api.Test;
import java.net.InetAddress;
import java.net.ServerSocket;
import java.net.Socket;
import java.nio.ByteBuffer;
import java.time.Duration;
import java.util.function.BooleanSupplier;
import static org.junit.jupiter.api.Assertions.*;

class NativeWaterHandshakeTest {
    private static final byte[] KEY=new byte[16];
    private static void until(BooleanSupplier condition){assertTimeoutPreemptively(Duration.ofSeconds(4),()->{while(!condition.getAsBoolean())Thread.sleep(5);});}
    private static ServerSocket host()throws Exception{var s=new ServerSocket(0,4,InetAddress.getLoopbackAddress());s.setSoTimeout(4000);return s;}
    private static BridgeLink link(ServerSocket host,boolean client){return new BridgeLink(new BridgeLink.Config(host.getLocalPort(),KEY,KEY,client?Wire.FABRIC_CLIENT:Wire.FABRIC_SERVER,client?Wire.HOST_CLIENT:Wire.HOST_SERVER));}
    private static Wire.Message read(Socket s)throws Exception{
        s.setSoTimeout(4000);var in=s.getInputStream();var h=Wire.header(ByteBuffer.wrap(in.readNBytes(Wire.HEADER_BYTES)));
        var p=in.readNBytes(h.size());assertEquals(h.size(),p.length);return new Wire.Message(h.type(),p);
    }
    private static int hello(Socket s)throws Exception{
        var m=read(s);assertEquals(Wire.HELLO,m.type());var r=new Wire.Reader(m.payload());
        assertArrayEquals(KEY,r.bytes(16));assertArrayEquals(KEY,r.bytes(16));r.i32();int caps=r.i32();r.finish();return caps;
    }
    private static void welcome(Socket s,int caps,boolean client)throws Exception{
        var p=new Wire.Writer().bytes(KEY).i32(client?Wire.HOST_CLIENT:Wire.HOST_SERVER).i32(caps).f32(Wire.UNITS_PER_BLOCK).f32(Wire.Y_OFFSET).toByteArray();
        s.getOutputStream().write(Wire.frameHeader(Wire.WELCOME,41,1,p.length).array());s.getOutputStream().write(p);s.getOutputStream().flush();
    }
    @Test void nativeWaterRequiresNegotiationAndCurrentServerGeneration()throws Exception{
        try(var host=host();var link=link(host,false)){
            long previous;
            try(var s=host.accept()){
                assertNotEquals(0,hello(s)&Wire.NATIVE_WATER_CAP);welcome(s,Wire.NATIVE_WATER_CAP,false);until(link::connected);
                previous=link.generation();assertFalse(link.sendWater(0,new byte[0]));
                assertTrue(link.sendWater(previous,new byte[]{1,2,3}));
                Wire.Message m;do{m=read(s);}while(m.type()==Wire.HEARTBEAT);
                assertEquals(Wire.NATIVE_WATER,m.type());assertArrayEquals(new byte[]{1,2,3},m.payload());
            }
            until(()->!link.connected());assertFalse(link.sendWater(previous,new byte[0]));
            try(var s=host.accept()){
                hello(s);welcome(s,Wire.NATIVE_HIT_CAP,false);until(link::connected);
                assertFalse(link.nativeWater(link.generation()));assertFalse(link.sendWater(link.generation(),new byte[0]));
            }
        }
    }
    @Test void graphicalClientCannotSendWater()throws Exception{
        try(var host=host();var link=link(host,true);var s=host.accept()){
            assertEquals(0,hello(s)&Wire.NATIVE_WATER_CAP);welcome(s,Wire.NATIVE_WATER_CAP,true);
            until(()->link.lastError().contains("contract mismatch"));assertFalse(link.connected());
            assertFalse(link.sendWater(link.generation(),new byte[0]));
        }
    }
}
