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

class CarvedMapTest {
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
