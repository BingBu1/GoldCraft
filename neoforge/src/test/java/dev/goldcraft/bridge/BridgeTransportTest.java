package dev.goldcraft.bridge;

import java.io.InputStream;
import java.io.OutputStream;
import java.net.InetAddress;
import java.net.ServerSocket;
import java.net.Socket;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.List;
import java.util.concurrent.atomic.AtomicReference;
import org.junit.jupiter.api.Test;
import static org.junit.jupiter.api.Assertions.*;

class BridgeTransportTest {
    static final long EPOCH=SharedBulkSenderTest.EPOCH;
    record Frame(Wire.Header header,byte[] payload){}
    static Frame read(InputStream in)throws Exception{
        byte[] head=in.readNBytes(32);assertEquals(32,head.length);var h=Wire.header(ByteBuffer.wrap(head));
        byte[] payload=in.readNBytes(h.size());assertEquals(h.size(),payload.length);return new Frame(h,payload);
    }
    static void write(OutputStream out,int type,long seq,byte[] payload)throws Exception{out.write(Wire.frame(type,EPOCH,seq,payload).array());out.flush();}
    static void welcome(OutputStream out)throws Exception{write(out,Wire.WELCOME,1,new Wire.Writer().bytes(new byte[16]).i32(Wire.HOST_CLIENT).i32(0).f32(Wire.UNITS_PER_BLOCK).f32(Wire.Y_OFFSET).toByteArray());}
    static void await(java.util.function.BooleanSupplier check)throws Exception{long until=System.nanoTime()+5_000_000_000L;while(!check.getAsBoolean()&&System.nanoTime()<until)Thread.sleep(2);assertTrue(check.getAsBoolean());}
    static BridgeLink link(int port){return new BridgeLink(new BridgeLink.Config(port,new byte[16],new byte[16],Wire.FABRIC_CLIENT,Wire.HOST_CLIENT));}
    @Test void actualSocketAckPrecedesReferenceAndFullRingAllowsUrgentReliableOrdering()throws Exception{
        try(var peer=new ServerSocket(0,1,InetAddress.getLoopbackAddress());var link=link(peer.getLocalPort());var socket=peer.accept();var mapping=new SharedBulkSenderTest.NativeMap()){
            socket.setSoTimeout(6000);var input=socket.getInputStream();var output=socket.getOutputStream();
            var hello=read(input);assertEquals(1,ByteBuffer.wrap(hello.payload()).order(ByteOrder.LITTLE_ENDIAN).getInt(36));
            welcome(output);write(output,Wire.BULK_OFFER,2,SharedBulkSenderTest.offer(mapping.name,EPOCH));
            Frame ack=read(input);assertEquals(Wire.BULK_ACK,ack.header().type());assertEquals(1,ByteBuffer.wrap(ack.payload()).order(ByteOrder.LITTLE_ENDIAN).getInt(4));
            await(()->link.metrics().enabled()==1);
            byte[] large=new byte[Wire.MAX_PAYLOAD];large[123]=77;
            for(int i=0;i<2;i++){assertTrue(link.send(Wire.SECTION_MESH,large));Frame ref=read(input);assertEquals(Wire.BULK_REF,ref.header().type());assertEquals(i*Wire.MAX_PAYLOAD,ByteBuffer.wrap(ref.payload()).order(ByteOrder.LITTLE_ENDIAN).getInt(8));}
            assertTrue(link.send(Wire.SECTION_MESH,new byte[65536]));await(()->link.metrics().ringFullWaits()>0);
            assertTrue(link.send(Wire.REMOVE_SECTION,new byte[]{42}));
            assertTrue(link.send(Wire.PLAYER_POSE,new byte[]{11}));assertTrue(link.send(Wire.CAMERA,new byte[]{12}));assertTrue(link.send(Wire.HEARTBEAT,new byte[0]));
            for(int i=0;i<3;i++){int type=read(input).header().type();assertTrue(type==Wire.PLAYER_POSE||type==Wire.CAMERA||type==Wire.HEARTBEAT);}
            assertEquals(SharedBulkSender.CAPACITY,(int)SharedBulkSenderTest.INTS.getAcquire(mapping.bytes,128));
            SharedBulkSenderTest.INTS.setRelease(mapping.bytes,64,Wire.MAX_PAYLOAD);
            assertEquals(Wire.BULK_REF,read(input).header().type());assertEquals(Wire.REMOVE_SECTION,read(input).header().type());
            assertEquals(3,link.metrics().sharedFrames()[Wire.SECTION_MESH]);assertEquals(0,link.metrics().tcpFrames()[Wire.SECTION_MESH]);
            assertEquals(77,mapping.bytes.get(256+Wire.MAX_PAYLOAD+123));
        }
    }
    @Test void unavailableMappingAcknowledgesZeroAndKeepsLargeTcpPayload()throws Exception{
        try(var peer=new ServerSocket(0,1,InetAddress.getLoopbackAddress());var link=link(peer.getLocalPort());var socket=peer.accept()){
            socket.setSoTimeout(6000);var input=socket.getInputStream();var output=socket.getOutputStream();read(input);welcome(output);
            write(output,Wire.BULK_OFFER,2,SharedBulkSenderTest.offer(SharedBulkSenderTest.name(),EPOCH));
            Frame ack=read(input);assertEquals(Wire.BULK_ACK,ack.header().type());assertEquals(0,ByteBuffer.wrap(ack.payload()).order(ByteOrder.LITTLE_ENDIAN).getInt(4));
            byte[] payload=new byte[65536];payload[7]=89;assertTrue(link.send(Wire.ENTITY_TEXTURE,payload));Frame frame=read(input);assertEquals(Wire.ENTITY_TEXTURE,frame.header().type());assertArrayEquals(payload,frame.payload());
            assertEquals(1,link.metrics().fallback());assertEquals(0,link.metrics().enabled());
        }
    }
    @Test void capsZeroNonWindowsIgnoresJnaAndUsesTcpWithoutOffer()throws Exception{
        String previous=System.getProperty("os.name");System.setProperty("os.name","Linux");
        try(var peer=new ServerSocket(0,1,InetAddress.getLoopbackAddress());var link=link(peer.getLocalPort());var socket=peer.accept()){
            socket.setSoTimeout(6000);var input=socket.getInputStream();var output=socket.getOutputStream();Frame hello=read(input);
            assertEquals(0,ByteBuffer.wrap(hello.payload()).order(ByteOrder.LITTLE_ENDIAN).getInt(36));welcome(output);await(link::connected);
            assertTrue(link.send(Wire.ENTITY_TEXTURE,new byte[65536]));assertEquals(Wire.ENTITY_TEXTURE,read(input).header().type());assertEquals(0,link.metrics().offered());
            assertFalse(link.send(Wire.BULK_REF,new byte[16]));
        }finally{System.setProperty("os.name",previous);}
    }
    @Test void unsolicitedAndDuplicateOffersDisconnectInsteadOfReplay()throws Exception{
        for(boolean duplicate:new boolean[]{false,true}){
            try(var peer=new ServerSocket(0,1,InetAddress.getLoopbackAddress());var link=link(peer.getLocalPort());var socket=peer.accept()){
                socket.setSoTimeout(6000);var input=socket.getInputStream();var output=socket.getOutputStream();read(input);welcome(output);
                byte[] offer=SharedBulkSenderTest.offer(SharedBulkSenderTest.name(),duplicate?EPOCH:EPOCH+1);write(output,Wire.BULK_OFFER,2,offer);
                if(duplicate){assertEquals(Wire.BULK_ACK,read(input).header().type());write(output,Wire.BULK_OFFER,3,offer);}
                await(()->!link.lastError().isEmpty());assertFalse(link.connected());assertEquals(0,link.metrics().sharedFrames()[Wire.ENTITY_TEXTURE]);
            }
        }
    }
    @Test void gatherWritesRespectBudgetAcrossHeaderPayloadAndEmptyAck()throws Exception{
        try(var listener=new ServerSocket(0,1,InetAddress.getLoopbackAddress());var sender=java.nio.channels.SocketChannel.open(new java.net.InetSocketAddress(InetAddress.getLoopbackAddress(),listener.getLocalPort()));var receiver=listener.accept()){
            receiver.setSoTimeout(3000);
            for(int size:new int[]{0,8,1000}){
                var pending=new BridgeLink.Pending(size==8?Wire.BULK_ACK:Wire.HEARTBEAT,new byte[size],EPOCH,1,size==8);
                int total=0;
                while(pending.remaining()){
                    int count=pending.write(sender,7);assertTrue(count>0&&count<=7);total+=count;
                    assertEquals(total<Wire.HEADER_BYTES+size,pending.remaining());
                }
                assertEquals(Wire.HEADER_BYTES+size,total);assertEquals(total,receiver.getInputStream().readNBytes(total).length);
            }
        }
    }
    @Test void bulkThresholdLeaves65535OnTcpAndPublishes65536()throws Exception{
        try(var peer=new ServerSocket(0,1,InetAddress.getLoopbackAddress());var link=link(peer.getLocalPort());var socket=peer.accept();var mapping=new SharedBulkSenderTest.NativeMap()){
            socket.setSoTimeout(6000);var input=socket.getInputStream();var output=socket.getOutputStream();read(input);welcome(output);
            write(output,Wire.BULK_OFFER,2,SharedBulkSenderTest.offer(mapping.name,EPOCH));assertEquals(Wire.BULK_ACK,read(input).header().type());await(()->link.metrics().enabled()==1);
            assertTrue(link.send(Wire.ENTITY_TEXTURE,new byte[65535]));assertEquals(Wire.ENTITY_TEXTURE,read(input).header().type());
            assertTrue(link.send(Wire.ENTITY_TEXTURE,new byte[65536]));assertEquals(Wire.BULK_REF,read(input).header().type());
        }
    }
}
