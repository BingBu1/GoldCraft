package dev.goldcraft.bridge;

import org.junit.jupiter.api.Test;
import java.net.InetAddress;
import java.net.ServerSocket;
import java.net.Socket;
import java.nio.ByteBuffer;
import java.time.Duration;
import java.util.function.BooleanSupplier;
import static org.junit.jupiter.api.Assertions.*;

class HealthHandshakeTest {
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
    @Test void healthTransportFencesReconnectAndCountsNewType()throws Exception{
        try(var host=host();var link=link(host,false)){
            long old;
            try(var s=host.accept()){
                assertNotEquals(0,hello(s)&Wire.STATUS_HEALTH_CAP);welcome(s,Wire.STATUS_HEALTH_CAP,false);until(link::connected);
                old=link.generation();assertTrue(link.healthLimits(old));assertFalse(link.healthLimits(0));
                var payload=new Wire.Writer().i64(41).i64(1).i32(1).i32(2).i32(3).i32(4).f32(5).f32(.5f).toByteArray();
                assertEquals(40,payload.length);assertTrue(link.sendHealthLimit(old,payload));
                Wire.Message m;do{m=read(s);}while(m.type()==Wire.HEARTBEAT);
                assertEquals(Wire.STATUS_HEALTH,m.type());assertArrayEquals(payload,m.payload());
                until(()->link.metrics().tcpFrames()[Wire.STATUS_HEALTH]==1);assertEquals(40,link.metrics().tcpBytes()[Wire.STATUS_HEALTH]);
            }
            until(()->!link.connected());assertFalse(link.sendHealthLimit(old,new byte[0]));
            try(var s=host.accept()){
                hello(s);welcome(s,Wire.STATUS_HEALTH_CAP,false);until(link::connected);
                assertNotEquals(old,link.generation());assertFalse(link.healthLimits(old));assertTrue(link.healthLimits(link.generation()));
            }
        }
    }
    @Test void oldHostDropsOnlyHealthAndKeepsFall()throws Exception{
        try(var host=host();var link=link(host,false)){
            try(var s=host.accept()){assertNotEquals(0,hello(s)&Wire.STATUS_MELEE_CAP);}
            try(var s=host.accept()){assertNotEquals(0,hello(s)&Wire.STATUS_NAUSEA_CAP);}
            try(var s=host.accept()){assertNotEquals(0,hello(s)&Wire.STATUS_GLOW_CAP);}
            int first;try(var s=host.accept()){first=hello(s);assertNotEquals(0,first&Wire.STATUS_HEALTH_CAP);}
            try(var s=host.accept()){
                assertEquals(first&~Wire.STATUS_HEALTH_CAP,hello(s));welcome(s,Wire.STATUS_FALL_CAP,false);until(link::connected);
                assertFalse(link.healthLimits(link.generation()));assertFalse(link.sendHealthLimit(link.generation(),new byte[0]));assertTrue(link.fall(link.generation()));
            }
        }
    }
    @Test void oldHostCannotAcceptUnrequestedHealth()throws Exception{
        try(var host=host();var link=link(host,false)){
            try(var s=host.accept()){assertNotEquals(0,hello(s)&Wire.STATUS_MELEE_CAP);}
            try(var s=host.accept()){assertNotEquals(0,hello(s)&Wire.STATUS_NAUSEA_CAP);}
            try(var s=host.accept()){assertNotEquals(0,hello(s)&Wire.STATUS_GLOW_CAP);}
            try(var s=host.accept()){hello(s);}
            try(var s=host.accept()){
                assertEquals(0,hello(s)&Wire.STATUS_HEALTH_CAP);welcome(s,Wire.STATUS_HEALTH_CAP,false);
                until(()->link.lastError().contains("Unrequested health limit"));assertFalse(link.connected());
            }
        }
    }
    @Test void graphicalClientCannotAuthorHealthLimits()throws Exception{
        try(var host=host();var link=link(host,true);var s=host.accept()){
            assertEquals(0,hello(s)&Wire.STATUS_HEALTH_CAP);welcome(s,Wire.STATUS_HEALTH_CAP,true);
            until(()->link.lastError().contains("contract mismatch"));assertFalse(link.connected());assertFalse(link.healthLimits(link.generation()));
        }
    }
}
