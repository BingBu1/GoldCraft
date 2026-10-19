package dev.goldcraft.bridge;

import org.junit.jupiter.api.Test;
import java.net.InetAddress;
import java.net.ServerSocket;
import java.net.Socket;
import java.nio.ByteBuffer;
import java.time.Duration;
import java.util.function.BooleanSupplier;
import static org.junit.jupiter.api.Assertions.*;

class NativeHitHandshakeTest {
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
    @Test void nativeHitTransportFencesReconnectAndCountsNewType()throws Exception{
        try(var host=host();var link=link(host,false)){
            long old;
            try(var s=host.accept()){
                assertNotEquals(0,hello(s)&Wire.NATIVE_HIT_CAP);welcome(s,Wire.NATIVE_HIT_CAP,false);until(link::connected);
                old=link.generation();assertTrue(link.nativeHit(old));assertFalse(link.nativeHit(0));
                var payload=new Wire.Writer().i64(41).i64(1).i32(0).toByteArray();
                s.getOutputStream().write(Wire.frameHeader(Wire.NATIVE_HIT,41,2,payload.length).array());
                s.getOutputStream().write(payload);s.getOutputStream().flush();
                var received=new java.util.ArrayList<Wire.Message>();
                until(()->{received.addAll(link.drain());return !received.isEmpty();});
                assertEquals(Wire.NATIVE_HIT,received.getFirst().type());assertArrayEquals(payload,received.getFirst().payload());
            }
            until(()->!link.connected());assertFalse(link.nativeHit(old));
            try(var s=host.accept()){
                hello(s);welcome(s,Wire.NATIVE_HIT_CAP,false);until(link::connected);
                assertNotEquals(old,link.generation());assertFalse(link.nativeHit(old));assertTrue(link.nativeHit(link.generation()));
            }
        }
    }
    @Test void oldHostDropsOnlyNativeHitAndKeepsNativeView()throws Exception{
        try(var host=host();var link=link(host,false)){
            try(var absorbed=host.accept()){hello(absorbed);} // reject absorbed-hit extension
            try(var knockback=host.accept()){hello(knockback);} // reject native-knockback extension
            try(var movement=host.accept()){hello(movement);} // reject native-movement extension
            try(var water=host.accept()){hello(water);} // reject native-water extension
            try(var s=host.accept()){assertNotEquals(0,hello(s)&Wire.NATIVE_IMPULSE_CAP);}
            int first;try(var s=host.accept()){first=hello(s);assertNotEquals(0,first&Wire.NATIVE_HIT_CAP);}
            try(var s=host.accept()){
                assertEquals(first&~Wire.NATIVE_HIT_CAP,hello(s));welcome(s,Wire.NATIVE_VIEW_CAP,false);until(link::connected);
                assertFalse(link.nativeHit(link.generation()));assertTrue(link.nativeView(link.generation()));
            }
        }
    }
    @Test void oldHostCannotAcceptUnrequestedNativeHit()throws Exception{
        try(var host=host();var link=link(host,false)){
            try(var absorbed=host.accept()){hello(absorbed);} // reject absorbed-hit extension
            try(var knockback=host.accept()){hello(knockback);} // reject native-knockback extension
            try(var movement=host.accept()){hello(movement);} // reject native-movement extension
            try(var water=host.accept()){hello(water);} // reject native-water extension
            try(var s=host.accept()){assertNotEquals(0,hello(s)&Wire.NATIVE_IMPULSE_CAP);}
            try(var s=host.accept()){hello(s);}
            try(var s=host.accept()){
                assertEquals(0,hello(s)&Wire.NATIVE_HIT_CAP);welcome(s,Wire.NATIVE_HIT_CAP,false);
                until(()->link.lastError().contains("Unrequested native hit"));assertFalse(link.connected());
            }
        }
    }
    @Test void graphicalClientCannotAuthorNativeHit()throws Exception{
        try(var host=host();var link=link(host,true);var s=host.accept()){
            assertEquals(0,hello(s)&Wire.NATIVE_HIT_CAP);welcome(s,Wire.NATIVE_HIT_CAP,true);
            until(()->link.lastError().contains("contract mismatch"));assertFalse(link.connected());assertFalse(link.nativeHit(link.generation()));
        }
    }
    @Test void completeFallbackCycleRetriesNewestCapability()throws Exception{
        try(var host=host();var link=link(host,false)){
            for(int offer=19;offer>=0;offer--)try(var s=host.accept()){
                int caps=hello(s);assertEquals(offer>=15,(caps&Wire.NATIVE_IMPULSE_CAP)!=0);assertEquals(offer>=14,(caps&Wire.NATIVE_HIT_CAP)!=0);
                assertEquals(offer>=13,(caps&Wire.NATIVE_VIEW_CAP)!=0);
            }
            try(var s=host.accept()){
                assertNotEquals(0,hello(s)&Wire.NATIVE_HIT_CAP);welcome(s,Wire.NATIVE_HIT_CAP,false);
                until(link::connected);assertTrue(link.nativeHit(link.generation()));
            }
        }
    }
    @Test void nativeImpulseRequiresNegotiationAndCurrentServerGeneration()throws Exception{
        try(var host=host();var link=link(host,false)){
            long previous;
            try(var s=host.accept()){
                assertNotEquals(0,hello(s)&Wire.NATIVE_IMPULSE_CAP);welcome(s,Wire.NATIVE_IMPULSE_CAP,false);until(link::connected);
                previous=link.generation();assertFalse(link.sendNativeImpulse(0,new byte[0]));
                assertTrue(link.sendNativeImpulse(previous,new byte[]{1,2,3}));
                Wire.Message m;do{m=read(s);}while(m.type()==Wire.HEARTBEAT);
                assertEquals(Wire.NATIVE_IMPULSE,m.type());assertArrayEquals(new byte[]{1,2,3},m.payload());
            }
            until(()->!link.connected());assertFalse(link.sendNativeImpulse(previous,new byte[0]));
            try(var s=host.accept()){
                hello(s);welcome(s,Wire.NATIVE_HIT_CAP,false);until(link::connected);
                assertFalse(link.nativeImpulse(link.generation()));assertFalse(link.sendNativeImpulse(link.generation(),new byte[0]));
            }
        }
    }
    @Test void graphicalClientCannotSendNativeImpulse()throws Exception{
        try(var host=host();var link=link(host,true);var s=host.accept()){
            assertEquals(0,hello(s)&Wire.NATIVE_IMPULSE_CAP);welcome(s,Wire.NATIVE_IMPULSE_CAP,true);
            until(()->link.lastError().contains("contract mismatch"));assertFalse(link.connected());
            assertFalse(link.sendNativeImpulse(link.generation(),new byte[0]));
        }
    }
}
