package dev.goldcraft.bridge;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;

/** Lossless, independently decodable RGBA snapshots; no deltas can be lost by queue coalescing. */
public final class HudPixels {
    private HudPixels() {}
    public static byte[] encode(ByteBuffer source,int width,int height) {
        if(width<1||height<1||width>4096||height>4096||(long)width*height>8_388_608||source.remaining()!=width*height*4)
            throw new IllegalArgumentException("HUD pixel bounds");
        var pixels=source.slice().order(ByteOrder.LITTLE_ENDIAN).asIntBuffer();
        Wire.Writer out=new Wire.Writer();int cursor=0,count=pixels.remaining();
        while(cursor<count) {
            int run=1,pixel=pixels.get(cursor);
            while(run<32768&&cursor+run<count&&pixels.get(cursor+run)==pixel)run++;
            if(run>=3) {out.u16(0x8000|(run-1)).i32(pixel);cursor+=run;}
            else {
                int start=cursor;cursor+=run;
                while(cursor<count&&cursor-start<32768) {
                    if(cursor+2<count&&pixels.get(cursor)==pixels.get(cursor+1)&&pixels.get(cursor)==pixels.get(cursor+2))break;
                    cursor++;
                }
                out.u16(cursor-start-1);
                for(int i=start;i<cursor;i++)out.i32(pixels.get(i));
            }
        }
        return out.toByteArray();
    }
}
