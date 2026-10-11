package dev.goldcraft.bridge;

import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.lang.management.ManagementFactory;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashMap;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.concurrent.CopyOnWriteArrayList;
import java.util.concurrent.TimeUnit;
import java.util.zip.CRC32;

/** Isolated actual x64 Java BridgeLink to actual x86 Endpoint. Synthetic payloads do not exercise rendering. */
public final class SharedBulkInteropMain {
    private final Map<String,Long> expected=new HashMap<>();
    private long receipts, receivedBytes;
    private final Map<Integer,Long> receivedByType=new LinkedHashMap<>();
    private long verificationNanos;
    private final List<Integer> receivedOrder=new ArrayList<>();
    static byte[] pattern(int size,int seed){byte[] data=new byte[size];for(int i=0;i<size;i++)data[i]=(byte)(i*13+seed);return data;}
    byte[] payload(int type,int size,int seed){byte[] data=pattern(size,seed);CRC32 crc=new CRC32();crc.update(data);expected.put(type+":"+size,crc.getValue());return data;}
    void collect(BridgeLink link){
        long start=System.nanoTime();
        for(var message:link.drain()){
            if(message.type()!=Wire.DAMAGE_RESULT||message.payload().length!=12)throw new AssertionError("Unexpected native response");
            var b=ByteBuffer.wrap(message.payload()).order(ByteOrder.LITTLE_ENDIAN);int type=b.getInt(),length=b.getInt();long crc=Integer.toUnsignedLong(b.getInt());
            if(!Long.valueOf(crc).equals(expected.get(type+":"+length)))throw new AssertionError("Native payload CRC mismatch type="+type+" length="+length);
            receipts++;receivedBytes+=length;receivedByType.merge(type,1L,Long::sum);receivedOrder.add(type);
        }
        verificationNanos+=System.nanoTime()-start;
    }
    static void check(boolean value,String reason){if(!value)throw new AssertionError(reason);}
    void until(BridgeLink link,java.util.function.BooleanSupplier condition,long timeoutMs)throws Exception{
        long end=System.nanoTime()+timeoutMs*1_000_000L;
        while(!condition.getAsBoolean()&&System.nanoTime()<end){collect(link);Thread.sleep(1);}
        check(condition.getAsBoolean(),"Timed out; link="+link.connected()+" error="+link.lastError());
    }
    static long cpuNanos(){return ((com.sun.management.OperatingSystemMXBean)ManagementFactory.getOperatingSystemMXBean()).getProcessCpuTime();}
    static String json(Object value)throws Exception{
        if(value==null)return "null";
        if(value instanceof Number||value instanceof Boolean)return value.toString();
        if(value instanceof String s)return "\""+s.replace("\\","\\\\").replace("\"","\\\"").replace("\n","\\n").replace("\r","\\r")+"\"";
        if(value instanceof long[] array){List<String> parts=new ArrayList<>();for(long n:array)parts.add(Long.toString(n));return "["+String.join(",",parts)+"]";}
        if(value instanceof Map<?,?> map){List<String> parts=new ArrayList<>();for(var e:map.entrySet())parts.add(json(e.getKey().toString())+":"+json(e.getValue()));return "{"+String.join(",",parts)+"}";}
        if(value.getClass().isRecord()){Map<String,Object> map=new LinkedHashMap<>();for(var c:value.getClass().getRecordComponents())map.put(c.getName(),c.getAccessor().invoke(value));return json(map);}
        return json(value.toString());
    }
    public static void main(String[] args)throws Exception{
        if(args.length<3)throw new IllegalArgumentException("receiver.exe paced|rollover|burst output.json [frames]");
        new SharedBulkInteropMain().run(args);
    }
    void run(String[] args)throws Exception{
        String mode=args[1];boolean paced=mode.startsWith("paced");int frames=args.length>3?Integer.parseInt(args[3]):360;
        int entityType=mode.equals("paced-reliable")?Wire.SECTION_MESH:Wire.ENTITY_FRAME;
        int poseCount=(frames*50+59)/60;
        int warmups=paced?120:0;
        int target=paced?frames+poseCount+warmups:mode.equals("rollover")?130:6;
        long timeout=paced?frames*1000L/60+12000:mode.equals("rollover")?120000:12000;
        List<String> command=new ArrayList<>(List.of(args[0],"--messages",Integer.toString(target),"--timeout-ms",Long.toString(timeout),"--quiet"));
        if(mode.equals("burst")){command.add("--pause-ms");command.add("1800");}
        Process nativePeer=new ProcessBuilder(command).redirectErrorStream(true).start();
        var output=new BufferedReader(new InputStreamReader(nativePeer.getInputStream()));String ready=output.readLine();
        check(ready!=null&&ready.startsWith("READY "),"No native readiness: "+ready);int port=Integer.parseInt(ready.substring(6));
        List<String> nativeLines=new CopyOnWriteArrayList<>();Thread reader=Thread.ofPlatform().daemon().start(()->{try{String line;while((line=output.readLine())!=null)nativeLines.add(line);}catch(Exception e){nativeLines.add(e.toString());}});
        Map<String,Object> result=new LinkedHashMap<>();long start=System.nanoTime(),cpu=cpuNanos();
        BridgeLink.Metrics metrics;
        try(var link=new BridgeLink(new BridgeLink.Config(port,Wire.key("00112233445566778899aabbccddeeff"),Wire.key("ffeeddccbbaa99887766554433221100"),Wire.FABRIC_CLIENT,Wire.HOST_CLIENT))){
            until(link,link::connected,5000);
            boolean useShared=!"0".equals(System.getenv("GOLDCRAFT_SHARED_MEMORY"));
            if(useShared)until(link,()->link.metrics().enabled()==1,5000);
            else Thread.sleep(50);
            BridgeLink.Metrics warmupMetrics=null;
            long nativeCpuStart=nativePeer.info().totalCpuDuration().orElse(java.time.Duration.ZERO).toNanos();
            if(paced){
                byte[] entity=payload(entityType,1153433,7),pose=payload(Wire.PLAYER_POSE,64,11);
                for(int i=0;i<warmups;i++) {check(link.send(entityType,entity),"Warmup rejected");long count=i+1;until(link,()->receipts==count,5000);}
                warmupMetrics=link.metrics();
                receipts=receivedBytes=verificationNanos=0;receivedByType.clear();receivedOrder.clear();
                start=System.nanoTime();cpu=cpuNanos();nativeCpuStart=nativePeer.info().totalCpuDuration().orElse(java.time.Duration.ZERO).toNanos();
                int sentFrames=0,sentPoses=0;long begin=System.nanoTime();
                while(sentFrames<frames||sentPoses<poseCount){
                    long elapsed=System.nanoTime()-begin;
                    if(sentFrames<frames&&elapsed>=sentFrames*1_000_000_000L/60){check(link.send(entityType,entity),"Entity enqueue rejected");sentFrames++;}
                    if(sentPoses<poseCount&&elapsed>=sentPoses*1_000_000_000L/50){check(link.send(Wire.PLAYER_POSE,pose),"Pose enqueue rejected");sentPoses++;}
                    collect(link);Thread.sleep(1);
                }
                until(link,()->receipts==target-warmups,5000);
            }else if(mode.equals("rollover")){
                byte[] maximum=payload(Wire.SECTION_MESH,Wire.MAX_PAYLOAD,17);
                for(int i=0;i<130;i++){check(link.send(Wire.SECTION_MESH,maximum),"Maximum enqueue rejected");long next=i+1;until(link,()->receipts>=next,5000);}
                check(receivedBytes>0x1_0000_0000L,"32-bit rollover not exercised");
            }else if(mode.equals("burst")){
                byte[] maximum=payload(Wire.SECTION_MESH,Wire.MAX_PAYLOAD,17);
                check(link.send(Wire.SECTION_MESH,maximum),"First burst rejected");
                until(link,()->link.metrics().sharedFrames()[Wire.SECTION_MESH]>=1,5000);
                check(link.send(Wire.SECTION_MESH,maximum),"Second burst rejected");
                until(link,()->link.metrics().sharedFrames()[Wire.SECTION_MESH]>=2,5000);
                check(link.send(Wire.PARTICLE_TEXTURE,payload(Wire.PARTICLE_TEXTURE,65536,31)),"Blocked enqueue rejected");
                until(link,()->link.metrics().ringFullWaits()>0,1000);
                check(link.send(Wire.REMOVE_SECTION,payload(Wire.REMOVE_SECTION,8,37)),"Ordered enqueue rejected");
                check(link.send(Wire.PLAYER_POSE,payload(Wire.PLAYER_POSE,64,41)),"Urgent pose rejected");
                check(link.send(Wire.CAMERA,payload(Wire.CAMERA,64,43)),"Urgent camera rejected");
                check(link.send(Wire.HEARTBEAT,new byte[0]),"Urgent heartbeat rejected");
                until(link,()->link.metrics().tcpFrames()[Wire.PLAYER_POSE]==1&&link.metrics().tcpFrames()[Wire.CAMERA]==1&&link.metrics().tcpFrames()[Wire.HEARTBEAT]>=1,1000);
                check(link.metrics().sharedFrames()[Wire.PARTICLE_TEXTURE]==0,"Blocked bulk unexpectedly escaped full ring");
                until(link,()->receipts==target,7000);
                check(link.metrics().ringHighWater()==SharedBulkSender.CAPACITY,"Ring did not fill");
                check(receivedOrder.indexOf(Wire.PLAYER_POSE)<receivedOrder.indexOf(Wire.PARTICLE_TEXTURE),"Pose did not bypass blocked bulk");
                check(receivedOrder.indexOf(Wire.CAMERA)<receivedOrder.indexOf(Wire.PARTICLE_TEXTURE),"Camera did not bypass blocked bulk");
                check(receivedOrder.indexOf(Wire.PARTICLE_TEXTURE)<receivedOrder.indexOf(Wire.REMOVE_SECTION),"Reliable order changed");
            }else throw new IllegalArgumentException("Unknown mode");
            collect(link);metrics=link.metrics();
            result.put("elapsedNanos",System.nanoTime()-start);result.put("javaProcessCpuNanos",cpuNanos()-cpu);
            result.put("nativeProcessCpuNanosBeforeExit",nativePeer.info().totalCpuDuration().orElse(java.time.Duration.ZERO).toNanos()-nativeCpuStart);
            result.put("warmupFrames",warmups);result.put("warmupMetrics",warmupMetrics);result.put("receivedOrder",receivedOrder.toString());
            result.put("transport",useShared?"shared":"tcp");result.put("mode",mode);result.put("messagesSubmitted",target-warmups);result.put("messagesVerified",receipts);result.put("payloadBytesVerified",receivedBytes);result.put("verificationNanos",verificationNanos);result.put("receivedByType",receivedByType);result.put("metrics",metrics);result.put("java",System.getProperty("java.runtime.version"));
            check(metrics.queuedBytes()==0&&metrics.blockedBytes()==0&&metrics.pendingBytes()==0,"Pending data at finish");
        }finally{
            if(!nativePeer.waitFor(8,TimeUnit.SECONDS)){nativePeer.destroy();throw new AssertionError("Native receiver did not finish");}
            reader.join(2000);
        }
        result.put("nativeExit",nativePeer.exitValue());result.put("nativeOutput",String.join("\n",nativeLines));
        check(nativePeer.exitValue()==0,"Native failed: "+nativeLines);
        String json=json(result);Files.writeString(Path.of(args[2]),json+"\n");System.out.println(json);
    }
}
