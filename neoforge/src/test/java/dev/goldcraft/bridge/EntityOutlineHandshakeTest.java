package dev.goldcraft.bridge;

import org.junit.jupiter.api.Test;
import java.net.*;
import java.nio.ByteBuffer;
import java.time.Duration;
import java.util.function.BooleanSupplier;
import static org.junit.jupiter.api.Assertions.*;

class EntityOutlineHandshakeTest {
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
    @Test void negotiatedFrameAndRetiredGeneration()throws Exception{
        try(var host=host();var link=link(host.getLocalPort(),true)){
            long old;
            try(var s=host.accept()){
                assertNotEquals(0,hello(s)&Wire.ENTITY_OUTLINES_CAP);welcome(s,Wire.ENTITY_OUTLINES_CAP,Wire.HOST_CLIENT);until(link::connected);old=link.generation();
                assertTrue(link.entityOutlines(old));assertFalse(link.entityOutlines(0));assertFalse(link.sendEntityFrame(old+1,true,new byte[0]));
                assertTrue(link.sendEntityFrame(old,true,new byte[]{1,2,3}));Wire.Message m;do{m=read(s);}while(m.type()==Wire.HEARTBEAT);
                assertEquals(Wire.ENTITY_OUTLINE_FRAME,m.type());assertArrayEquals(new byte[]{1,2,3},m.payload());
            }
            until(()->!link.connected());assertFalse(link.sendEntityFrame(old,true,new byte[0]));
            try(var s=host.accept()){
                hello(s);welcome(s,Wire.ENTITY_OUTLINES_CAP,Wire.HOST_CLIENT);until(link::connected);
                assertFalse(link.entityOutlines(old));assertFalse(link.sendEntityFrame(old,false,new byte[0]));assertTrue(link.entityOutlines(link.generation()));
            }
        }
    }
    @Test void olderHostRetainsViewDistortionAndOldEntityFrames()throws Exception{
        try(var host=host();var link=link(host.getLocalPort(),true)){
            int first;try(var s=host.accept()){first=hello(s);assertNotEquals(0,first&Wire.ENTITY_OUTLINES_CAP);}
            try(var s=host.accept()){assertEquals(first&~Wire.HSV_POST_CAP,hello(s));}
            first &= ~Wire.HSV_POST_CAP;
            try(var s=host.accept()){
                assertEquals(first&~Wire.ENTITY_OUTLINES_CAP,hello(s));welcome(s,Wire.VIEW_DISTORTION_CAP,Wire.HOST_CLIENT);until(link::connected);
                long generation=link.generation();assertTrue(link.viewDistortion(generation));assertFalse(link.entityOutlines(generation));
                assertFalse(link.sendEntityFrame(generation,true,new byte[0]));assertTrue(link.sendEntityFrame(generation,false,new byte[]{4}));
                Wire.Message m;do{m=read(s);}while(m.type()==Wire.HEARTBEAT);assertEquals(Wire.ENTITY_FRAME,m.type());
            }
        }
    }
    @Test void rejectsUnrequestedAndServerRole()throws Exception{
        try(var host=host();var link=link(host.getLocalPort(),true)){
            try(var s=host.accept()){hello(s);}
            try(var s=host.accept()){hello(s);}
            try(var s=host.accept()){
                assertEquals(0,hello(s)&Wire.ENTITY_OUTLINES_CAP);welcome(s,Wire.ENTITY_OUTLINES_CAP,Wire.HOST_CLIENT);
                until(()->link.lastError().contains("Unrequested entity outlines"));assertFalse(link.connected());
            }
        }
        try(var host=host();var link=link(host.getLocalPort(),false);var s=host.accept()){
            assertEquals(0,hello(s)&Wire.ENTITY_OUTLINES_CAP);welcome(s,Wire.ENTITY_OUTLINES_CAP,Wire.HOST_SERVER);
            until(()->link.lastError().contains("Unrequested entity outlines"));assertFalse(link.connected());assertFalse(link.sendEntityFrame(link.generation(),false,new byte[0]));
        }
    }
}
