package dev.goldcraft.bridge;

import java.util.Arrays;
import java.util.LinkedHashMap;
import java.util.Map;

/** Latest CPU pixels for every animated sprite, so queued snapshots can safely coalesce. */
public final class AtlasAnimationState {
    public static final int MAX_BYTES=8*1024*1024, MAX_PATCHES=1024;
    private record Rect(int x,int y,int width,int height) {}
    private final Map<Rect,byte[]> frames=new LinkedHashMap<>();
    private int width,height,bytes;
    private boolean dirty;

    public void reset(int width,int height) {
        this.width=width;this.height=height;frames.clear();bytes=0;dirty=false;
    }
    public boolean dirty(){return dirty;}
    public void sent(){dirty=false;}
    public void update(int x,int y,int w,int h,byte[] rgba) {
        if(x<0||y<0||w<=0||h<=0||w>width||h>height||x>width-w||y>height-h||(long)w*h*4!=rgba.length)
            throw new IllegalArgumentException("Animated atlas rectangle is out of bounds");
        Rect key=new Rect(x,y,w,h);byte[] old=frames.get(key);
        if(Arrays.equals(old,rgba))return;
        if(old==null&&(frames.size()>=MAX_PATCHES||rgba.length>MAX_BYTES-bytes))
            throw new IllegalArgumentException("Animated atlas budget exceeded");
        if(old==null)bytes+=rgba.length;
        frames.put(key,rgba.clone());dirty=true;
    }
    public byte[] snapshot(long world,long generation) {
        Wire.Writer writer=new Wire.Writer().i64(world).i64(generation).i32(frames.size());
        for(var entry:frames.entrySet()) {
            Rect r=entry.getKey();writer.i32(r.x).i32(r.y).i32(r.width).i32(r.height).bytes(entry.getValue());
        }
        return writer.toByteArray();
    }
}
