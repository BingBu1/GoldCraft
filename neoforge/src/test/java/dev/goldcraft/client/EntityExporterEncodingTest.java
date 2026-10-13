package dev.goldcraft.client;

import dev.goldcraft.bridge.BridgeLink;
import dev.goldcraft.bridge.EntityFrame;
import dev.goldcraft.bridge.HostWorldState;
import dev.goldcraft.bridge.ParticleSnapshot;
import dev.goldcraft.bridge.Wire;
import java.lang.reflect.Field;
import java.net.InetAddress;
import java.net.ServerSocket;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.List;
import java.util.UUID;
import java.util.concurrent.atomic.AtomicReference;
import org.junit.jupiter.api.Test;
import static org.junit.jupiter.api.Assertions.*;

class EntityExporterEncodingTest {
    private static final long EPOCH=0x1122334455667788L;
    private record Fixture(EntityExporter.Batch batch,byte[] expected) {}
    private static Fixture fixture(int texture,int flags,boolean triangles,float shift) {
        return fixture(new WorldExporter.Collector(true,triangles),texture,flags,triangles,shift);
    }
    private static Fixture fixture(WorldExporter.Collector collector,int texture,int flags,boolean triangles,float shift) {
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
    private static EntityFrame.Base prepare(long revision,int entities,int blocks,List<Fixture> fixtures,List<HostWorldState.Actor> avatars) {
        return EntityExporter.prepareBase(EPOCH,revision,entities,blocks,fixtures.stream().map(Fixture::batch).toList(),avatars);
    }
    private static byte[] encodeFrame(long revision,int entities,int blocks,List<Fixture> fixtures,List<HostWorldState.Actor> avatars,
                                      List<ParticleSnapshot.Batch> effects) {
        return EntityFrame.encode(EPOCH,3,revision,7,prepare(revision,entities,blocks,fixtures,avatars),effects);
    }
    private static byte[] expectedEnvelope(long revision,byte[] base) {
        return expectedEnvelope(revision,base,List.of());
    }
    private static byte[] expectedEnvelope(long revision,byte[] base,List<ParticleSnapshot.Batch> effects) {
        int effectSize=36+effects.stream().mapToInt(b->64+b.vertices().length).sum();
        var out=ByteBuffer.allocate(8+base.length+effectSize).order(ByteOrder.LITTLE_ENDIAN);
        out.putInt(base.length).put(base).putInt(effectSize).putLong(EPOCH).putLong(3).putLong(revision)
            .putInt(7).putInt(effects.isEmpty()?0:1).putInt(effects.size());
        for(var batch:effects){
            var m=batch.material();out.putInt(batch.texture()).putInt(m.flags()).putInt(m.srcRgb()).putInt(m.dstRgb())
                .putInt(m.srcAlpha()).putInt(m.dstAlpha()).putInt(m.equationRgb()).putInt(m.equationAlpha()).putInt(m.depthFunc())
                .putInt(m.colorMask()).putFloat(m.cutoff()).putFloat(m.red()).putFloat(m.green()).putFloat(m.blue()).putFloat(m.alpha())
                .putInt(batch.vertices().length/24).put(batch.vertices());
        }
        assertEquals(out.capacity(),out.position());return out.array();
    }
    private static ParticleSnapshot.Batch effect(int texture,int flags,float shift) {
        var vertices=ByteBuffer.allocate(96).order(ByteOrder.LITTLE_ENDIAN);
        for(int i=0;i<4;i++)vertices.putFloat(shift+i).putFloat(64-i).putFloat(-4+i).putFloat(i*.25f)
            .putFloat(1-i).putInt(0x8090a0b0+i);
        return new ParticleSnapshot.Batch(texture,new ParticleSnapshot.Material(flags,768,1,0,1,
            32778,32779,514,13,.125f,.25f,.5f,.75f,1),vertices.array());
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
    @Test void livePreparedBaseRouteMatchesFullIndependentEnvelopeWithEffectsTrianglesOverlaysAndAvatars() {
        var fixtures=List.of(fixture(0,2,false,-17),fixture(256,7,true,1024));
        var avatars=List.of(avatar(31,0x12345678,90,"00112233-4455-6677-8899-aabbccddeeff"),
            avatar(2,987,45,"fedcba98-7654-3210-0123-456789abcdef"));
        var effects=List.of(effect(-1,6|128|256,-100),effect(64,1|8|16|64,400));
        byte[] oracle=expectedEnvelope(21,expected(21,4,2,fixtures,avatars),effects);
        var prepared=prepare(21,4,2,fixtures,avatars);
        assertEquals(32+2*(12+112)+2*28,prepared.size());
        byte[] frame=EntityFrame.encode(EPOCH,3,21,7,prepared,effects);
        assertArrayEquals(oracle,frame);
        for(var batch:effects)Arrays.fill(batch.vertices(),(byte)0);
        for(var fixture:fixtures)for(int i=0;i<4;i++)fixture.batch.vertices().vertex(900+i,0,0);
        assertArrayEquals(oracle,frame);
        assertArrayEquals(expectedEnvelope(22,expected(22,0,0,List.of(),List.of())),
            encodeFrame(22,0,0,List.of(),List.of(),List.of()));
        assertArrayEquals(expectedEnvelope(23,expected(23,2,0,List.of(),avatars)),
            encodeFrame(23,2,0,List.of(),avatars,List.of()));
    }
    @Test void preparedBaseFreezesListsButRechecksCollectorSizeWhenActuallyWritten() {
        var fixture=fixture(2,2,false,5);var fixtures=List.of(fixture);
        var actor=avatar(1,2,3,"00112233-4455-6677-8899-aabbccddeeff");
        var batches=new ArrayList<>(List.of(fixture.batch));var avatars=new ArrayList<>(List.of(actor));
        var prepared=EntityExporter.prepareBase(EPOCH,31,1,0,batches,avatars);
        byte[] oracle=expectedEnvelope(31,expected(31,1,0,fixtures,List.of(actor)));
        batches.clear();avatars.clear();
        assertArrayEquals(oracle,EntityFrame.encode(EPOCH,3,31,7,prepared,List.of()));
        for(int i=0;i<4;i++)fixture.batch.vertices().vertex(100+i,0,0);
        assertThrows(IllegalArgumentException.class,()->EntityFrame.encode(EPOCH,3,31,7,prepared,List.of()));
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
        assertEquals(32+128*(12+112),EntityExporter.prepareBase(EPOCH,1,1,0,Collections.nCopies(128,small),List.of()).size());
        assertThrows(IllegalArgumentException.class,()->EntityExporter.encodeBase(EPOCH,1,1,0,Collections.nCopies(129,small),List.of()));
        assertThrows(IllegalArgumentException.class,()->EntityExporter.prepareBase(EPOCH,1,1,0,Collections.nCopies(129,small),List.of()));
        var large=constantBatch(262144);var remainder=constantBatch(37448);
        // Entity quads are 112 bytes: the greatest permitted multiple below 8 MiB.
        assertEquals(8*1024*1024-32,large.bytes()+remainder.bytes());
        assertEquals(8*1024*1024-32+32+24,
            EntityExporter.encodeBase(EPOCH,1,1,0,List.of(large,remainder),List.of()).length);
        var prepared=EntityExporter.prepareBase(EPOCH,1,1,0,List.of(large,remainder),List.of());
        assertEquals(8*1024*1024+24,prepared.size());
        assertEquals(prepared.size()+44,EntityFrame.encode(EPOCH,3,1,7,prepared,List.of()).length);
        assertThrows(IllegalArgumentException.class,()->EntityExporter.encodeBase(EPOCH,2,1,0,List.of(large,remainder,small),List.of()));
        assertThrows(IllegalArgumentException.class,()->EntityExporter.prepareBase(EPOCH,2,1,0,List.of(large,remainder,small),List.of()));
        for(int invalid:new int[]{-1,Integer.MAX_VALUE})assertThrows(IllegalArgumentException.class,
            ()->EntityExporter.prepareBase(EPOCH,1,1,0,List.of(new EntityExporter.Batch(0,2,small.vertices(),invalid)),List.of()));
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
    private static byte[] queuedFrameWithTextures(BridgeLink link)throws Exception {
        var messages=(ArrayDeque<?>)field("outgoing").get(link);int bytes=0,frames=0;byte[] result=null;
        for(var message:messages){
            var payload=message.getClass().getDeclaredMethod("payload");payload.setAccessible(true);
            var type=message.getClass().getDeclaredMethod("type");type.setAccessible(true);
            byte[] data=(byte[])payload.invoke(message);bytes+=data.length+Wire.HEADER_BYTES;
            if((int)type.invoke(message)==Wire.ENTITY_FRAME){frames++;result=data;}
        }
        assertEquals(1,frames);assertEquals(bytes,field("outboundBytes").getInt(link));return result;
    }
    @Test void liveProducedFrameKeepsOwnershipAcrossFailedEncodingAndFullQueueReplacement()throws Exception {
        try(var link=queue()){
            var fixture=fixture(1,2,true,9);var fixtures=List.of(fixture);
            var effects=List.of(effect(3,6|128|256,11));
            byte[] oracle=expectedEnvelope(51,expected(51,1,0,fixtures,List.of()),effects);
            var prepared=prepare(51,1,0,fixtures,List.of());var retained=new AtomicReference<Wire.Writer>();
            var producer=new EntityFrame.Base(prepared.size(),out->{retained.set(out);prepared.write().accept(out);});
            byte[] frame=EntityFrame.encode(EPOCH,3,51,7,producer,effects);
            Arrays.fill(effects.getFirst().vertices(),(byte)0);
            for(int i=0;i<3;i++)fixture.batch.vertices().vertex(200+i,0,0);
            assertArrayEquals(oracle,frame);assertTrue(link.send(Wire.ENTITY_FRAME,frame));Arrays.fill(frame,(byte)0);
            assertThrows(IllegalStateException.class,()->retained.get().bytes(new byte[]{1}));
            assertArrayEquals(oracle,queued(link));
            assertThrows(IllegalArgumentException.class,()->link.send(Wire.ENTITY_FRAME,
                EntityFrame.encode(EPOCH,3,51,7,prepared,List.of())));
            assertArrayEquals(oracle,queued(link));
            var partial=new EntityFrame.Base(32,out->out.i64(EPOCH));
            assertThrows(IllegalArgumentException.class,()->link.send(Wire.ENTITY_FRAME,
                EntityFrame.encode(EPOCH,3,52,7,partial,List.of())));
            assertArrayEquals(oracle,queued(link));
            var wrongIdentity=new EntityFrame.Base(32,out->out.i64(EPOCH+1).i64(52).i32(0).i32(0).i32(0).i32(0));
            assertThrows(IllegalArgumentException.class,()->link.send(Wire.ENTITY_FRAME,
                EntityFrame.encode(EPOCH,3,52,7,wrongIdentity,List.of())));
            assertArrayEquals(oracle,queued(link));

            assertTrue(link.send(Wire.ENTITY_TEXTURE,new byte[Wire.MAX_PAYLOAD]));
            int queueLimit=Wire.MAX_QUEUED-field("URGENT_RESERVED_BYTES").getInt(null);
            assertTrue(link.send(Wire.ENTITY_TEXTURE,new byte[queueLimit-field("outboundBytes").getInt(link)-Wire.HEADER_BYTES]));
            assertEquals(queueLimit,field("outboundBytes").getInt(link));
            var nextFixtures=List.of(fixture(1,2,false,300),fixture(2,7,true,400));
            byte[] larger=encodeFrame(52,2,0,nextFixtures,List.of(),List.of(effect(3,6|128|256,500),effect(4,6,600)));
            assertTrue(larger.length>oracle.length);assertFalse(link.send(Wire.ENTITY_FRAME,larger));
            assertArrayEquals(oracle,queuedFrameWithTextures(link));assertEquals(queueLimit,field("outboundBytes").getInt(link));
            assertThrows(IllegalStateException.class,()->retained.get().u8(0));
            assertArrayEquals(oracle,queuedFrameWithTextures(link));

            byte[] clear=encodeFrame(53,0,0,List.of(),List.of(),List.of());
            assertTrue(link.send(Wire.ENTITY_FRAME,clear));Arrays.fill(clear,(byte)0);
            byte[] clearOracle=expectedEnvelope(53,expected(53,0,0,List.of(),List.of()));
            assertArrayEquals(clearOracle,queuedFrameWithTextures(link));
            assertEquals(queueLimit-oracle.length+clearOracle.length,field("outboundBytes").getInt(link));
        }
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
    @Test void recycledArrayAndFailedNextFrameCannotChangePreviouslyQueuedEnvelope()throws Exception {
        var pool=new EntityCollectorBuffers<String>();
        try(var link=queue()){
            Fixture first;byte[] firstOracle;
            try(var buffers=pool.beginFrame()){
                first=fixture(buffers.collector("skin",false),1,2,false,-10);var fixtures=List.of(first);
                firstOracle=expectedEnvelope(1,expected(1,1,0,fixtures,List.of()));
                assertTrue(link.send(Wire.ENTITY_FRAME,encodeFrame(1,1,0,fixtures,List.of(),List.of())));buffers.complete();
            }
            byte[] nextPacket,nextOracle;
            try(var buffers=pool.beginFrame()){
                Fixture next=fixture(buffers.collector("skin",false),1,2,false,900);var fixtures=List.of(next);
                assertEquals(1,pool.reuses());
                assertThrows(IllegalStateException.class,()->first.batch.vertices().vertex(0,0,0));
                assertArrayEquals(firstOracle,queued(link));
                nextPacket=encodeFrame(2,1,0,fixtures,List.of(),List.of());
                nextOracle=expectedEnvelope(2,expected(2,1,0,fixtures,List.of()));buffers.complete();
            }
            assertTrue(link.send(Wire.ENTITY_FRAME,nextPacket));Arrays.fill(nextPacket,(byte)0);
            assertArrayEquals(nextOracle,queued(link));
            try(var buffers=pool.beginFrame()){
                var broken=buffers.collector("skin",false);
                for(int i=0;i<3;i++)broken.vertex(100+i,64,-2);
                assertThrows(IllegalArgumentException.class,broken::finishSize);
            }
            assertEquals(0,pool.retainedBytes());assertArrayEquals(nextOracle,queued(link));
            try(var buffers=pool.beginFrame()){
                Fixture recovered=fixture(buffers.collector("skin",true),1,2,true,42);var fixtures=List.of(recovered);
                byte[] payload=encodeFrame(3,1,0,fixtures,List.of(),List.of());
                assertTrue(link.send(Wire.ENTITY_FRAME,payload));
                assertArrayEquals(expectedEnvelope(3,expected(3,1,0,fixtures,List.of())),queued(link));buffers.complete();
            }
            byte[] accepted=queued(link).clone();pool.clear();assertArrayEquals(accepted,queued(link));
        }
    }
    @SuppressWarnings("unchecked")
    @Test void exporterResetClosesActiveLoanAndDropsPriorContextRetention()throws Exception {
        var active=EntityExporter.class.getDeclaredField("active");active.setAccessible(true);Object previous=active.get(null);
        try(var link=queue()){
            var exporter=new EntityExporter(link);
            var field=EntityExporter.class.getDeclaredField("collectorBuffers");field.setAccessible(true);
            var pool=(EntityCollectorBuffers<EntityExporter.Material>)field.get(exporter);
            var material=new EntityExporter.Material(net.minecraft.util.Identifier.ofVanilla("textures/test.png"),false,true,false,false);
            try(var frame=pool.beginFrame()){
                fixture(frame.collector(material,false),0,2,false,0);frame.complete();
            }
            assertTrue(pool.retainedBytes()>0);
            var interrupted=pool.beginFrame();var stale=interrupted.collector(material,false);
            fixture(stale,0,2,false,0);exporter.reset();
            assertEquals(0,pool.retainedBytes());assertEquals(0,pool.retainedEntries());
            assertThrows(IllegalStateException.class,interrupted::complete);
            assertThrows(IllegalStateException.class,()->stale.vertex(0,0,0));interrupted.close();
            try(var frame=pool.beginFrame()){
                var fresh=fixture(frame.collector(material,false),0,2,false,99);var fixtures=List.of(fresh);
                assertArrayEquals(expected(1,1,0,fixtures,List.of()),encode(1,1,0,fixtures,List.of()));frame.complete();
            }
            exporter.reset();assertEquals(0,pool.retainedEntries());
        }finally{active.set(null,previous);}
    }
}
