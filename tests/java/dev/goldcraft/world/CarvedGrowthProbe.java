package dev.goldcraft.world;

import dev.goldcraft.bridge.MapEdits;
import java.io.*;
import java.lang.management.ManagementFactory;
import java.nio.file.*;
import java.util.*;

/** Run separately against frozen production classes before/after optimization. */
public final class CarvedGrowthProbe {
    private static volatile long sink;
    private static long median(long[] samples){Arrays.sort(samples);return samples[samples.length/2];}
    private record Query(BspMap.Vec a,BspMap.Vec b) {}
    private static BspMap.Vec v(float x,float y,float z){return new BspMap.Vec(x,y,z);}
    private static final MapEdits.Target WORLD=new MapEdits.Target(0,0,0);
    private static List<MapEdits.Cut> cuts(int count) {
        var result=new ArrayList<MapEdits.Cut>();
        for(int i=0;i<count;i++) {
            float x=32+(i%16)*32,y=2800+((i/16)%8)*32,z=288+(i/128)*48;
            result.add(new MapEdits.Cut(i+2,WORLD,new MapEdits.Box(x,y,z,x+16,y+16,z+24)));
        }
        return result;
    }
    private static List<Query> queries(List<MapEdits.Cut> cuts) {
        var result=new ArrayList<Query>();var random=new Random(79261);
        for(int i=0;i<3000;i++) {
            var c=cuts.get(i%cuts.size()).box();
            var a=v(c.minX()-48+random.nextFloat()*112,c.minY()-48+random.nextFloat()*112,c.minZ()-48+random.nextFloat()*112);
            var b=i%7==0?a:v(a.x()+random.nextFloat()*64-32,a.y()+random.nextFloat()*64-32,a.z()+random.nextFloat()*64-32);
            result.add(new Query(a,b));
        }
        for(var cut:cuts.subList(0,Math.min(24,cuts.size()))) {
            var c=cut.box();
            for(float boundary:new float[]{c.minX()-16,c.minX(),c.maxX(),c.maxX()+16})
                for(float x:new float[]{Math.nextDown(boundary),boundary,Math.nextUp(boundary)})
                    result.add(new Query(v(x,c.minY()-64,c.minZ()+12),v(x,c.maxY()+64,c.minZ()+12)));
        }
        return result;
    }
    private static int[] observe(CarvedMap map,int hull,Query q) {
        var t=map.trace(null,hull,q.a(),q.b());
        return new int[]{Float.floatToIntBits(t.fraction()),Float.floatToIntBits(t.end().x()),
            Float.floatToIntBits(t.end().y()),Float.floatToIntBits(t.end().z()),Float.floatToIntBits(t.normal().x()),
            Float.floatToIntBits(t.normal().y()),Float.floatToIntBits(t.normal().z()),Float.floatToIntBits(t.planeDistance()),
            (t.startSolid()?1:0)|(t.allSolid()?2:0)|(t.inOpen()?4:0)|(t.inWater()?8:0),
            map.contents(null,hull,q.a()),map.contents(null,hull,q.b())};
    }
    public static void main(String[] args)throws Exception {
        if(args.length!=3)throw new IllegalArgumentException("bsp --write|--compare corpus");
        var bsp=BspMap.read(Files.readAllBytes(Path.of(args[0])));
        if(bsp.crc()!=0xf6725c06L)throw new IllegalArgumentException("Unexpected cs_assault BSP");
        boolean compare=args[1].equals("--compare");
        var thread=(com.sun.management.ThreadMXBean)ManagementFactory.getThreadMXBean();
        thread.setThreadAllocatedMemoryEnabled(true);long id=Thread.currentThread().threadId();
        try(var output=compare?null:new DataOutputStream(new BufferedOutputStream(Files.newOutputStream(Path.of(args[2]))));
            var input=compare?new DataInputStream(new BufferedInputStream(Files.newInputStream(Path.of(args[2])))):null) {
            int total=0;
            for(int count:new int[]{32,128,384}) {
                var cuts=cuts(count);var queries=queries(cuts);
                // Warm the actual construction/query methods before timing.
                for(int warm=0;warm<12;warm++) {
                    var map=new CarvedMap(bsp,cuts);
                    for(int h=0;h<4;h++)for(var q:queries)observe(map,h,q);
                }
                long[] builds=new long[7],allocations=new long[7],times=new long[7],queryAllocations=new long[7];
                CarvedMap map=null;long digest=0;
                for(int round=0;round<7;round++) {
                    long allocation=thread.getThreadAllocatedBytes(id),begin=System.nanoTime();
                    map=new CarvedMap(bsp,cuts);
                    builds[round]=System.nanoTime()-begin;allocations[round]=thread.getThreadAllocatedBytes(id)-allocation;
                    begin=System.nanoTime();allocation=thread.getThreadAllocatedBytes(id);digest=0;
                    for(int h=0;h<4;h++)for(var q:queries) {
                        var row=observe(map,h,q);for(int value:row)digest=digest*31+value;
                    }
                    times[round]=System.nanoTime()-begin;queryAllocations[round]=thread.getThreadAllocatedBytes(id)-allocation;
                    sink=digest;
                }
                for(int h=0;h<4;h++)for(var q:queries) {
                    for(int value:observe(map,h,q)) {
                        if(compare) {if(input.readInt()!=value)throw new AssertionError("Trace/contents difference at query "+total);}
                        else output.writeInt(value);
                    }
                    total++;
                }
                System.out.printf(Locale.ROOT,"{\"cuts\":%d,\"buildMs\":%.6f,\"buildBytes\":%d,\"queryMs\":%.6f,\"queryBytes\":%d,\"queries\":%d,\"digest\":\"%d\"}%n",
                    count,median(builds)/1e6,median(allocations),median(times)/1e6,median(queryAllocations),queries.size()*4,digest);
            }
            if(compare&&input.read()!=-1)throw new AssertionError("Unconsumed reference corpus");
            System.out.println("{\"traceContentsQueries\":"+total+",\"compared\":"+compare+"}");
        }
    }
}
