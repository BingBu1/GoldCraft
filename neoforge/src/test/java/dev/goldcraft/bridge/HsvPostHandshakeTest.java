package dev.goldcraft.bridge;

import org.junit.jupiter.api.Test;
import java.net.*;
import java.nio.ByteBuffer;
import java.time.Duration;
import java.util.function.BooleanSupplier;
import static org.junit.jupiter.api.Assertions.*;

class HsvPostHandshakeTest {
    private static final byte[] SESSION=Wire.key("00112233445566778899aabbccddeeff"),TOKEN=Wire.key("ffeeddccbbaa99887766554433221100");
    private static void until(BooleanSupplier condition){assertTimeoutPreemptively(Duration.ofSeconds(4),()->{while(!condition.getAsBoolean())Thread.sleep(5);});}
    private static ServerSocket host()throws Exception{return new ServerSocket(0,4,InetAddress.getLoopbackAddress());}
    private static BridgeLink link(int port,boolean client){return new BridgeLink(new BridgeLink.Config(port,SESSION,TOKEN,client?Wire.FABRIC_CLIENT:Wire.FABRIC_SERVER,client?Wire.HOST_CLIENT:Wire.HOST_SERVER));}
    private static Wire.Message read(Socket s)throws Exception{
        s.setSoTimeout(4000);var in=s.getInputStream();var h=Wire.header(ByteBuffer.wrap(in.readNBytes(Wire.HEADER_BYTES)));
        return new Wire.Message(h.type(),in.readNBytes(h.size()));
    }
    private static int hello(Socket s)throws Exception{var m=read(s);assertEquals(Wire.HELLO,m.type());var r=new Wire.Reader(m.payload());r.bytes(32);r.i32();int caps=r.i32();r.finish();return caps;}
    private static void welcome(Socket s,int caps,int role)throws Exception{
        var p=new Wire.Writer().bytes(SESSION).i32(role).i32(caps).f32(Wire.UNITS_PER_BLOCK).f32(Wire.Y_OFFSET).toByteArray();
        s.getOutputStream().write(Wire.frameHeader(Wire.WELCOME,19,1,p.length).array());s.getOutputStream().write(p);s.getOutputStream().flush();
    }
    @Test void acceptanceIsBoundToConnectionGeneration()throws Exception{
        try(var host=host();var link=link(host.getLocalPort(),true)){
            long old;
            try(var s=host.accept()){
                assertNotEquals(0,hello(s)&Wire.HSV_POST_CAP);welcome(s,Wire.HSV_POST_CAP,Wire.HOST_CLIENT);until(link::connected);old=link.generation();
                assertTrue(link.hsvPost(old));assertFalse(link.hsvPost(0));assertFalse(link.sendPostEffects(old+1,true,new byte[0]));
                assertTrue(link.sendPostEffects(old,true,new byte[]{8}));Wire.Message m;do{m=read(s);}while(m.type()==Wire.HEARTBEAT);
                assertEquals(Wire.POST_EFFECT_PLAN,m.type());assertArrayEquals(new byte[]{8},m.payload());
            }
            until(()->!link.connected());assertFalse(link.hsvPost(old));
            try(var s=host.accept()){
                hello(s);welcome(s,0,Wire.HOST_CLIENT);until(link::connected);
                assertFalse(link.hsvPost(old));assertFalse(link.hsvPost(link.generation()));
                assertFalse(link.sendPostEffects(link.generation(),true,new byte[0]));
                assertTrue(link.sendPostEffects(link.generation(),false,new byte[0]));
            }
        }
    }
    @Test void oldHostRetryOnlyRemovesHsvAndRejectsUnrequestedAcknowledgement()throws Exception{
        try(var host=host();var link=link(host.getLocalPort(),true)){
            int first;try(var s=host.accept()){first=hello(s);assertNotEquals(0,first&Wire.HSV_POST_CAP);}
            try(var s=host.accept()){
                assertEquals(first&~Wire.HSV_POST_CAP,hello(s));welcome(s,Wire.HSV_POST_CAP,Wire.HOST_CLIENT);
                until(()->link.lastError().contains("Unrequested HSV"));assertFalse(link.connected());
            }
        }
    }
    @Test void serverStatusBitDoesNotGrantClientPresentation()throws Exception{
        try(var host=host();var link=link(host.getLocalPort(),false);var s=host.accept()){
            assertNotEquals(0,hello(s)&Wire.STATUS_EFFECTS);welcome(s,Wire.STATUS_EFFECTS,Wire.HOST_SERVER);until(link::connected);
            assertFalse(link.hsvPost(link.generation()));assertFalse(link.sendPostEffects(link.generation(),true,new byte[0]));
            assertFalse(link.sendPostEffects(link.generation(),false,new byte[0]));
        }
    }
}
