package dev.goldcraft.client;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.Comparator;
import java.util.List;
import java.util.Map;
import java.util.TreeMap;

/** Lights belonging to successfully exported sections, independent of the camera. */
final class SectionLightSnapshot {
    // The current 5 x 5 x 5 section window contains at most 512,000 blocks.
    static final int MAX_LIGHTS=524288,MAX_SECTION_LIGHTS=4096;
    record Lamp(float x,float y,float z,float radius,int color) {
        Lamp {
            if(!Float.isFinite(x)||!Float.isFinite(y)||!Float.isFinite(z)||!Float.isFinite(radius)||radius<=0)
                throw new IllegalArgumentException("Invalid section light");
        }
    }
    private record Section(int x,int y,int z) {}
    private final Map<Section,List<Lamp>> sections=new TreeMap<>(Comparator.comparingInt(Section::x)
        .thenComparingInt(Section::y).thenComparingInt(Section::z));
    private int count;
    private long cachedEpoch;
    private byte[] cached;

    void replace(int x,int y,int z,List<Lamp> lights) {
        Section section=new Section(x,y,z);
        List<Lamp> previous=sections.get(section);
        if(lights.equals(previous))return;
        if(lights.isEmpty()){remove(x,y,z);return;}
        if(lights.size()>MAX_SECTION_LIGHTS||lights.size()>MAX_LIGHTS-count+(previous==null?0:previous.size()))
            throw new IllegalArgumentException("Section light budget exceeded");
        List<Lamp> copy=List.copyOf(lights);
        sections.put(section,copy);
        count+=copy.size()-(previous==null?0:previous.size());
        cached=null;
    }
    void remove(int x,int y,int z) {
        List<Lamp> removed=sections.remove(new Section(x,y,z));
        if(removed!=null){count-=removed.size();cached=null;}
    }
    void clear() {
        sections.clear();count=0;cachedEpoch=0;cached=null;
    }
    /** Borrowed immutable bytes: BridgeLink.send copies them into its transport queue. */
    byte[] snapshot(long epoch) {
        if(epoch==0)throw new IllegalArgumentException("Missing light world epoch");
        if(cached!=null&&cachedEpoch==epoch)return cached;
        ByteBuffer output=ByteBuffer.allocate(12+count*20).order(ByteOrder.LITTLE_ENDIAN);
        output.putLong(epoch).putInt(count);
        // Section order and the exporter's y/z/x block order remain stable across
        // rescans, camera movement and different section delivery orders.
        for(List<Lamp> lights:sections.values())for(Lamp lamp:lights)
            output.putFloat(lamp.x).putFloat(lamp.y).putFloat(lamp.z).putFloat(lamp.radius).putInt(lamp.color);
        cachedEpoch=epoch;cached=output.array();return cached;
    }
}
