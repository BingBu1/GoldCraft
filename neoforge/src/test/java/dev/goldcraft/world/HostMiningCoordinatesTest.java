package dev.goldcraft.world;

import dev.goldcraft.bridge.HostWorldState;
import dev.goldcraft.bridge.MapEdits;
import dev.goldcraft.bridge.MapMining;
import dev.goldcraft.bridge.Wire;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.List;
import net.minecraft.util.math.BlockPos;
import net.minecraft.util.math.Vec3d;
import org.junit.jupiter.api.Test;
import static org.junit.jupiter.api.Assertions.*;

class HostMiningCoordinatesTest {
    private static BspMap.Vec v(float x, float y, float z) { return new BspMap.Vec(x,y,z); }
    private static HostWorldState.Vector hv(float x, float y, float z) { return new HostWorldState.Vector(x,y,z); }
    private static HostWorldState.Brush brush(float x, float y, float z, float pitch, float yaw, float roll) {
        return new HostWorldState.Brush(7,55,1,7,1,hv(x,y,z),hv(pitch,yaw,roll),hv(-16384,-16384,-16384),hv(16384,16384,16384),hv(0,0,0));
    }
    private static BspMap.Vec world(BspMap.Vec local, HostWorldState.Brush brush) {
        // Independent inverse of GoldSrc's AngleVectors frame, not the production local() helper.
        double p=Math.toRadians(brush.angles().x()),y=Math.toRadians(brush.angles().y()),r=Math.toRadians(brush.angles().z());
        double sp=Math.sin(p),cp=Math.cos(p),sy=Math.sin(y),cy=Math.cos(y),sr=Math.sin(r),cr=Math.cos(r);
        return v((float)(brush.origin().x()+cp*cy*local.x()+(sr*sp*cy-cr*sy)*local.y()+(cr*sp*cy+sr*sy)*local.z()),
                 (float)(brush.origin().y()+cp*sy*local.x()+(sr*sp*sy+cr*cy)*local.y()+(cr*sp*sy-sr*cy)*local.z()),
                 (float)(brush.origin().z()-sp*local.x()+sr*cp*local.y()+cr*cp*local.z()));
    }
    private static Vec3d minecraft(BspMap.Vec point) {
        return new Vec3d(point.x()/32.0,point.z()/32.0+Wire.Y_OFFSET,-point.y()/32.0);
    }
    private static byte[] map() {
        // World floor at -8192; model *1 has a local x=0 wall. Actual BSP parser,
        // hull traversal and production HostRaycast supply all hit metadata.
        var b=ByteBuffer.allocate(412).order(ByteOrder.LITTLE_ENDIAN);b.putInt(30);
        int[] offsets=new int[15],sizes=new int[15];
        offsets[1]=124;sizes[1]=40;offsets[5]=164;sizes[5]=48;
        offsets[9]=212;sizes[9]=16;offsets[10]=228;sizes[10]=56;offsets[14]=284;sizes[14]=128;
        for(int i=0;i<15;i++)b.putInt(offsets[i]).putInt(sizes[i]);
        b.putFloat(0).putFloat(0).putFloat(1).putFloat(-8192).putInt(2);
        b.putFloat(1).putFloat(0).putFloat(0).putFloat(0).putInt(0);
        for(int i=0;i<2;i++){b.position(164+i*24);b.putInt(i).putShort((short)-2).putShort((short)-1);}
        b.position(212);for(int i=0;i<2;i++)b.putInt(i).putShort((short)-1).putShort((short)-2);
        b.putInt(228,-2);b.putInt(256,-1);
        for(int i=0;i<2;i++) {
            b.position(284+i*64);
            for(int axis=0;axis<3;axis++)b.putFloat(-16384);
            for(int axis=0;axis<3;axis++)b.putFloat(16384);
            for(int axis=0;axis<3;axis++)b.putFloat(0);
            for(int hull=0;hull<4;hull++)b.putInt(i);
        }
        return b.array();
    }
    private static HostWorldState host() {
        var host=new HostWorldState();host.world(new Wire.Writer().i64(9).string("coordinate_test").toByteArray());
        byte[] bytes=map();host.bsp(new Wire.Writer().i64(9).i32((int)BspMap.checksum(bytes)).i32(bytes.length).bytes(bytes).toByteArray());
        host.edits().accept(new MapEdits.Snapshot(9,1,List.of()));return host;
    }
    private static void publish(HostWorldState host,HostWorldState.Brush brush,long tick) {
        var w=new Wire.Writer().i64(9).i64(tick).i32(1).i32(brush.slot()).i32(brush.serial()).i32(brush.model()).i32(brush.moveType()).i32(brush.flags());
        for(var vector:List.of(brush.origin(),brush.angles(),brush.min(),brush.max(),brush.velocity()))w.f32(vector.x()).f32(vector.y()).f32(vector.z());
        host.brushes(w.toByteArray());
    }
    private static HostMiningCoordinates.Target key(HostRaycast.Hit hit,long revision) {
        return HostMiningCoordinates.target(hit.slot,hit.serial,hit.model,hit.getBlockPos(),hit.miningRayCell,revision);
    }
    private static MapMining.Surface sample(int slot,int serial,MapMining.Cell cell,long revision) {
        return new MapMining.Surface(new MapMining.Result(9,20,2,1,3,4,slot,serial,0,0,0),2,MapMining.STONE,
            640,-960,128,1,0,0,revision,cell);
    }
    @Test void movingAndRotatingBrushKeepsLocalKeyButUpdatesVanillaWorldPosition() {
        var host=host();HostMiningCoordinates.Target first=null;BlockPos prior=null;long tick=0;
        for(var brush:List.of(brush(0,0,0,0,0,0),brush(640,-960,128,0,90,0),brush(-480,736,256,17,37,11))) {
            publish(host,brush,++tick);
            var hit=HostRaycast.trace(host,minecraft(world(v(48,7,13),brush)),minecraft(world(v(-1,7,13),brush)));
            assertNotNull(hit);assertEquals(7,hit.slot);assertEquals(new MapMining.Cell(-1,0,0),hit.miningRayCell);
            var key=key(hit,1);if(first==null)first=key;else assertEquals(first,key);
            var nativeSample=HostMiningCoordinates.sample(key,sample(7,55,new MapMining.Cell(-1,0,0),1));
            var world=HostMiningCoordinates.worldCell(key,nativeSample,hit.miningWorldCell);
            assertEquals(hit.miningWorldCell,world);
            if(prior!=null)assertNotEquals(prior,world);prior=world;
        }
    }
    @Test void newLocalCellInstanceSerialModelOrEditRevisionCannotReuseProgressIdentity() {
        var host=host();var brush=brush(640,-960,128,17,37,11);publish(host,brush,1);
        var first=HostRaycast.trace(host,minecraft(world(v(48,7,13),brush)),minecraft(world(v(-1,7,13),brush)));
        var next=HostRaycast.trace(host,minecraft(world(v(48,39,13),brush)),minecraft(world(v(-1,39,13),brush)));
        assertNotEquals(key(first,1),key(next,1));
        var original=key(first,1);
        assertNotEquals(original,HostMiningCoordinates.target(8,55,1,first.getBlockPos(),first.miningRayCell,1));
        assertNotEquals(original,HostMiningCoordinates.target(7,56,1,first.getBlockPos(),first.miningRayCell,1));
        assertNotEquals(original,HostMiningCoordinates.target(7,55,2,first.getBlockPos(),first.miningRayCell,1));
        assertNotEquals(original,key(first,2));
    }
    @Test void authoritativeCellIsSeparateFromProvisionalRayAndMustResetOnChange() {
        var target=HostMiningCoordinates.target(7,55,1,new BlockPos(20,68,30),new MapMining.Cell(-1,0,0),5);
        var first=HostMiningCoordinates.sample(target,sample(7,55,new MapMining.Cell(-1,0,0),5));
        var corrected=HostMiningCoordinates.sample(target,sample(7,55,new MapMining.Cell(-2,0,0),5));
        assertNotEquals(first,corrected); // Same ray key cannot retain progress across this native correction.
        assertEquals(corrected,HostMiningCoordinates.sample(target,sample(7,55,new MapMining.Cell(-2,0,0),5)));
        assertNotEquals(corrected,HostMiningCoordinates.sample(target,sample(7,55,new MapMining.Cell(-2,0,0),6)));
        var current=new BlockPos(35,72,49);
        assertEquals(current,HostMiningCoordinates.worldCell(target,corrected,current));
    }
    @Test void staticWorldRetainsOriginalRayKeyAndCanonicalWorldCellConversion() {
        var hit=new BlockPos(30,77,-90);
        var target=HostMiningCoordinates.target(0,0,0,hit,null,5);
        assertEquals(hit,target.worldCell());assertNull(target.rayCell());
        assertNotEquals(target,HostMiningCoordinates.target(0,0,0,hit.add(1,0,0),null,5));
        var sampled=HostMiningCoordinates.sample(target,sample(0,0,new MapMining.Cell(-1,90,12),5));
        assertEquals(new BlockPos(-1,76,-91),HostMiningCoordinates.worldCell(target,sampled,hit));
        assertEquals(hit,HostMiningCoordinates.worldCell(target,null,hit));
    }
    @Test void damageableMovingTargetsKeepWorldCoordinatesWithoutAuthoritativeCell() {
        var target=HostMiningCoordinates.target(7,55,1,new BlockPos(1,2,3),new MapMining.Cell(-1,0,0),5);
        var sampled=HostMiningCoordinates.sample(target,sample(7,55,new MapMining.Cell(0,0,0),0));
        assertNull(sampled.cell());var moved=new BlockPos(60,70,-10);
        assertEquals(moved,HostMiningCoordinates.worldCell(target,sampled,moved));
        assertNull(HostMiningCoordinates.target(7,55,1,moved,null,5));
    }
    @Test void exactPlaneSelectsSolidSideAtPositiveNegativeAndZeroBoundaries() {
        for(float plane:new float[]{0,32,-32,-32.001f}) {
            var from=v(plane+48,7,13);var to=v(plane-1,7,13);
            var trace=new BspMap.Trace(.5f,v(plane+.03125f,7,13),v(1,0,0),plane,false,false,true,false);
            int expected=plane==0?-1:plane==32?0:-2;
            assertEquals(new MapMining.Cell(expected,0,0),HostMiningCoordinates.rayCell(from,to,trace));
        }
        var reverse=new BspMap.Trace(.5f,v(-.03125f,7,13),v(-1,0,0),0,false,false,true,false);
        assertEquals(new MapMining.Cell(0,0,0),HostMiningCoordinates.rayCell(v(-48,7,13),v(1,7,13),reverse));
    }
    @Test void editedCavityUsesItsOwnLocalPlaneAndLeavesSameModelInstanceUntouched() {
        var host=host();var brush=brush(640,-960,128,0,90,0);publish(host,brush,1);
        host.edits().accept(new MapEdits.Delta(9,1,2,MapEdits.ADD,new MapEdits.Target(7,55,1),new MapEdits.Box(-32,-16,-16,0,16,16)));
        var hit=HostRaycast.trace(host,minecraft(world(v(48,7,13),brush)),minecraft(world(v(-40,7,13),brush)));
        assertEquals(new MapMining.Cell(-2,0,0),hit.miningRayCell);
        var other=new HostWorldState.Brush(8,66,1,7,1,brush.origin(),brush.angles(),brush.min(),brush.max(),brush.velocity());
        publish(host,other,2);
        var intact=HostRaycast.trace(host,minecraft(world(v(48,7,13),other)),minecraft(world(v(-40,7,13),other)));
        assertEquals(new MapMining.Cell(-1,0,0),intact.miningRayCell);
    }
    @Test void rotatedBoundaryPausesProgressUntilCurrentNativeSampleConfirmsTheCell() {
        var host=host();HostRaycast.Hit previous=null;long tick=0;var state=new HostMining.Intent();
        HostMiningCoordinates.Sample confirmed=null;
        for(float yaw:new float[]{0,37}) {
            var brush=brush(-480,736,256,17,yaw,11);publish(host,brush,++tick);
            var hit=HostRaycast.trace(host,minecraft(world(v(48,0,13),brush)),minecraft(world(v(-1,0,13),brush)));
            assertNotNull(hit);
            var target=key(hit,1);long generation=state.generation,probe=state.probe;
            state.observe(target,hit.miningWorldCell);
            if(previous==null) {
                confirmed=HostMiningCoordinates.sample(target,sample(7,55,new MapMining.Cell(-1,0,0),1));
                state.confirm(confirmed);assertFalse(state.advance(.25f));
            } else {
                // Real float round trips straddle y=0. Provisional disagreement
                // requires a fresh probe; it must neither erase nor advance work.
                assertEquals(-1,previous.miningRayCell.y());assertEquals(0,hit.miningRayCell.y());
                assertNotEquals(key(previous,1),target);
                assertEquals(.25f,state.progress);assertFalse(state.advance(1));
                assertEquals(.25f,state.progress);assertFalse(state.accepts(generation,probe,key(previous,1)));
                assertTrue(state.accepts(state.generation,state.probe,target));
                state.confirm(confirmed);assertFalse(state.advance(.25f));assertEquals(.5f,state.progress);
                state.confirm(HostMiningCoordinates.sample(target,sample(7,55,new MapMining.Cell(-1,-1,0),1)));
                assertEquals(0,state.progress);
            }
            previous=hit;
        }
    }
    @Test void translatedPositiveFaceUsesSolidWorldCellForMining() {
        var host=host();var brush=brush(640,0,0,0,0,0);publish(host,brush,1);
        var hit=HostRaycast.trace(host,minecraft(world(v(48,7,13),brush)),minecraft(world(v(-1,7,13),brush)));
        var key=key(hit,1);var nativeSample=HostMiningCoordinates.sample(key,sample(7,55,new MapMining.Cell(-1,0,0),1));
        assertEquals(new BlockPos(20,64,-1),hit.getBlockPos());
        assertEquals(new BlockPos(19,64,-1),hit.miningWorldCell);
        assertEquals(new BlockPos(19,64,-1),HostMiningCoordinates.worldCell(key,nativeSample,hit.miningWorldCell));
    }

    @Test void changedProbeReturningToOldCellCannotAcceptAnInFlightReply() {
        var state=new HostMining.Intent();var position=new BlockPos(19,64,-1);
        var first=HostMiningCoordinates.target(7,55,1,position,new MapMining.Cell(-1,0,0),1);
        var next=HostMiningCoordinates.target(7,55,1,position,new MapMining.Cell(-1,1,0),1);
        state.observe(first,position);state.confirm(HostMiningCoordinates.sample(first,sample(7,55,first.rayCell(),1)));
        assertFalse(state.advance(.4f));long generation=state.generation,probe=state.probe;
        state.observe(next,position);assertFalse(state.advance(1));
        state.observe(first,position);assertFalse(state.accepts(generation,probe,first));
        assertFalse(state.advance(1));assertEquals(.4f,state.progress);
        state.confirm(HostMiningCoordinates.sample(first,sample(7,55,first.rayCell(),1)));
        assertFalse(state.advance(.1f));assertEquals(.5f,state.progress);
    }

    @Test void replacementEditRevisionAndStaticWorldCellImmediatelyEraseProgress() {
        var position=new BlockPos(19,64,-1);var original=new MapMining.Cell(-1,0,0);
        for(var replacement:List.of(
            HostMiningCoordinates.target(8,55,1,position,original,1),
            HostMiningCoordinates.target(7,56,1,position,original,1),
            HostMiningCoordinates.target(7,55,2,position,original,1),
            HostMiningCoordinates.target(7,55,1,position,original,2))) {
            var state=new HostMining.Intent();var first=HostMiningCoordinates.target(7,55,1,position,original,1);
            state.observe(first,position);state.confirm(HostMiningCoordinates.sample(first,sample(7,55,original,1)));
            state.advance(.4f);long generation=state.generation,probe=state.probe;
            state.observe(replacement,position);assertEquals(0,state.progress);assertNull(state.sampled);
            assertFalse(state.accepts(generation,probe,first));assertFalse(state.advance(1));
        }
        var state=new HostMining.Intent();var first=HostMiningCoordinates.target(0,0,0,position,null,1);
        state.observe(first,position);state.confirm(HostMiningCoordinates.sample(first,sample(0,0,original,1)));
        state.advance(.4f);state.observe(HostMiningCoordinates.target(0,0,0,position.add(1,0,0),null,1),position.add(1,0,0));
        assertEquals(0,state.progress);
    }

    @Test void translatedMiningMatchesEquivalentWorldPlaneOnBothSidesOfAllAxes() {
        for(int axis=0;axis<3;axis++)for(float sign:new float[]{-1,1}) {
            float[] origin={0,0,0},from={7,13,19},to={7,13,19};
            origin[axis]=axis==2?128:640;from[axis]=48*sign;to[axis]=-sign;
            var bytes=ByteBuffer.wrap(map()).order(ByteOrder.LITTLE_ENDIAN);
            for(int i=0;i<3;i++)bytes.putFloat(144+i*4,i==axis?sign:0);
            // Native axial types 0..2 use +axis without dot(normal, point).
            // Negative normals require the corresponding nonaxial plane type.
            int planeType=sign>0?axis:axis+3;bytes.putInt(160,planeType);
            var host=host(bytes.array());var brush=brush(origin[0],origin[1],origin[2],0,0,0);publish(host,brush,1);
            var start=minecraft(world(v(from[0],from[1],from[2]),brush));
            var end=minecraft(world(v(to[0],to[1],to[2]),brush));
            var hit=HostRaycast.trace(host,start,end);assertNotNull(hit,"axis="+axis+", sign="+sign);assertEquals(7,hit.slot);
            // Replace the remote world floor with exactly the translated brush plane.
            for(int i=0;i<3;i++)bytes.putFloat(124+i*4,i==axis?sign:0);
            bytes.putFloat(136,origin[axis]*sign);bytes.putInt(140,planeType);
            var staticHit=HostRaycast.trace(host(bytes.array()),start,end);assertNotNull(staticHit);assertEquals(0,staticHit.slot);
            assertEquals(staticHit.miningWorldCell,hit.miningWorldCell);
            assertNotEquals(hit.getBlockPos(),hit.miningWorldCell);
            var state=new HostMining.Intent();state.observe(key(hit,1),hit.miningWorldCell);
            assertEquals(staticHit.miningWorldCell,state.miningCell);
            long probe=state.probe;
            state.observe(key(hit,1),hit.miningWorldCell.add(10,0,0));
            assertEquals(staticHit.miningWorldCell.add(10,0,0),state.miningCell);assertEquals(probe,state.probe);
        }
    }

    @Test void rotatedWorldBoundarySelectsTheSolidSideWithoutFloatLocalRoundTrip() {
        for(float yaw:new float[]{37,45})for(float sign:new float[]{-1,1}) {
            var bytes=ByteBuffer.wrap(map()).order(ByteOrder.LITTLE_ENDIAN);
            bytes.putFloat(144,sign);bytes.putInt(160,sign>0?0:3);
            var host=host(bytes.array());publish(host,brush(640,0,0,0,yaw,0),1);
            var from=minecraft(v(640+48*sign,48*sign,7));var to=minecraft(v(640-sign,-sign,7));
            var hit=HostRaycast.trace(host,from,to);assertNotNull(hit);
            assertEquals(new BlockPos(sign>0?19:20,64,sign>0?0:-1),hit.miningWorldCell);
            for(float offset:new float[]{-.001f,.001f}) {
                var nearby=HostRaycast.trace(host,minecraft(v(640+48*sign+offset,48*sign,7)),
                    minecraft(v(640-sign+offset,-sign,7)));
                assertNotNull(nearby);
                assertEquals(new BlockPos(offset>0?20:19,64,offset>0?0:-1),nearby.miningWorldCell);
            }
        }
    }

    private static HostWorldState host(byte[] bytes) {
        var host=new HostWorldState();host.world(new Wire.Writer().i64(9).string("coordinate_test").toByteArray());
        host.bsp(new Wire.Writer().i64(9).i32((int)BspMap.checksum(bytes)).i32(bytes.length).bytes(bytes).toByteArray());
        host.edits().accept(new MapEdits.Snapshot(9,1,List.of()));return host;
    }
}
