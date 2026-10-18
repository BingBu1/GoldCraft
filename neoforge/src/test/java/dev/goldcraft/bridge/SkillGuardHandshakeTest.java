package dev.goldcraft.bridge;

import org.junit.jupiter.api.Test;
import java.net.InetAddress;
import java.net.ServerSocket;
import java.net.Socket;
import java.nio.ByteBuffer;
import java.time.Duration;
import java.util.function.BooleanSupplier;
import static org.junit.jupiter.api.Assertions.*;

class SkillGuardHandshakeTest {
    private static final byte[] KEY=new byte[16];
    private static void until(BooleanSupplier condition){assertTimeoutPreemptively(Duration.ofSeconds(4),()->{while(!condition.getAsBoolean())Thread.sleep(5);});}
    private static ServerSocket host()throws Exception{var s=new ServerSocket(0,4,InetAddress.getLoopbackAddress());s.setSoTimeout(4000);return s;}
    private static BridgeLink link(ServerSocket host,boolean client){return new BridgeLink(new BridgeLink.Config(host.getLocalPort(),KEY,KEY,client?Wire.FABRIC_CLIENT:Wire.FABRIC_SERVER,client?Wire.HOST_CLIENT:Wire.HOST_SERVER));}
    private static int hello(Socket s)throws Exception{
        s.setSoTimeout(4000);var in=s.getInputStream();var h=Wire.header(ByteBuffer.wrap(in.readNBytes(Wire.HEADER_BYTES)));assertEquals(Wire.HELLO,h.type());
        var r=new Wire.Reader(in.readNBytes(h.size()));r.bytes(16);r.bytes(16);r.i32();return r.i32();
    }
    private static void welcome(Socket s,int caps,boolean client)throws Exception{
        var p=new Wire.Writer().bytes(KEY).i32(client?Wire.HOST_CLIENT:Wire.HOST_SERVER).i32(caps).f32(Wire.UNITS_PER_BLOCK).f32(Wire.Y_OFFSET).toByteArray();
        s.getOutputStream().write(Wire.frameHeader(Wire.WELCOME,41,1,p.length).array());s.getOutputStream().write(p);s.getOutputStream().flush();
    }
    @Test void authenticatedGuardExpiresWithConnectionAndGeneration()throws Exception{
        try(var host=host();var link=link(host,false)){
            long old;
            try(var s=host.accept()){
                assertNotEquals(0,hello(s)&Wire.SKILL_GUARD_CAP);welcome(s,Wire.PLAYER_GAMEPLAY_CAP|Wire.SKILL_GUARD_CAP,false);until(link::connected);
                old=link.generation();assertTrue(link.skillGuard(old));assertFalse(link.skillGuard(0));
            }
            until(()->!link.connected());assertFalse(link.skillGuard(old));
            try(var s=host.accept()){
                hello(s);welcome(s,Wire.PLAYER_GAMEPLAY_CAP|Wire.SKILL_GUARD_CAP,false);until(link::connected);
                assertNotEquals(old,link.generation());assertFalse(link.skillGuard(old));assertTrue(link.skillGuard(link.generation()));
            }
        }
    }
    @Test void oldHostDropsOnlyGuardAndKeepsGameplay()throws Exception{
        try(var host=host();var link=link(host,false)){
            try(var s=host.accept()){assertNotEquals(0,hello(s)&Wire.STATUS_MELEE_CAP);}
            try(var s=host.accept()){assertNotEquals(0,hello(s)&Wire.STATUS_NAUSEA_CAP);}
            try(var s=host.accept()){assertNotEquals(0,hello(s)&Wire.STATUS_GLOW_CAP);}
            try(var s=host.accept()){assertNotEquals(0,hello(s)&Wire.STATUS_HEALTH_CAP);}
            try(var s=host.accept()){assertNotEquals(0,hello(s)&Wire.STATUS_FALL_CAP);}
            int first;try(var s=host.accept()){first=hello(s);}
            try(var s=host.accept()){
                assertEquals(first&~Wire.SKILL_GUARD_CAP,hello(s));welcome(s,Wire.PLAYER_GAMEPLAY_CAP|Wire.STATUS_VISION_CAP,false);until(link::connected);
                assertFalse(link.skillGuard(link.generation()));assertTrue(link.playerGameplay(link.generation()));assertTrue(link.vision(link.generation()));
            }
        }
    }
    @Test void guardCannotBeAcceptedWithoutGameplay()throws Exception{
        try(var host=host();var link=link(host,false);var s=host.accept()){
            hello(s);welcome(s,Wire.SKILL_GUARD_CAP,false);until(()->link.lastError().contains("incomplete skill guard"));assertFalse(link.connected());
        }
    }
    @Test void oldHostCannotAcceptUnrequestedGuard()throws Exception{
        try(var host=host();var link=link(host,false)){
            try(var s=host.accept()){assertNotEquals(0,hello(s)&Wire.STATUS_MELEE_CAP);}
            try(var s=host.accept()){assertNotEquals(0,hello(s)&Wire.STATUS_NAUSEA_CAP);}
            try(var s=host.accept()){assertNotEquals(0,hello(s)&Wire.STATUS_GLOW_CAP);}
            try(var s=host.accept()){assertNotEquals(0,hello(s)&Wire.STATUS_HEALTH_CAP);}
            try(var s=host.accept()){assertNotEquals(0,hello(s)&Wire.STATUS_FALL_CAP);}
            try(var s=host.accept()){hello(s);}
            try(var s=host.accept()){
                assertEquals(0,hello(s)&Wire.SKILL_GUARD_CAP);welcome(s,Wire.SKILL_GUARD_CAP|Wire.PLAYER_GAMEPLAY_CAP,false);
                until(()->link.lastError().contains("Unrequested"));assertFalse(link.connected());
            }
        }
    }
    @Test void clientCannotClaimGuard()throws Exception{
        try(var host=host();var link=link(host,true);var s=host.accept()){
            assertEquals(0,hello(s)&Wire.SKILL_GUARD_CAP);welcome(s,Wire.PLAYER_GAMEPLAY_CAP|Wire.SKILL_GUARD_CAP,true);
            until(()->!link.lastError().isBlank());assertFalse(link.connected());
        }
    }
}
