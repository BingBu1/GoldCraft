package dev.goldcraft.bridge;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.Arrays;
import java.util.Random;
import org.junit.jupiter.api.Test;
import static org.junit.jupiter.api.Assertions.*;

class HudPixelsTest {
    /** Frozen previous encoder: segmentation and byte-at-a-time output are independent of the new writer. */
    private static byte[] legacy(ByteBuffer source,int width,int height){
        if(width<1||height<1||width>4096||height>4096||(long)width*height>8_388_608||source.remaining()!=width*height*4)
            throw new IllegalArgumentException("HUD pixel bounds");
        var pixels=source.slice().order(ByteOrder.LITTLE_ENDIAN).asIntBuffer();
        Wire.Writer out=new Wire.Writer();int cursor=0,count=pixels.remaining();
        while(cursor<count){
            int run=1,pixel=pixels.get(cursor);
            while(run<32768&&cursor+run<count&&pixels.get(cursor+run)==pixel)run++;
            if(run>=3){out.u16(0x8000|(run-1)).i32(pixel);cursor+=run;}
            else{
                int start=cursor;cursor+=run;
                while(cursor<count&&cursor-start<32768){
                    if(cursor+2<count&&pixels.get(cursor)==pixels.get(cursor+1)&&pixels.get(cursor)==pixels.get(cursor+2))break;
                    cursor++;
                }
                out.u16(cursor-start-1);
                for(int i=start;i<cursor;i++)out.i32(pixels.get(i));
            }
        }
        return out.toByteArray();
    }
    private static byte[] bytes(ByteBuffer source){
        byte[] result=new byte[source.remaining()];source.duplicate().get(result);return result;
    }
    private static byte[] rgba(int[] pixels){
        ByteBuffer result=ByteBuffer.allocate(pixels.length*4).order(ByteOrder.LITTLE_ENDIAN);
        for(int pixel:pixels)result.putInt(pixel);
        return result.array();
    }
    private static int[] noise(int count){
        int[] pixels=new int[count];for(int i=0;i<count;i++)pixels[i]=0x81abcdef+i*0x9e3779b9;return pixels;
    }
    /** Independent wire decoder checks every channel, including transparent RGB and alpha. */
    private static byte[] decode(byte[] encoded,int count){
        ByteBuffer packets=ByteBuffer.wrap(encoded).order(ByteOrder.LITTLE_ENDIAN);
        ByteBuffer rgba=ByteBuffer.allocate(count*4).order(ByteOrder.LITTLE_ENDIAN);
        while(packets.hasRemaining()){
            int header=Short.toUnsignedInt(packets.getShort()),length=(header&0x7fff)+1;
            assertTrue(length<=rgba.remaining()/4,"Packet exceeds remaining pixels");
            if((header&0x8000)!=0){int pixel=packets.getInt();for(int i=0;i<length;i++)rgba.putInt(pixel);}
            else for(int i=0;i<length;i++)rgba.putInt(packets.getInt());
        }
        assertEquals(0,rgba.remaining());return rgba.array();
    }
    private static byte[] equivalent(ByteBuffer source,int width,int height){
        int position=source.position(),limit=source.limit();ByteOrder order=source.order();
        byte[] original=bytes(source);source.mark();
        byte[] encoded=HudPixels.encode(source,width,height);
        assertEquals(position,source.position());assertEquals(limit,source.limit());assertEquals(order,source.order());
        source.reset();assertEquals(position,source.position());
        assertArrayEquals(legacy(source,width,height),encoded);
        assertArrayEquals(original,decode(encoded,width*height));assertArrayEquals(original,bytes(source));
        return encoded;
    }
    @Test void emptyBackgroundHasBoundedIndependentRuns() {
        byte[] encoded=HudPixels.encode(ByteBuffer.wrap(new byte[512*129*4]),512,129);
        Wire.Reader r=new Wire.Reader(encoded);
        assertEquals(0xffff,r.u16());assertEquals(0,r.i32());
        assertEquals(0xffff,r.u16());assertEquals(0,r.i32());
        assertEquals(0x81ff,r.u16());assertEquals(0,r.i32());r.finish();
    }
    @Test void onePixelPreservesChannelsAndBufferPosition() {
        var data=ByteBuffer.wrap(new byte[]{99,1,2,3,(byte)128});data.position(1);
        assertArrayEquals(new byte[]{0,0,1,2,3,(byte)128},HudPixels.encode(data,1,1));assertEquals(1,data.position());
    }
    @Test void rejectsInvalidSizesBeforeAllocationOrIteration() {
        assertThrows(IllegalArgumentException.class,()->HudPixels.encode(ByteBuffer.allocate(0),Integer.MAX_VALUE,1024));
        assertThrows(IllegalArgumentException.class,()->HudPixels.encode(ByteBuffer.allocate(3),1,1));
        assertThrows(IllegalArgumentException.class,()->HudPixels.encode(ByteBuffer.allocate(0),0,1));
    }
    @Test void heapDirectReadOnlyAndNestedOffsetsPreserveEveryByteAndBufferState(){
        int[] pixels=noise(1024);
        Arrays.fill(pixels,20,130,0);Arrays.fill(pixels,256,513,0x80ffffff);Arrays.fill(pixels,701,704,0x00030201);
        byte[] image=rgba(pixels);
        for(boolean direct:new boolean[]{false,true})for(boolean readOnly:new boolean[]{false,true})
            for(ByteOrder order:new ByteOrder[]{ByteOrder.BIG_ENDIAN,ByteOrder.LITTLE_ENDIAN}){
                ByteBuffer storage=direct?ByteBuffer.allocateDirect(image.length+17):ByteBuffer.allocate(image.length+17);
                for(int i=0;i<storage.capacity();i++)storage.put(i,(byte)0x5a);
                storage.position(7).put(image);
                byte[] before=bytes(storage.duplicate().clear());
                storage.position(3).limit(7+image.length);
                ByteBuffer source=storage.slice();source.position(4);
                if(readOnly)source=source.asReadOnlyBuffer();
                equivalent(source.order(order),256,4);
                assertArrayEquals(before,bytes(storage.duplicate().clear()));
            }
    }
    @Test void shortRunsAndTransparentPixelValuesKeepLegacyPackets(){
        for(int count:new int[]{1,2,3,4}){
            int[] pixels=new int[count];Arrays.fill(pixels,0x00030201);
            byte[] result=equivalent(ByteBuffer.wrap(rgba(pixels)),count,1);
            int header=Short.toUnsignedInt(ByteBuffer.wrap(result).order(ByteOrder.LITTLE_ENDIAN).getShort());
            assertEquals((count>=3?0x8000:0)|(count-1),header);
        }
    }
    @Test void repeatedRunsAcross32768RemainByteIdentical(){
        for(int count:new int[]{32767,32768,32769}){
            int[] pixels=noise(65536);Arrays.fill(pixels,0,count,0x89abcdef);
            byte[] result=equivalent(ByteBuffer.wrap(rgba(pixels)),256,256);
            assertEquals(0x8000|(Math.min(count,32768)-1),Short.toUnsignedInt(ByteBuffer.wrap(result).order(ByteOrder.LITTLE_ENDIAN).getShort()));
        }
    }
    @Test void literalsBeforeRunsCrossThe32768BoundaryWithoutLosingPixels(){
        for(int count:new int[]{32767,32768,32769}){
            int[] pixels=noise(65536);Arrays.fill(pixels,count,count+3,0x12345678);
            byte[] result=equivalent(ByteBuffer.wrap(rgba(pixels)),256,256);
            assertEquals(Math.min(count,32768)-1,Short.toUnsignedInt(ByteBuffer.wrap(result).order(ByteOrder.LITTLE_ENDIAN).getShort()));
        }
    }
    @Test void noisyFramesAndAlternatingRunsMatchOldEncodingAndIndependentDecoder(){
        Random random=new Random(0x47434855);
        for(int iteration=0;iteration<24;iteration++){
            int[] pixels=new int[1+random.nextInt(4096)];
            for(int at=0;at<pixels.length;){
                int pixel=random.nextInt(),length=Math.min(1+random.nextInt(17),pixels.length-at);
                if(iteration%2==0)length=Math.min(1+random.nextInt(2),pixels.length-at);
                Arrays.fill(pixels,at,at+length,pixel);at+=length;
            }
            equivalent(ByteBuffer.wrap(rgba(pixels)),pixels.length,1);
        }
    }
    @Test void returnedPacketOwnsItsStorage(){
        ByteBuffer source=ByteBuffer.wrap(rgba(noise(512)));byte[] first=equivalent(source,512,1),expected=first.clone();
        byte[] second=HudPixels.encode(source,512,1);Arrays.fill(second,(byte)0);
        source.put(0,(byte)99);assertArrayEquals(expected,first);
    }
    @Test void maximumPixelBudgetStillEncodesAndDecodes(){
        equivalent(ByteBuffer.wrap(new byte[4096*2048*4]),4096,2048);
    }
    @Test void outOfRangeDimensionsAndMismatchedRemainingKeepValidation(){
        for(int[] dimensions:new int[][]{{-1,1},{1,-1},{4097,1},{1,4097},{4096,2049},{Integer.MAX_VALUE,Integer.MAX_VALUE}}){
            ByteBuffer source=ByteBuffer.allocate(8);source.position(3).mark();
            assertEquals(assertThrows(IllegalArgumentException.class,()->legacy(source,dimensions[0],dimensions[1])).getMessage(),
                assertThrows(IllegalArgumentException.class,()->HudPixels.encode(source,dimensions[0],dimensions[1])).getMessage());
            assertEquals(3,source.position());assertEquals(8,source.limit());source.reset();
        }
        assertThrows(IllegalArgumentException.class,()->HudPixels.encode(ByteBuffer.allocate(8),1,1));
    }
}
