package dev.goldcraft.client;

import dev.goldcraft.bridge.BridgeLink;
import dev.goldcraft.bridge.EntityFrame;
import dev.goldcraft.bridge.HostWorldState;
import dev.goldcraft.bridge.Wire;
import java.lang.reflect.Field;
import java.net.InetAddress;
import java.net.ServerSocket;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.ArrayDeque;
import java.util.Arrays;
import java.util.Collections;
import java.util.List;
import java.util.UUID;
import org.junit.jupiter.api.Test;
import static org.junit.jupiter.api.Assertions.*;

class EntityExporterEncodingTest {
    private static final long EPOCH=0x1122334455667788L;
    private record Fixture(EntityExporter.Batch batch,byte[] expected) {}
    private static Fixture fixture(int texture,int flags,boolean triangles,float shift) {
        var collector=new WorldExporter.Collector(true,triangles);
        int source=triangles?3:4;
        var expected=ByteBuffer.allocate(4*28).order(ByteOrder.LITTLE_ENDIAN);
        for(int i=0;i<source;i++){
            float x=shift+i+.25f,y=64+i,z=-2-i,u=i==0?-0.0f:i*.5f,v=i==0?Float.MIN_VALUE:1-i;
            collector.vertex(x,y,z).texture(u,v).color(256+i,-1-i,128+i,240-i);
            int overlay=0;if(i%2==0){collector.overlay(15,0);overlay=0x4d0000ff;}
            int copies=triangles&&i==2?2:1;
            for(int j=0;j<copies;j++)expected.putFloat(x).putFloat(y).putFloat(z).putFloat(u).putFloat(v)
                .put((byte)(256+i)).put((byte)(-1-i)).put((byte)(128+i)).put((byte)(240-i)).putInt(overlay);
        }
        assertEquals(expected.capacity(),expected.position());
        return new Fixture(new EntityExporter.Batch(texture,flags,collector,collector.finishSize()),expected.array());
    }
    private static HostWorldState.Actor avatar(int slot,int serial,int userid,String id) {
        var zero=new HostWorldState.Vector(0,0,0);
        return new HostWorldState.Actor(slot,serial,userid,1,113,7,100,0,zero,zero,0,0,zero,zero,UUID.fromString(id),7,0);
    }
    /** Independent full layout oracle: no Wire.Writer or Collector output is used. */
    private static byte[] expected(long revision,int entities,int blocks,List<Fixture> fixtures,List<HostWorldState.Actor> avatars) {
        int size=32+fixtures.stream().mapToInt(f->12+f.expected.length).sum()+avatars.size()*28;
        var out=ByteBuffer.allocate(size).order(ByteOrder.LITTLE_ENDIAN);
        out.putLong(EPOCH).putLong(revision).putInt(entities).putInt(blocks).putInt(fixtures.size());
        for(var fixture:fixtures)out.putInt(fixture.batch.texture()).putInt(fixture.batch.flags())
            .putInt(fixture.expected.length/28).put(fixture.expected);
        out.putInt(avatars.size());
        for(var actor:avatars){
            out.putInt(actor.slot()).putInt(actor.serial()).putInt(actor.userid());
            // UUID wire bytes are network order, unlike the surrounding fields.
            out.order(ByteOrder.BIG_ENDIAN).putLong(actor.minecraftPlayer().getMostSignificantBits())
                .putLong(actor.minecraftPlayer().getLeastSignificantBits()).order(ByteOrder.LITTLE_ENDIAN);
        }
        assertEquals(size,out.position());return out.array();
    }
    private static byte[] encode(long revision,int entities,int blocks,List<Fixture> fixtures,List<HostWorldState.Actor> avatars) {
        return EntityExporter.encodeBase(EPOCH,revision,entities,blocks,fixtures.stream().map(Fixture::batch).toList(),avatars);
    }
    private static byte[] expectedEnvelope(long revision,byte[] base) {
        return ByteBuffer.allocate(base.length+44).order(ByteOrder.LITTLE_ENDIAN).putInt(base.length).put(base).putInt(36)
            .putLong(EPOCH).putLong(3).putLong(revision).putInt(7).putInt(0).putInt(0).array();
    }
    @Test void actualBaseAndEnvelopeMatchIndependentBytesForMaterialsAndAvatars() {
        var fixtures=List.of(fixture(0,2,false,-17),fixture(256,7,true,1024));
        var avatars=List.of(avatar(31,0x12345678,90,"00112233-4455-6677-8899-aabbccddeeff"),
            avatar(2,987,45,"fedcba98-7654-3210-0123-456789abcdef"));
        byte[] oracle=expected(9,4,2,fixtures,avatars),base=encode(9,4,2,fixtures,avatars);
        assertArrayEquals(oracle,base);
        assertArrayEquals(expectedEnvelope(9,oracle),EntityFrame.encode(EPOCH,3,9,7,base,List.of()));
        assertArrayEquals(expected(10,0,0,List.of(),List.of()),encode(10,0,0,List.of(),List.of()));
        assertArrayEquals(expected(11,2,0,List.of(),avatars),encode(11,2,0,List.of(),avatars));
    }
    @Test void serializationOwnsBytesAndRejectsChangedPreparedCollector() {
        Fixture fixture=fixture(4,2,false,0);var fixtures=List.of(fixture);
        byte[] first=encode(1,1,0,fixtures,List.of()),oracle=first.clone();
        byte[] snapshot=fixture.batch.vertices().finish();Arrays.fill(snapshot,(byte)0);
        assertArrayEquals(oracle,encode(1,1,0,fixtures,List.of()));
        first[0]=0;assertArrayEquals(oracle,encode(1,1,0,fixtures,List.of()));
        for(int i=0;i<4;i++)fixture.batch.vertices().vertex(i,0,0);
        assertThrows(IllegalArgumentException.class,()->encode(2,1,0,fixtures,List.of()));
        assertArrayEquals(expected(1,1,0,fixtures,List.of()),oracle);
    }
    private static EntityExporter.Batch constantBatch(int count) {
        var collector=new WorldExporter.Collector(true,false);
        for(int i=0;i<count;i++)collector.vertex(1,64,-2);
        return new EntityExporter.Batch(0,2,collector,collector.finishSize());
    }
    @Test void exactBatchAndAggregateBudgetsRemainEnforced() {
        var small=constantBatch(4);
        assertEquals(32+128*(12+112),EntityExporter.encodeBase(EPOCH,1,1,0,Collections.nCopies(128,small),List.of()).length);
        assertThrows(IllegalArgumentException.class,()->EntityExporter.encodeBase(EPOCH,1,1,0,Collections.nCopies(129,small),List.of()));
        var large=constantBatch(262144);var remainder=constantBatch(37448);
        // Entity quads are 112 bytes: the greatest permitted multiple below 8 MiB.
        assertEquals(8*1024*1024-32,large.bytes()+remainder.bytes());
        assertEquals(8*1024*1024-32+32+24,
            EntityExporter.encodeBase(EPOCH,1,1,0,List.of(large,remainder),List.of()).length);
        assertThrows(IllegalArgumentException.class,()->EntityExporter.encodeBase(EPOCH,2,1,0,List.of(large,remainder,small),List.of()));
    }
    private static Field field(String name)throws Exception {
        var field=BridgeLink.class.getDeclaredField(name);field.setAccessible(true);return field;
    }
    private static BridgeLink queue()throws Exception {
        try(var peer=new ServerSocket(0,1,InetAddress.getLoopbackAddress())){
            var link=new BridgeLink(new BridgeLink.Config(peer.getLocalPort(),new byte[16],new byte[16],Wire.FABRIC_CLIENT,Wire.HOST_CLIENT));
            link.close();assertFalse(((Thread)field("worker").get(link)).isAlive());field("connected").setBoolean(link,true);return link;
        }
    }
    private static byte[] queued(BridgeLink link)throws Exception {
        var messages=(ArrayDeque<?>)field("outgoing").get(link);assertEquals(1,messages.size());
        Object message=messages.getFirst();var payload=message.getClass().getDeclaredMethod("payload");payload.setAccessible(true);
        byte[] result=(byte[])payload.invoke(message);assertEquals(result.length+Wire.HEADER_BYTES,field("outboundBytes").getInt(link));
        return result;
    }
    @Test void productionQueueKeepsOwnedCompleteFrameAcrossSourceMutationFailureAndRecovery()throws Exception {
        try(var link=queue()){
            Fixture first=fixture(1,2,false,0);var fixtures=List.of(first);
            byte[] base=encode(1,1,0,fixtures,List.of());
            byte[] frame=EntityFrame.encode(EPOCH,3,1,7,base,List.of()),oracle=expectedEnvelope(1,expected(1,1,0,fixtures,List.of()));
            assertTrue(link.send(Wire.ENTITY_FRAME,frame));
            Arrays.fill(base,(byte)0);Arrays.fill(frame,(byte)0);Arrays.fill(first.batch.vertices().finish(),(byte)0);
            for(int i=0;i<4;i++)first.batch.vertices().vertex(i+100,0,0);
            assertArrayEquals(oracle,queued(link));
            // A changed prepared batch and a new malformed frame never reach send.
            assertThrows(IllegalArgumentException.class,()->link.send(Wire.ENTITY_FRAME,
                EntityFrame.encode(EPOCH,3,2,7,encode(2,1,0,fixtures,List.of()),List.of())));
            for(float x:new float[]{1,Float.NaN}){
                var invalid=new WorldExporter.Collector(true,false);invalid.vertex(x,0,0);
                var malformed=new EntityExporter.Batch(1,2,invalid,28);
                assertThrows(IllegalArgumentException.class,()->link.send(Wire.ENTITY_FRAME,
                    EntityFrame.encode(EPOCH,3,2,7,EntityExporter.encodeBase(EPOCH,2,1,0,List.of(malformed),List.of()),List.of())));
                assertArrayEquals(oracle,queued(link));
            }
            Fixture next=fixture(1,2,true,50);var nextFixtures=List.of(next);
            byte[] recovered=EntityFrame.encode(EPOCH,3,3,7,encode(3,1,0,nextFixtures,List.of()),List.of());
            assertTrue(link.send(Wire.ENTITY_FRAME,recovered));Arrays.fill(recovered,(byte)0);
            assertArrayEquals(expectedEnvelope(3,expected(3,1,0,nextFixtures,List.of())),queued(link));
            field("connected").setBoolean(link,false);
            byte[] accepted=queued(link).clone();assertFalse(link.send(Wire.ENTITY_FRAME,new byte[]{1}));assertArrayEquals(accepted,queued(link));
        }
    }
}
