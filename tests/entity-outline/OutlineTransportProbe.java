package dev.goldcraft.bridge;

import com.sun.jna.platform.win32.Kernel32;
import com.sun.jna.platform.win32.WinBase;
import java.lang.invoke.*;
import java.nio.*;
import java.util.*;

/** Shipped x64 producer, real named Windows mapping, new and legacy frames. */
public final class OutlineTransportProbe {
    static int checks;
    static void check(boolean v){checks++;if(!v)throw new AssertionError("check "+checks);}
    public static void main(String[] args){
        var kernel=Kernel32.INSTANCE;
        String name="Local\\GoldCraftBulk-"+UUID.randomUUID().toString().replace("-","");
        var handle=kernel.CreateFileMapping(WinBase.INVALID_HANDLE_VALUE,null,4,0,SharedBulkSender.HEADER_BYTES+SharedBulkSender.CAPACITY,name);
        check(handle!=null);
        var pointer=kernel.MapViewOfFile(handle,6,0,0,SharedBulkSender.HEADER_BYTES+SharedBulkSender.CAPACITY);check(pointer!=null);
        var bytes=pointer.getByteBuffer(0,SharedBulkSender.HEADER_BYTES+SharedBulkSender.CAPACITY).order(ByteOrder.LITTLE_ENDIAN);
        VarHandle ints=MethodHandles.byteBufferViewVarHandle(int[].class,ByteOrder.LITTLE_ENDIAN);
        long epoch=42;
        try{
            bytes.putInt(0,SharedBulkSender.MAGIC).putInt(4,1).putInt(8,SharedBulkSender.CAPACITY).putLong(16,epoch);
            for(int type:new int[]{Wire.ENTITY_FRAME,Wire.ENTITY_OUTLINE_FRAME})
                for(int cursor:new int[]{0,SharedBulkSender.CAPACITY-32,-32})for(int size:new int[]{65536,65537}){
                    ints.setRelease(bytes,64,cursor);ints.setRelease(bytes,128,cursor);
                    try(var sender=SharedBulkSender.open(new SharedBulkSender.Offer(epoch,name))){
                        check(sender!=null);byte[] payload=new byte[size];for(int i=0;i<size;i++)payload[i]=(byte)(i*29+type);
                        var frame=sender.frame(type,payload,epoch,2);check(frame!=null);
                        check(Wire.header(frame.slice(0,32)).type()==Wire.BULK_REF);
                        var ref=frame.slice(32,16).order(ByteOrder.LITTLE_ENDIAN);
                        check(ref.getInt()==type&&ref.getInt()==size&&ref.getInt()==cursor&&ref.getInt()==0);
                        check((int)ints.getAcquire(bytes,128)==cursor+SharedBulkSender.aligned(size));
                        int offset=cursor&(SharedBulkSender.CAPACITY-1),first=Math.min(size,SharedBulkSender.CAPACITY-offset);
                        byte[] actual=new byte[size];bytes.get(256+offset,actual,0,first);if(first<size)bytes.get(256,actual,first,size-first);
                        check(Arrays.equals(payload,actual));check(sender.frames()==1&&sender.payloadBytes()==size);
                    }
                }
        }finally{check(kernel.UnmapViewOfFile(pointer));check(kernel.CloseHandle(handle));}
        var old=kernel.OpenFileMapping(6,false,name);if(old!=null)kernel.CloseHandle(old);check(old==null);
        System.out.println("PASS shipped x64 shared sender + Windows mapping checks="+checks);
    }
}
