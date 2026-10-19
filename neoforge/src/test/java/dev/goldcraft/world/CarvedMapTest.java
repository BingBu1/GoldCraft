package dev.goldcraft.world;

import dev.goldcraft.bridge.HostWorldState;
import dev.goldcraft.bridge.MapEdits;
import dev.goldcraft.bridge.Wire;
import java.io.BufferedWriter;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.Random;
import java.util.concurrent.TimeUnit;
import org.junit.jupiter.api.Test;
import static org.junit.jupiter.api.Assertions.*;
import static org.junit.jupiter.api.Assumptions.assumeTrue;
import static dev.goldcraft.world.CarvedVolume.*;

class CarvedMapTest {
    @Test void pairedDecompositionRetainsIndependentGeometryAndCombinedBudget() {
        var region=new Box(new P(-4,-4,-4),new P(4,4,4));
        var a=new Box(new P(-2,-2,-2),new P(2,2,2));
        var b=new Box(new P(0,-2,-2),new P(4,2,2));
        var cuts=List.of(a,b,a,new Box(new P(20,20,20),new P(24,24,24)));
        float n=(float)Math.sqrt(.5);
        var slope=new BspMap.Hull(new BspMap.Plane[]{new BspMap.Plane(v(n,n,0),0,3)},
            new BspMap.Node[]{new BspMap.Node(0,-1,-2)},0);
        var solid=new BspMap.Hull(new BspMap.Plane[0],new BspMap.Node[0],-2);
        var empty=new BspMap.Hull(new BspMap.Plane[0],new BspMap.Node[0],-1);
        var water=new BspMap.Hull(new BspMap.Plane[0],new BspMap.Node[0],-3);
        for(var hull:List.of(solid,empty,water,slope)) {
            for(var boxes:List.of(List.<Box>of(),cuts,List.of(region))) {
                var oldWork=new Work(4_194_304,4096);oldWork.use(17);
                var original=carve(hull,region,List.of(),oldWork);
                var remaining=carve(hull,region,boxes,oldWork);
                var work=new Work(oldWork.used,4096);work.use(17);
                var pair=carvePair(hull,region,boxes,work);
                assertEquals(original,pair.original());assertEquals(remaining,pair.remaining());
                assertEquals(oldWork.used,work.used);
                var shortWork=new Work(oldWork.used-1,4096);shortWork.use(17);
                assertThrows(IllegalArgumentException.class,()->carvePair(hull,region,boxes,shortWork));
                // Consumers may share uncut cells, but subtraction and body
                // expansion must leave those source faces and vertices intact.
                var body=new Box(new P(-1,-1,-2),new P(1,1,2));
                var expected=expand(remaining,body,new Work(4_194_304,4096));
                assertEquals(expected,expand(pair.remaining(),body,new Work(4_194_304,4096)));
                expand(pair.original(),body,new Work(4_194_304,4096));
                assertEquals(original,pair.original());assertEquals(remaining,pair.remaining());
            }
        }
        assertThrows(IllegalArgumentException.class,()->carvePair(solid,region,cuts,new Work(4_194_304,1)));
    }

    private static final MapEdits.Target WORLD=new MapEdits.Target(0,0,0);
    private static final Path MAP=Path.of("../sandbox/cs-client-b/Half-Life/cstrike/maps/cs_assault.bsp");
    private static BspMap.Vec v(float x,float y,float z) { return new BspMap.Vec(x,y,z); }
    private static List<MapEdits.Cut> opening(boolean low) {
        float z0=low?384.4f:358.4f,z1=low?428.4f:454.4f;
        return List.of(new MapEdits.Cut(2,WORLD,new MapEdits.Box(64,2864,z0,96,2960,z1)),
                       new MapEdits.Cut(3,WORLD,new MapEdits.Box(96,2864,z0,128,2960,z1)));
    }
    private static BspMap map() throws Exception {
        assumeTrue(Files.exists(MAP));var bsp=BspMap.read(Files.readAllBytes(MAP));assertEquals(0xf6725c06L,bsp.crc());return bsp;
    }
    @Test void realWallAdjacentHolesLowCeilingsAndRetainedClip() throws Exception {
        var bsp=map();var a=v(96,2784,406.4f);var b=v(96,2976,406.4f);
        var full=new CarvedMap(bsp,opening(false));var partial=new CarvedMap(bsp,opening(false).subList(0,1));
        for(int hull:new int[]{0,1,3}) {
            assertTrue(bsp.trace(0,hull,a,b).fraction()<1);
            assertEquals(1,full.trace(null,hull,a,b).fraction());
        }
        assertTrue(partial.trace(null,1,a,b).fraction()<1);
        assertTrue(full.trace(null,1,v(120,2784,406.4f),v(120,2976,406.4f)).fraction()<1);
        var low=new CarvedMap(bsp,opening(true));assertEquals(1,low.trace(null,3,a,b).fraction());
        assertTrue(low.trace(null,1,a,b).fraction()<1);
        var clipStart=v(-2021.544912f,1689.130686f,633.474396f);var clipEnd=v(-1925.544912f,1689.130686f,633.474396f);
        var clip=new CarvedMap(bsp,List.of(new MapEdits.Cut(2,WORLD,new MapEdits.Box(-2069.544912f,1641.130686f,585.474396f,-1877.544912f,1737.130686f,681.474396f))));
        assertEquals(1,bsp.trace(0,0,clipStart,clipEnd).fraction());
        var nativeStop=bsp.trace(0,1,clipStart,clipEnd);assertTrue(nativeStop.fraction()<1);
        var stop=clip.trace(null,1,clipStart,clipEnd);assertEquals(nativeStop.fraction(),stop.fraction(),1e-6);
        assertEquals(-1936.03125,stop.end().x(),1e-5);
        assertThrows(IllegalArgumentException.class,()->new CarvedMap(bsp,opening(false),10));
    }
    @Test void classificationUsesCutUnionAndKeepsTheRim() throws Exception {
        var bsp=map();var edit=new CarvedMap(bsp,opening(false));
        var inside=new BspMap.Bounds(v(92,2882,400),v(100,2890,410));
        assertEquals(BspMap.FILLED,bsp.classify(0,inside));
        assertEquals(BspMap.CLEAR,edit.classify(null,inside));
        assertEquals(BspMap.MIXED,edit.classify(null,new BspMap.Bounds(v(126,2882,400),v(132,2890,410))));
        assertEquals(BspMap.FILLED,edit.classify(null,new BspMap.Bounds(v(130,2882,400),v(136,2890,410))));
        var random=new Random(3471);
        for(int i=0;i<2000;i++) {
            var p=v(40+random.nextFloat()*120,2870+random.nextFloat()*100,340+random.nextFloat()*130);
            boolean cut=opening(false).stream().anyMatch(c->p.x()>c.box().minX()&&p.x()<c.box().maxX()&&p.y()>c.box().minY()&&p.y()<c.box().maxY()&&p.z()>c.box().minZ()&&p.z()<c.box().maxZ());
            assertEquals(bsp.contents(0,0,p)==BspMap.SOLID&&!cut,edit.contents(null,0,p)==BspMap.SOLID);
        }
    }
    @Test void editAndPhysicsCommitTogetherAndRestoreWithoutAcceptingOlderState() throws Exception {
        var bsp=map();var bytes=Files.readAllBytes(MAP);var host=new HostWorldState();
        host.world(new Wire.Writer().i64(31).string("cs_assault").toByteArray());
        host.bsp(new Wire.Writer().i64(31).i32((int)bsp.crc()).i32(bytes.length).bytes(bytes).toByteArray());
        var a=v(96,2784,406.4f);var b=v(96,2976,406.4f);
        host.edits().accept(new MapEdits.Snapshot(31,3,opening(false)));
        var good=host.collision();assertEquals(1,good.trace(null,1,a,b).fraction());
        var invalidTarget=new MapEdits.Target(1,4,4095);
        assertThrows(IllegalArgumentException.class,()->host.edits().accept(new MapEdits.Delta(31,3,4,MapEdits.ADD,invalidTarget,opening(false).getFirst().box())));
        assertSame(good,host.collision());assertEquals(3,host.edits().revision());
        assertEquals(MapEdits.Applied.NEED_SNAPSHOT,host.edits().accept(new MapEdits.Delta(31,5,6,MapEdits.CLEAR,null,null)));
        assertSame(good,host.collision());assertFalse(host.edits().ready());
        host.edits().accept(new MapEdits.Snapshot(31,6,List.of()));
        assertTrue(host.collision().trace(null,1,a,b).fraction()<1);assertTrue(host.edits().ready());
        var restored=host.collision();assertEquals(MapEdits.Applied.IGNORED,host.edits().accept(new MapEdits.Snapshot(31,3,opening(false))));assertSame(restored,host.collision());
        host.world(new Wire.Writer().i64(32).string("cs_assault").toByteArray());assertNull(host.collision());assertTrue(host.edits().cuts().isEmpty());
    }
    @Test void blastCellsPreserveRemainingMaterial() throws Exception {
        var bsp=map();
        for(int radius:new int[]{2,4,5}) {
            var cuts=new ArrayList<MapEdits.Cut>();
            for(int x=-radius;x<=radius;x++)for(int y=-radius;y<=radius;y++)for(int z=-radius;z<=radius;z++)
                cuts.add(new MapEdits.Cut(cuts.size()+1,WORLD,new MapEdits.Box(96+x*32,2880+y*32,384+z*32,128+x*32,2912+y*32,416+z*32)));
            long started=System.nanoTime();var edit=new CarvedMap(bsp,cuts);long prepared=System.nanoTime();
            int queries=0;
            for(int x=-radius-1;x<=radius+1;x++)for(int y=-radius-1;y<=radius+1;y++)for(int z=-radius-1;z<=radius+1;z++) {
                var q=new BspMap.Bounds(v(96+x*32,2880+y*32,384+z*32),v(128+x*32,2912+y*32,416+z*32));
                boolean inside=Math.abs(x)<=radius&&Math.abs(y)<=radius&&Math.abs(z)<=radius;
                assertEquals(inside?BspMap.CLEAR:bsp.classify(0,q),edit.classify(null,q));queries++;
            }
            System.out.printf("blastCollision cuts=%d prepareMs=%.3f queries=%d queryMs=%.3f%n",cuts.size(),(prepared-started)/1e6,queries,(System.nanoTime()-prepared)/1e6);
            var unmerged=cuts.stream().map(c->{var b=c.box();return new CarvedVolume.Box(
                new CarvedVolume.P(b.minX(),b.minY(),b.minZ()),new CarvedVolume.P(b.maxX(),b.maxY(),b.maxZ()));}).toList();
            var random=new Random(954+radius);
            double maxEndError=0,maxFractionError=0;
            for(int hull=0;hull<4;hull++) {
                var reference=new CarvedHull(bsp,0,hull,unmerged,4_194_304);
                for(int i=0;i<200;i++) {
                    var a=v(96+(random.nextFloat()*2-1)*(radius+2)*32,2880+(random.nextFloat()*2-1)*(radius+2)*32,384+(random.nextFloat()*2-1)*(radius+2)*32);
                    var b=v(a.x()+random.nextFloat()*128-64,a.y()+random.nextFloat()*128-64,a.z()+random.nextFloat()*128-64);
                    var expected=reference.affects(a,b)?reference.trace(a,b):bsp.trace(0,hull,a,b);
                    var actual=edit.trace(null,hull,a,b);
                    var label="radius="+radius+" hull="+hull+" a="+a+" b="+b+" old="+expected+" new="+actual;
                    // Different exact union partitions can round axial contact
                    // coordinates differently. Bound physical error far below
                    // GoldSrc's 1/32-unit trace epsilon, as well as the fraction.
                    maxFractionError=Math.max(maxFractionError,Math.abs(expected.fraction()-actual.fraction()));
                    assertEquals(expected.fraction(),actual.fraction(),1e-5,label);
                    for(int axis=0;axis<3;axis++) {
                        maxEndError=Math.max(maxEndError,Math.abs(expected.end().axis(axis)-actual.end().axis(axis)));
                        assertEquals(expected.end().axis(axis),actual.end().axis(axis),.001);
                    }
                    assertEquals(expected.startSolid(),actual.startSolid(),label);assertEquals(expected.allSolid(),actual.allSolid(),label);
                    assertEquals(reference.affects(a,a)?reference.contents(a):bsp.contents(0,hull,a),edit.contents(null,hull,a),label);
                }
            }
            System.out.printf("blastTraceParity cuts=%d traces=800 maxEndUnits=%.9f maxFraction=%.9f%n",cuts.size(),maxEndError,maxFractionError);
        }
    }
    private static void write(BufferedWriter out,Object... values) throws Exception {
        for(var value:values) { out.write(value.toString());out.write(' '); }out.newLine();
    }
    private static void writeHull(BufferedWriter out,BspMap.Hull hull) throws Exception {
        write(out,hull.planes().length);
        for(var p:hull.planes()) {
            var n=p.type()<3?v(p.type()==0?1:0,p.type()==1?1:0,p.type()==2?1:0):p.normal();
            write(out,(double)n.x(),(double)n.y(),(double)n.z(),(double)p.distance());
        }
        write(out,hull.nodes().length);for(var n:hull.nodes())write(out,n.plane(),n.front(),n.back());write(out,hull.root());
    }
    @Test void actualBspTracesMatchTheX86NativeCarvingCore() throws Exception {
        var bsp=map();var executable=Path.of("../build/native-x86/Release/goldcraft_carved_hull_probe.exe").toAbsolutePath();assumeTrue(Files.exists(executable));
        var work=Path.of("build/carved-parity");Files.createDirectories(work);int comparisons=0;
        var random=new Random(80391);
        for(int hull=0;hull<4;hull++) {
            var input=work.resolve("input-"+hull+".txt");var output=work.resolve("output-"+hull+".txt");var errors=work.resolve("errors-"+hull+".txt");
            var expected=new ArrayList<BspMap.Trace>();var contents=new ArrayList<int[]>();
            try(var out=Files.newBufferedWriter(input)) {
                writeHull(out,bsp.hull(0,0));writeHull(out,bsp.hull(0,hull));
                int width=hull==0?0:hull==2?32:16,height=hull==0?0:hull==1?36:hull==3?18:32;
                write(out,-width,-width,-height,width,width,height);write(out,6);
                for(int group=0;group<6;group++) {
                    float x=group<2?96:-1500+random.nextFloat()*2900,y=group<2?2912:-400+random.nextFloat()*3000,z=group<2?406.4f:random.nextFloat()*800;
                    var cuts=group<2?opening(group==1):List.of(new MapEdits.Cut(2,WORLD,new MapEdits.Box(x-32,y-48,z-48,x,y+48,z+48)),new MapEdits.Cut(3,WORLD,new MapEdits.Box(x,y-48,z-48,x+32,y+48,z+48)));
                    write(out,cuts.size());for(var c:cuts) { var q=c.box();write(out,(double)q.minX(),(double)q.minY(),(double)q.minZ(),(double)q.maxX(),(double)q.maxY(),(double)q.maxZ()); }
                    var boxes=cuts.stream().map(c->{var q=c.box();return new CarvedVolume.Box(new CarvedVolume.P(q.minX(),q.minY(),q.minZ()),new CarvedVolume.P(q.maxX(),q.maxY(),q.maxZ()));}).toList();
                    var edit=new CarvedHull(bsp,0,hull,boxes,4_194_304);
                    write(out,200);
                    for(int i=0;i<200;i++) {
                        var a=v(x-64+random.nextFloat()*128,y-128+random.nextFloat()*256,z-90+random.nextFloat()*180);
                        var b=i%11==0?a:v(x-64+random.nextFloat()*128,y-128+random.nextFloat()*256,z-90+random.nextFloat()*180);
                        write(out,(double)a.x(),(double)a.y(),(double)a.z(),(double)b.x(),(double)b.y(),(double)b.z());
                        expected.add(edit.trace(a,b));contents.add(new int[]{edit.contents(a),edit.contents(b)});
                    }
                }
            }
            var process=new ProcessBuilder(executable.toString()).redirectInput(input.toFile()).redirectOutput(output.toFile()).redirectError(errors.toFile()).start();
            try { assertTrue(process.waitFor(60,TimeUnit.SECONDS),"Native oracle timed out"); }
            finally { if(process.isAlive())process.destroyForcibly().waitFor(); }
            assertEquals(0,process.exitValue(),Files.readString(errors));var lines=Files.readAllLines(output);assertEquals(expected.size(),lines.size());
            for(int i=0;i<lines.size();i++) {
                var values=java.util.Arrays.stream(lines.get(i).trim().split("\\s+")).mapToDouble(Double::parseDouble).toArray();var a=expected.get(i);
                String label="hull="+hull+" query="+i;
                assertEquals(values[0],a.fraction(),1e-6,label);assertEquals(values[1]!=0,a.startSolid(),label);assertEquals(values[2]!=0,a.allSolid(),label);
                assertEquals(values[3]!=0,a.inOpen(),label);assertEquals(values[4]!=0,a.inWater(),label);
                for(int axis=0;axis<3;axis++)assertEquals(values[5+axis],a.normal().axis(axis),1e-6,label);
                assertEquals(values[8],a.planeDistance(),.001,label);assertEquals((int)values[9],contents.get(i)[0],label);assertEquals((int)values[10],contents.get(i)[1],label);comparisons++;
            }
        }
        assertEquals(4800,comparisons);System.out.println("Carved Java/x86 native parity: "+comparisons+" actual BSP traces and 9600 contents queries passed");
    }
}
