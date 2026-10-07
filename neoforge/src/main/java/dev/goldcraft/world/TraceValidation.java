package dev.goldcraft.world;

import com.google.gson.GsonBuilder;
import dev.goldcraft.GoldCraft;
import dev.goldcraft.bridge.HostWorldState;
import dev.goldcraft.bridge.Wire;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.*;

/** Optional sandbox oracle: compares parsed BSP hulls to the running engine's pfnTraceModel. */
public final class TraceValidation {
    private record Query(long id,int hull,BspMap.Vec start,BspMap.Vec end,BspMap.Trace expected) {}
    private final ArrayDeque<Query> unsent=new ArrayDeque<>();
    private final Map<Long,Query> pending=new HashMap<>();
    private final List<Map<String,Object>> failures=new ArrayList<>();
    private final List<Map<String,Object>> clipSamples=new ArrayList<>();
    private long epoch;
    private int received;
    private float maxFractionError,maxPositionError;
    private boolean finished;
    public void reset(){epoch=0;received=0;maxFractionError=maxPositionError=0;finished=false;unsent.clear();pending.clear();failures.clear();clipSamples.clear();}
    public boolean finished(){return finished;}
    public int received(){return received;}
    public int failed(){return failures.size();}
    public void tick(HostWorldState state) {
        if(System.getenv("GOLDCRAFT_TRACE_REPORT_DIR")==null||state.geometry()==null||state.actors().isEmpty())return;
        if(epoch!=state.epoch()){reset();epoch=state.epoch();prepare(state);}
        for(int i=0;i<8&&!unsent.isEmpty();i++) {
            Query q=unsent.peekFirst();Wire.Writer w=new Wire.Writer().i64(epoch).i64(q.id).i32(q.hull).i32(0);
            w.f32(q.start.x()).f32(q.start.y()).f32(q.start.z()).f32(q.end.x()).f32(q.end.y()).f32(q.end.z());
            if(!GoldCraft.sendToHost(Wire.TRACE_QUERY,w.toByteArray()))break;
            unsent.removeFirst();pending.put(q.id,q);
        }
    }
    private void prepare(HostWorldState state) {
        BspMap bsp=state.geometry();Random random=new Random(0x47434632);
        HostWorldState.Vector a=state.actors().getFirst().origin();
        for(int i=0;i<384;i++) {
            int hull=i%4;float radius=i<128?96:1024;
            BspMap.Vec start=new BspMap.Vec(a.x()+(random.nextFloat()-0.5f)*radius,a.y()+(random.nextFloat()-0.5f)*radius,a.z()+(random.nextFloat()-0.5f)*radius);
            BspMap.Vec end=i<128?new BspMap.Vec(start.x(),start.y(),start.z()-256):start.add(new BspMap.Vec((random.nextFloat()-0.5f)*2048,(random.nextFloat()-0.5f)*2048,(random.nextFloat()-0.5f)*512));
            unsent.addLast(new Query(i+1,hull,start,end,bsp.trace(0,hull,start,end)));
        }
        if(state.map().equals("cs_assault")&&bsp.crc()==0xf6725c06L) {
            long id=385;
            for(int hull:new int[]{1,3})for(float[] route:new float[][]{{-1632,-352,-1632,-512},{-1440,1920,-1600,1920}}) {
                float z=(hull==1?36:18)+0.03125f;
                var from=new BspMap.Vec(route[0],route[1],z);var to=new BspMap.Vec(route[2],route[3],z);
                unsent.addLast(new Query(id++,hull,from,to,bsp.trace(0,hull,from,to)));
            }
        }
    }
    public void result(byte[] bytes,HostWorldState state) {
        Wire.Reader r=new Wire.Reader(bytes);long world=r.i64(),id=r.i64();int hull=r.i32(),slot=r.i32();r.i32();float fraction=r.f32();int flags=r.i32();
        BspMap.Vec end=new BspMap.Vec(r.f32(),r.f32(),r.f32()),normal=new BspMap.Vec(r.f32(),r.f32(),r.f32());float distance=r.f32();r.finish();
        if(world!=epoch)return;Query q=pending.remove(id);if(q==null||q.hull!=hull||slot!=0)return;
        received++;BspMap.Trace expected=q.expected;
        float fractionError=Math.abs(expected.fraction()-fraction),positionError=(float)Math.sqrt(end.subtract(expected.end()).dot(end.subtract(expected.end())));
        maxFractionError=Math.max(maxFractionError,fractionError);maxPositionError=Math.max(maxPositionError,positionError);
        boolean solid=expected.startSolid()==((flags&1)!=0)&&expected.allSolid()==((flags&2)!=0);
        if(q.id>=385)clipSamples.add(Map.of("id",q.id,"hull",hull,"start",q.start,"end",q.end,"expected",expected,
            "engine",Map.of("fraction",fraction,"end",end,"normal",normal,"flags",flags)));
        if(fractionError>0.0002f||positionError>0.05f||!solid) {
            Map<String,Object> mismatch=new LinkedHashMap<>();mismatch.put("id",id);mismatch.put("hull",hull);mismatch.put("start",q.start);mismatch.put("end",q.end);mismatch.put("expected",expected);
            mismatch.put("engine",Map.of("fraction",fraction,"end",end,"normal",normal,"planeDistance",distance,"flags",flags));failures.add(mismatch);
        }
        if(unsent.isEmpty()&&pending.isEmpty()&&!finished) {
            finished=true;Map<String,Object> report=new LinkedHashMap<>();report.put("map",state.map());report.put("epoch",Long.toUnsignedString(epoch));report.put("bspCrc32",state.geometry().crc());
            report.put("received",received);report.put("failed",failures.size());report.put("maxFractionError",maxFractionError);report.put("maxPositionErrorGoldSrcUnits",maxPositionError);report.put("failures",failures);report.put("playerClipSamples",clipSamples);
            Path path=Path.of(System.getenv("GOLDCRAFT_TRACE_REPORT_DIR"),"trace-validation-"+state.map()+"-"+Long.toUnsignedString(epoch)+".json");
            try{Files.writeString(path,new GsonBuilder().setPrettyPrinting().create().toJson(report));}catch(java.io.IOException e){GoldCraft.LOGGER.error("Trace report failed",e);}
            GoldCraft.LOGGER.info("Engine/BSP trace comparison finished: {} cases, {} mismatches, max position error {} GS units",received,failures.size(),maxPositionError);
        }
    }
}
