package dev.goldcraft.bridge;

import org.junit.jupiter.api.Test;
import java.net.*;
import java.nio.ByteBuffer;
import java.time.Duration;
import java.util.function.BooleanSupplier;
import static org.junit.jupiter.api.Assertions.*;

class ViewDistortionHandshakeTest {
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
    @Test void currentConnectionAndGenerationOnly()throws Exception{
        try(var host=host();var link=link(host.getLocalPort(),true)){
            long old;
            try(var s=host.accept()){
                assertNotEquals(0,hello(s)&Wire.VIEW_DISTORTION_CAP);welcome(s,Wire.VIEW_DISTORTION_CAP,Wire.HOST_CLIENT);until(link::connected);old=link.generation();
                assertTrue(link.viewDistortion(old));assertFalse(link.viewDistortion(0));assertFalse(link.sendViewDistortion(old+1,new byte[32]));
                assertTrue(link.sendViewDistortion(old,new byte[32]));Wire.Message m;do{m=read(s);}while(m.type()==Wire.HEARTBEAT);
                assertEquals(Wire.VIEW_DISTORTION,m.type());assertEquals(32,m.payload().length);
            }
            until(()->!link.connected());assertFalse(link.sendViewDistortion(old,new byte[32]));
            try(var s=host.accept()){assertNotEquals(0,hello(s)&Wire.VIEW_DISTORTION_CAP);welcome(s,Wire.VIEW_DISTORTION_CAP,Wire.HOST_CLIENT);until(link::connected);assertFalse(link.viewDistortion(old));assertTrue(link.viewDistortion(link.generation()));}
        }
    }
    @Test void legacyHostRetryKeepsExistingClientFeatures()throws Exception{
        try(var host=host();var link=link(host.getLocalPort(),true)){
            int first;try(var s=host.accept()){first=hello(s);assertNotEquals(0,first&Wire.VIEW_DISTORTION_CAP);}
            try(var s=host.accept()){assertEquals(first&~Wire.HSV_POST_CAP,hello(s));}
            first &= ~Wire.HSV_POST_CAP;
            try(var s=host.accept()){assertEquals(first&~Wire.ENTITY_OUTLINES_CAP,hello(s));}
            try(var s=host.accept()){assertEquals(first&~(Wire.VIEW_DISTORTION_CAP|Wire.ENTITY_OUTLINES_CAP),hello(s));welcome(s,0,Wire.HOST_CLIENT);until(link::connected);assertFalse(link.viewDistortion(link.generation()));assertTrue(link.send(Wire.CAMERA,new byte[0]));}
        }
    }
    @Test void rejectsUnrequestedAndWrongRole()throws Exception{
        try(var host=host();var link=link(host.getLocalPort(),true)){
            try(var s=host.accept()){hello(s);}
            try(var s=host.accept()){hello(s);}
            try(var s=host.accept()){hello(s);}
            try(var s=host.accept()){assertEquals(0,hello(s)&Wire.VIEW_DISTORTION_CAP);welcome(s,Wire.VIEW_DISTORTION_CAP,Wire.HOST_CLIENT);until(()->link.lastError().contains("Unrequested view distortion"));assertFalse(link.connected());}
        }
        try(var host=host();var link=link(host.getLocalPort(),false);var s=host.accept()){
            assertEquals(0,hello(s)&Wire.VIEW_DISTORTION_CAP);welcome(s,Wire.VIEW_DISTORTION_CAP,Wire.HOST_SERVER);until(()->link.lastError().contains("Unrequested view distortion"));assertFalse(link.connected());
        }
    }
}
