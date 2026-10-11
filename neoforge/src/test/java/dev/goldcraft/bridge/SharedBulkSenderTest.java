package dev.goldcraft.bridge;

import com.sun.jna.Pointer;
import com.sun.jna.platform.win32.Kernel32;
import com.sun.jna.platform.win32.WinBase;
import com.sun.jna.platform.win32.WinNT;
import java.lang.invoke.MethodHandles;
import java.lang.invoke.VarHandle;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.UUID;
import org.junit.jupiter.api.Test;
import static org.junit.jupiter.api.Assertions.*;

class SharedBulkSenderTest {
    static final long EPOCH=0xFEDCBA9876543210L;
    static final VarHandle INTS=MethodHandles.byteBufferViewVarHandle(int[].class,ByteOrder.LITTLE_ENDIAN);
    static String name(){return "Local\\GoldCraftBulk-"+UUID.randomUUID().toString().replace("-","");}
    static byte[] offer(String name,long epoch){return new Wire.Writer().i32(1).i32(SharedBulkSender.CAPACITY).i64(epoch).string(name).toByteArray();}
    static class Fixture implements SharedBulkSender.Mapping {
        final ByteBuffer bytes=ByteBuffer.allocateDirect(SharedBulkSender.HEADER_BYTES+SharedBulkSender.CAPACITY).order(ByteOrder.LITTLE_ENDIAN);
        boolean closed;
        Fixture(int cursor){init(bytes,cursor);}
        public ByteBuffer buffer(){return bytes;}
        public void close(){closed=true;}
    }
    static void init(ByteBuffer b,int cursor){b.order(ByteOrder.LITTLE_ENDIAN).putInt(0,SharedBulkSender.MAGIC).putInt(4,1).putInt(8,SharedBulkSender.CAPACITY).putLong(16,EPOCH);INTS.setRelease(b,64,cursor);INTS.setRelease(b,128,cursor);}
    static class NativeMap implements AutoCloseable {
        final String name=name();
        final WinNT.HANDLE handle=Kernel32.INSTANCE.CreateFileMapping(WinBase.INVALID_HANDLE_VALUE,null,4,0,SharedBulkSender.HEADER_BYTES+SharedBulkSender.CAPACITY,name);
        final Pointer pointer=Kernel32.INSTANCE.MapViewOfFile(handle,6,0,0,SharedBulkSender.HEADER_BYTES+SharedBulkSender.CAPACITY);
        final ByteBuffer bytes=pointer.getByteBuffer(0,SharedBulkSender.HEADER_BYTES+SharedBulkSender.CAPACITY).order(ByteOrder.LITTLE_ENDIAN);
        NativeMap(){assertNotNull(handle);assertNotNull(pointer);init(bytes,0);}
        public void close(){assertTrue(Kernel32.INSTANCE.UnmapViewOfFile(pointer));assertTrue(Kernel32.INSTANCE.CloseHandle(handle));}
    }
    @Test void strictOfferParserRejectsEveryContractViolation(){
        String name=name();assertEquals(name,SharedBulkSender.Offer.parse(offer(name,EPOCH),EPOCH).name());
        for(String invalid:new String[]{name+"x",name.toUpperCase(),name.replace("\\","\\\\"),"Global"+name.substring(5),"Local\\GoldCraftBulk-../test","Local\\GoldCraftBulk-"+"a".repeat(31)})
            assertThrows(IllegalArgumentException.class,()->SharedBulkSender.Offer.parse(offer(invalid,EPOCH),EPOCH));
        byte[] valid=offer(name,EPOCH);
        for(int offset:new int[]{0,4,8,16,18}){byte[] bad=valid.clone();bad[offset]^=0x80;assertThrows(IllegalArgumentException.class,()->SharedBulkSender.Offer.parse(bad,EPOCH));}
        assertThrows(IllegalArgumentException.class,()->SharedBulkSender.Offer.parse(valid,EPOCH+1));
        assertThrows(IllegalArgumentException.class,()->SharedBulkSender.Offer.parse(java.util.Arrays.copyOf(valid,valid.length+1),EPOCH));
        for(int length=0;length<valid.length;length++){byte[] shortOffer=java.util.Arrays.copyOf(valid,length);assertThrows(IllegalArgumentException.class,()->SharedBulkSender.Offer.parse(shortOffer,EPOCH));}
    }
    @Test void actualJnaNamedPagefileMappingIsDirectAndReleaseVisible(){
        assertEquals("5.14.0 (b0)",com.sun.jna.Native.class.getPackage().getImplementationVersion());assertEquals(8,com.sun.jna.Native.POINTER_SIZE);
        try(var owner=new NativeMap();var sender=SharedBulkSender.open(SharedBulkSender.Offer.parse(offer(owner.name,EPOCH),EPOCH))){
            assertNotNull(sender);byte[] payload=new byte[65537];for(int i=0;i<payload.length;i++)payload[i]=(byte)(i*29);
            ByteBuffer reference=sender.frame(Wire.ENTITY_FRAME,payload,EPOCH,2);assertNotNull(reference);
            assertEquals(65544,(int)INTS.getAcquire(owner.bytes,128));
            byte[] actual=new byte[payload.length];owner.bytes.get(256,actual);assertArrayEquals(payload,actual);
            assertEquals(Wire.BULK_REF,Wire.header(reference.slice(0,32)).type());
            assertEquals(0,owner.bytes.getInt(64));assertEquals(1,sender.frames());
        }
        assertNull(SharedBulkSender.open(new SharedBulkSender.Offer(EPOCH,name())));
    }
    @Test void ringWrapRolloverAndUnreadPayloadCannotBeOverwritten(){
        for(int initial:new int[]{SharedBulkSender.CAPACITY-8,-8}){
            var mapping=new Fixture(initial);try(var sender=new SharedBulkSender(mapping,EPOCH)){
                byte[] payload=new byte[Wire.MAX_PAYLOAD];for(int i=0;i<payload.length;i++)payload[i]=(byte)(i*13+7);
                assertNotNull(sender.frame(Wire.SECTION_MESH,payload,EPOCH,2));
                int offset=initial&(SharedBulkSender.CAPACITY-1);
                for(int i=0;i<payload.length;i++)assertEquals(payload[i],mapping.bytes.get(256+((offset+i)&(SharedBulkSender.CAPACITY-1))));
                assertNotNull(sender.frame(Wire.ENTITY_TEXTURE,payload,EPOCH,3));
                int published=(int)INTS.getAcquire(mapping.bytes,128);assertEquals(initial+SharedBulkSender.CAPACITY,published);
                assertNull(sender.frame(Wire.ENTITY_TEXTURE,new byte[65536],EPOCH,4));assertEquals(published,(int)INTS.getAcquire(mapping.bytes,128));
                INTS.setRelease(mapping.bytes,64,initial+Wire.MAX_PAYLOAD);
                assertNotNull(sender.frame(Wire.SECTION_MESH,payload,EPOCH,4));assertEquals(SharedBulkSender.CAPACITY,sender.highWater());
            }assertTrue(mapping.closed);
        }
    }
    @Test void malformedHeadersAndCursorCorruptionAreFatal(){
        for(int offset:new int[]{0,4,8,12,16,24,63,68,127,132,255}){
            var map=new Fixture(0);map.bytes.put(offset,(byte)(map.bytes.get(offset)^1));
            assertThrows(IllegalArgumentException.class,()->new SharedBulkSender(map,EPOCH));
        }
        var map=new Fixture(0);try(var sender=new SharedBulkSender(map,EPOCH)){
            for(int size:new int[]{0,65535,Wire.MAX_PAYLOAD+1})assertThrows(IllegalArgumentException.class,()->sender.canPublish(size));
            assertThrows(IllegalArgumentException.class,()->sender.frame(Wire.CAMERA,new byte[65536],EPOCH,2));
            INTS.setRelease(map.bytes,64,8);assertThrows(IllegalArgumentException.class,()->sender.canPublish(65536));
            assertEquals(0,(int)INTS.getAcquire(map.bytes,128));
        }
    }
    @Test void serverAndNonWindowsNeverAdvertiseSharedMemory(){
        assertFalse(SharedBulkSender.supported(Wire.FABRIC_SERVER,Wire.HOST_SERVER));
        String os=System.getProperty("os.name");try{System.setProperty("os.name","Linux");assertFalse(SharedBulkSender.supported(Wire.FABRIC_CLIENT,Wire.HOST_CLIENT));}finally{System.setProperty("os.name",os);}
    }
    @Test void closingBothMappingHandlesRemovesEachKernelObject(){
        for(int i=0;i<32;i++){
            String name;
            try(var owner=new NativeMap();var sender=SharedBulkSender.open(new SharedBulkSender.Offer(EPOCH,owner.name))){name=owner.name;assertNotNull(sender);}
            var reopened=Kernel32.INSTANCE.OpenFileMapping(6,false,name);
            if(reopened!=null)Kernel32.INSTANCE.CloseHandle(reopened);
            assertNull(reopened,"Mapping object survived both owners closing");
        }
    }
}
