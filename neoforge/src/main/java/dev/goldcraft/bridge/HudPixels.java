package dev.goldcraft.bridge;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.Arrays;

/** Lossless, independently decodable RGBA snapshots; no deltas can be lost by queue coalescing. */
public final class HudPixels {
    private HudPixels() {}
    public static byte[] encode(ByteBuffer source,int width,int height) {
        if(width<1||height<1||width>4096||height>4096||(long)width*height>8_388_608||source.remaining()!=width*height*4)
            throw new IllegalArgumentException("HUD pixel bounds");
        var rgba=source.slice().order(ByteOrder.LITTLE_ENDIAN);
        var pixels=rgba.asIntBuffer();
        byte[] out=new byte[Math.min(4096,rgba.remaining()+2)];
        int cursor=0,count=pixels.remaining(),written=0;
        while(cursor<count) {
            int run=1,pixel=pixels.get(cursor);
            while(run<32768&&cursor+run<count&&pixels.get(cursor+run)==pixel)run++;
            boolean repeated=run>=3;int start=cursor;
            if(repeated)cursor+=run;
            else {
                cursor+=run;
                while(cursor<count&&cursor-start<32768) {
                    if(cursor+2<count&&pixels.get(cursor)==pixels.get(cursor+1)&&pixels.get(cursor)==pixels.get(cursor+2))break;
                    cursor++;
                }
            }
            int length=cursor-start,payloadBytes=repeated?4:length*4,required=written+2+payloadBytes;
            // Six bytes per pixel bounds even one-pixel literals; no buffer survives a frame.
            if(required>out.length)out=Arrays.copyOf(out,Math.max(required,Math.min(out.length*2,count*6)));
            int header=(repeated?0x8000:0)|(length-1);
            out[written++]=(byte)header;out[written++]=(byte)(header>>>8);
            if(repeated){
                out[written]=(byte)pixel;out[written+1]=(byte)(pixel>>>8);
                out[written+2]=(byte)(pixel>>>16);out[written+3]=(byte)(pixel>>>24);
            }else{
                // Preserve the actual RGBA bytes for heap, mapped, direct and read-only views.
                // Absolute bulk get leaves the source's position, limit and mark untouched.
                rgba.get(start*4,out,written,payloadBytes);
            }
            written+=payloadBytes;
        }
        return Arrays.copyOf(out,written);
    }
}
