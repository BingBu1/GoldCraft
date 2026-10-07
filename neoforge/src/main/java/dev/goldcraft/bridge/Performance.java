package dev.goldcraft.bridge;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardCopyOption;
import java.util.LinkedHashMap;
import java.util.Locale;
import java.util.Map;

/** Optional, bounded sandbox counters. No per-frame disk I/O. */
public final class Performance {
    private static final String DESTINATION=System.getenv("GOLDCRAFT_PERFORMANCE_LOG");
    private static final Map<String,Stat> STATS=new LinkedHashMap<>();
    private static long lastFlush;
    private static final class Stat {long count,total,max;}
    private Performance(){}
    public static long begin(){return DESTINATION==null?0:System.nanoTime();}
    public static synchronized void end(String name,long began) {
        if(began==0)return;
        long elapsed=System.nanoTime()-began;Stat s=STATS.computeIfAbsent(name,ignored->new Stat());
        s.count++;s.total+=elapsed;s.max=Math.max(s.max,elapsed);
    }
    public static synchronized void flush() {
        if(DESTINATION==null||System.nanoTime()-lastFlush<2_000_000_000L)return;lastFlush=System.nanoTime();
        StringBuilder json=new StringBuilder("{");
        for(var entry:STATS.entrySet()) {
            if(json.length()>1)json.append(',');Stat s=entry.getValue();
            json.append(String.format(Locale.ROOT,"\"%s\":{\"count\":%d,\"meanMs\":%.4f,\"maxMs\":%.4f}",entry.getKey(),s.count,(double)s.total/s.count/1e6,(double)s.max/1e6));
        }
        json.append('}');
        try {Path target=Path.of(DESTINATION),temporary=target.resolveSibling(target.getFileName()+".tmp");Files.writeString(temporary,json.toString());Files.move(temporary,target,StandardCopyOption.REPLACE_EXISTING);}
        catch(IOException ignored){/* Diagnostics cannot stop gameplay. */}
        STATS.clear();
    }
}
