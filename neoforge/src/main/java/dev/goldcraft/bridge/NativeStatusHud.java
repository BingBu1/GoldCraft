package dev.goldcraft.bridge;

import java.util.List;

/** Registry IDs stay ASCII so stock GoldSrc clients need no Minecraft font/assets. */
public final class NativeStatusHud {
    public record Entry(String id,int amplifier,int duration){}
    public static byte[] encode(long epoch,long revision,int slot,int serial,int spawn,List<Entry> entries){
        if(epoch==0||revision==0||slot<1||slot>64||serial==0||spawn==0||entries.size()>32)
            throw new IllegalArgumentException("Invalid native status HUD identity/count");
        var out=new Wire.Writer().i64(epoch).i64(revision).i32(slot).i32(serial).i32(spawn).i32(entries.size());
        int size=32;var ids=new java.util.HashSet<String>();
        for(var e:entries){
            if(e.id()==null||e.id().length()>96||!e.id().matches("[a-z0-9_.-]+:[a-z0-9_./-]+")
                ||!ids.add(e.id())||e.amplifier()<0||e.amplifier()>255||e.duration()<-1)
                throw new IllegalArgumentException("Invalid native status HUD entry");
            size+=10+e.id().length();if(size>4096)throw new IllegalArgumentException("Native status HUD size");
            out.string(e.id()).i32(e.amplifier()).i32(e.duration());
        }
        return out.toByteArray();
    }
}
