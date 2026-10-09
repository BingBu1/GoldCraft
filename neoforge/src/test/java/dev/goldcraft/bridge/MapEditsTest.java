package dev.goldcraft.bridge;

import org.junit.jupiter.api.Test;
import java.util.Arrays;
import java.util.ArrayList;
import java.util.List;
import static org.junit.jupiter.api.Assertions.*;

class MapEditsTest {
    private final MapEdits.Target world=new MapEdits.Target(0,0,0);
    private final MapEdits.Box box=new MapEdits.Box(-32,-16,-8,32,16,8);
    private MapEdits.Delta add(long revision){return new MapEdits.Delta(7,revision-1,revision,MapEdits.ADD,world,box);}
    @Test void recoveryNeverCommitsAGapOrAnOlderWorld(){
        var replica=new MapEdits.Replica();replica.reset(7);
        assertEquals(MapEdits.Applied.NEED_SNAPSHOT,replica.accept(add(2)));
        replica.accept(new MapEdits.Snapshot(7,1,List.of()));
        assertEquals(MapEdits.Applied.NEED_SNAPSHOT,replica.accept(add(3)));assertFalse(replica.ready());
        assertEquals(MapEdits.Applied.NEED_SNAPSHOT,replica.accept(add(2)));
        var current=new MapEdits.Snapshot(7,3,List.of(new MapEdits.Cut(2,world,box),new MapEdits.Cut(3,world,box)));
        assertEquals(MapEdits.Applied.CHANGED,replica.accept(current));
        assertEquals(MapEdits.Applied.IGNORED,replica.accept(add(2)));
        replica.accept(new MapEdits.Delta(7,3,4,MapEdits.CLEAR,null,null));assertTrue(replica.cuts().isEmpty());
        assertEquals(MapEdits.Applied.IGNORED,replica.accept(current));
        replica.invalidate();assertEquals(MapEdits.Applied.IGNORED,replica.accept(current));assertFalse(replica.ready());
        replica.accept(new MapEdits.Snapshot(7,4,List.of()));assertTrue(replica.ready());
        replica.reset(8);assertEquals(MapEdits.Applied.IGNORED,replica.accept(current));assertFalse(replica.ready());
    }
    @Test void codecsAndUnsignedRevisionsMatchFixedWidths(){
        var target=new MapEdits.Target(42,-1,12);
        var cut=new MapEdits.Cut(Long.MIN_VALUE,target,box);
        var snapshot=new MapEdits.Snapshot(7,Long.MIN_VALUE,List.of(cut));
        var bytes=snapshot.encode();assertEquals(64,bytes.length);assertEquals(snapshot,MapEdits.snapshot(bytes));
        for(int i=0;i<bytes.length;i++){var truncated=Arrays.copyOf(bytes,i);assertThrows(IllegalArgumentException.class,()->MapEdits.snapshot(truncated));}
        var delta=new MapEdits.Delta(7,Long.MAX_VALUE,Long.MIN_VALUE,MapEdits.ADD,target,box);
        assertEquals(delta,MapEdits.delta(delta.encode()));assertEquals(64,delta.encode().length);
        var replica=new MapEdits.Replica();replica.reset(7);replica.accept(new MapEdits.Snapshot(7,Long.MAX_VALUE,List.of()));
        assertEquals(MapEdits.Applied.CHANGED,replica.accept(delta));assertEquals(snapshot,replica.snapshot());
        assertThrows(IllegalArgumentException.class,()->new MapEdits.Delta(7,-1L,0,MapEdits.CLEAR,null,null));
        assertThrows(IllegalArgumentException.class,()->new MapEdits.Snapshot(7,4,List.of(new MapEdits.Cut(3,world,box),new MapEdits.Cut(2,world,box))));
    }
    @Test void reusedEdictOrAnotherModelCannotLoseItsEdits(){
        var replica=new MapEdits.Replica();replica.reset(7);
        var first=new MapEdits.Target(72,2,3);var second=new MapEdits.Target(72,3,3);
        replica.accept(new MapEdits.Snapshot(7,3,List.of(new MapEdits.Cut(2,first,box),new MapEdits.Cut(3,second,box))));
        replica.accept(new MapEdits.Delta(7,3,4,MapEdits.REMOVE_TARGET,first,null));
        assertEquals(List.of(new MapEdits.Cut(3,second,box)),replica.cuts());
    }
    @Test void fullSnapshotAndDeltasSurviveSlowFragmentDelivery(){
        var q=new HostDeliveryQueue();var replica=new MapEdits.Replica();replica.reset(7);
        var initial=new MapEdits.Snapshot(7,1,List.of()).encode();q.editSnapshot(initial);q.advance(10);
        q.editDelta(add(2).encode(),()->{throw new AssertionError();});q.editDelta(add(3).encode(),()->{throw new AssertionError();});
        assertEquals(3,q.size());assertEquals(10,q.offset());
        replica.accept(MapEdits.snapshot(q.first().payload()));q.advance(initial.length-10);
        while(q.first()!=null){var message=q.first();assertEquals(MapEdits.Applied.CHANGED,replica.accept(MapEdits.delta(message.payload())));q.advance(message.payload().length);}
        assertEquals(3,replica.revision());assertEquals(2,replica.cuts().size());
    }
    @Test void backpressureUsesAFullReplacementAndKeepsTheActiveFragment(){
        var q=new HostDeliveryQueue();var initial=new MapEdits.Snapshot(7,1,List.of()).encode();q.editSnapshot(initial);q.advance(1);
        var allCuts=new ArrayList<MapEdits.Cut>();
        for(long rev=2;rev<=300;rev++){
            final long current=rev;allCuts.add(new MapEdits.Cut(current,world,box));
            q.editDelta(add(rev).encode(),()->new MapEdits.Snapshot(7,current,allCuts).encode());
        }
        assertTrue(q.size()<256);assertEquals(1,q.offset());assertArrayEquals(initial,q.first().payload());
        q.advance(initial.length-1);assertEquals(Wire.MAP_EDIT_SNAPSHOT,q.first().type());
        var replica=new MapEdits.Replica();replica.reset(7);replica.accept(MapEdits.snapshot(q.first().payload()));q.advance(q.first().payload().length);
        while(q.first()!=null){var message=q.first();assertEquals(MapEdits.Applied.CHANGED,replica.accept(MapEdits.delta(message.payload())));q.advance(message.payload().length);}
        assertEquals(300,replica.revision());assertEquals(allCuts,replica.cuts());
    }
    @Test void sameMapAnnouncementsRetainEditsButNewMapsClearThem(){
        var host=new HostWorldState();var identity=new Wire.Writer().i64(7).string("cs_assault").toByteArray();host.world(identity);
        host.edits().accept(new MapEdits.Snapshot(7,2,List.of(new MapEdits.Cut(2,world,box))));
        host.world(identity);assertEquals(1,host.edits().cuts().size());
        host.world(new Wire.Writer().i64(8).string("cs_assault").toByteArray());assertFalse(host.edits().ready());assertTrue(host.edits().cuts().isEmpty());
    }
}
