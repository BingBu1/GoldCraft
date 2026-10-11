package dev.goldcraft.bridge;

import java.lang.reflect.Field;
import java.net.InetAddress;
import java.net.ServerSocket;
import java.util.ArrayDeque;
import java.util.List;
import org.junit.jupiter.api.Test;
import static org.junit.jupiter.api.Assertions.*;

class BridgeLinkTest {
    private static Field field(String name) throws Exception {var field=BridgeLink.class.getDeclaredField(name);field.setAccessible(true);return field;}
    // The production send implementation runs against a stopped worker. This isolates queue
    // semantics, not socket delivery, without adding a test-only production constructor.
    private static BridgeLink queue() throws Exception {
        try(var peer=new ServerSocket(0,1,InetAddress.getLoopbackAddress())){
            var link=new BridgeLink(new BridgeLink.Config(peer.getLocalPort(),new byte[16],new byte[16],Wire.FABRIC_CLIENT,Wire.HOST_CLIENT));
            link.close();assertFalse(((Thread)field("worker").get(link)).isAlive());field("connected").setBoolean(link,true);return link;
        }
    }
    @SuppressWarnings("unchecked") private static List<Wire.Message> messages(BridgeLink link) throws Exception {
        return List.copyOf((ArrayDeque<Wire.Message>)field("outgoing").get(link));
    }
    private static int bytes(BridgeLink link) throws Exception {return field("outboundBytes").getInt(link);}
    private static void accounting(BridgeLink link) throws Exception {
        assertEquals(messages(link).stream().mapToInt(m->m.payload().length+Wire.HEADER_BYTES).sum(),bytes(link));
    }
    @Test void completeFrameCoalescesWithoutMixingParticleRevisionOrTextureOrdering() throws Exception {
        try(var link=queue()){
            byte[] original=EntityFrameTest.frame(11,1,false);
            assertTrue(link.send(Wire.PARTICLE_TEXTURE,new byte[]{1}));
            assertTrue(link.send(Wire.ENTITY_FRAME,original));
            assertTrue(link.send(Wire.PARTICLE_MESH,ParticleSnapshot.encode(9,2,999,7,0,List.of())));
            assertTrue(link.send(Wire.PARTICLE_TEXTURE,new byte[]{2}));
            byte[] replacement=EntityFrameTest.frame(12,2,false);assertTrue(link.send(Wire.ENTITY_FRAME,replacement));replacement[0]=0;
            var sent=messages(link);assertEquals(List.of(Wire.PARTICLE_TEXTURE,Wire.PARTICLE_MESH,Wire.PARTICLE_TEXTURE,Wire.ENTITY_FRAME),sent.stream().map(Wire.Message::type).toList());
            EntityFrameTest.assertPair(sent.getLast().payload(),12,2,false);accounting(link);
            assertTrue(link.send(Wire.ENTITY_FRAME,EntityFrameTest.frame(13,0,true)));
            EntityFrameTest.assertPair(messages(link).getLast().payload(),13,0,true);accounting(link);
        }
    }
    @Test void rejectedReplacementKeepsOldCompleteFrameAndQueueAccounting() throws Exception {
        try(var link=queue()){
            assertTrue(link.send(Wire.ENTITY_FRAME,EntityFrameTest.frame(11,1,false)));
            assertTrue(link.send(Wire.ENTITY_TEXTURE,new byte[Wire.MAX_PAYLOAD]));
            assertTrue(link.send(Wire.ENTITY_TEXTURE,new byte[Wire.MAX_QUEUED-bytes(link)-Wire.HEADER_BYTES]));
            var old=messages(link);assertEquals(Wire.MAX_QUEUED,bytes(link));
            byte[] larger=EntityFrame.encode(9,2,12,7,EntityFrameTest.base(9,12,2,false),List.of(EntityFrameTest.glint(2),EntityFrameTest.glint(2)));
            assertFalse(link.send(Wire.ENTITY_FRAME,larger));assertEquals(old,messages(link));accounting(link);
            EntityFrameTest.assertPair(messages(link).getFirst().payload(),11,1,false);
            field("connected").setBoolean(link,false);assertFalse(link.send(Wire.ENTITY_FRAME,EntityFrameTest.frame(12,2,false)));
            assertEquals(old,messages(link));accounting(link);
        }
    }
    @Test void fullEntryQueueCanReplaceOneWholeFrameButRejectsAdditionalMessage() throws Exception {
        try(var link=queue()){
            assertTrue(link.send(Wire.ENTITY_FRAME,EntityFrameTest.frame(11,1,false)));
            for(int i=1;i<4096;i++)assertTrue(link.send(Wire.HEARTBEAT,new byte[0]));
            assertFalse(link.send(Wire.ENTITY_TEXTURE,new byte[0]));
            assertTrue(link.send(Wire.ENTITY_FRAME,EntityFrameTest.frame(12,2,false)));
            assertEquals(4096,messages(link).size());EntityFrameTest.assertPair(messages(link).getLast().payload(),12,2,false);accounting(link);
        }
    }
}
