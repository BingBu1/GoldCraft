package dev.goldcraft.healthprobe;

import dev.goldcraft.GoldCraft;
import dev.goldcraft.bridge.BridgeLink;
import dev.goldcraft.bridge.Wire;
import java.net.InetAddress;
import java.net.ServerSocket;
import java.net.Socket;
import java.nio.ByteBuffer;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.LinkedBlockingQueue;
import java.util.concurrent.TimeUnit;

/** Real BridgeLink transport with a loopback host fixture, not a ReHLDS server. */
final class HealthBridgeFixture implements AutoCloseable {
    private static final byte[] KEY=new byte[16];
    final BridgeLink link;
    final ServerSocket listener;
    final Socket peer;
    final Thread reader;
    final ConcurrentHashMap<Integer,LinkedBlockingQueue<Wire.Message>> messages=new ConcurrentHashMap<>();
    volatile boolean closing;
    volatile Throwable failure;
    private long sequence=1;

    HealthBridgeFixture()throws Exception {
        this(Wire.STATUS_HEALTH_CAP|Wire.STATUS_EFFECTS);
    }
    HealthBridgeFixture(int accepted)throws Exception {
        // Production BridgeLink explicitly connects to IPv4 loopback.
        listener=new ServerSocket(0,4,InetAddress.getByName("127.0.0.1"));listener.setSoTimeout(4000);
        link=new BridgeLink(new BridgeLink.Config(listener.getLocalPort(),KEY,KEY,Wire.FABRIC_SERVER,Wire.HOST_SERVER));
        Socket acceptedPeer;
        try{acceptedPeer=listener.accept();}
        catch(Exception e){String error=link.lastError();link.close();listener.close();throw new AssertionError("fixture accept: "+error,e);}
        peer=acceptedPeer;peer.setSoTimeout(4000);
        var hello=read();if(hello.type()!=Wire.HELLO)throw new AssertionError("missing real HELLO");
        var h=new Wire.Reader(hello.payload());h.bytes(16);h.bytes(16);h.i32();int caps=h.i32();h.finish();
        if((caps&accepted)!=accepted)throw new AssertionError("missing requested fixture capability: "+accepted);
        write(Wire.WELCOME,new Wire.Writer().bytes(KEY).i32(Wire.HOST_SERVER).i32(accepted)
            .f32(Wire.UNITS_PER_BLOCK).f32(Wire.Y_OFFSET).toByteArray());
        long deadline=System.nanoTime()+TimeUnit.SECONDS.toNanos(4);
        while(!link.connected()){
            if(System.nanoTime()>deadline)throw new AssertionError("health handshake timeout: "+link.lastError());
            Thread.sleep(5);
        }
        authority(true);
        reader=new Thread(()->{
            try{while(!closing){var m=read();
                if(m.type()==Wire.HEARTBEAT)write(Wire.HEARTBEAT,new byte[0]);
                else messages.computeIfAbsent(m.type(),unused->new LinkedBlockingQueue<>()).add(m);
            }}catch(Throwable t){if(!closing)failure=t;}
        },"health-fixture-peer");reader.setDaemon(true);reader.start();
    }
    private Wire.Message read()throws Exception {
        var in=peer.getInputStream();var header=in.readNBytes(Wire.HEADER_BYTES);
        if(header.length!=Wire.HEADER_BYTES)throw new java.io.EOFException();
        var h=Wire.header(ByteBuffer.wrap(header));var data=in.readNBytes(h.size());
        if(data.length!=h.size())throw new java.io.EOFException();return new Wire.Message(h.type(),data);
    }
    private synchronized void write(int type,byte[] data)throws Exception {
        var out=peer.getOutputStream();out.write(Wire.frameHeader(type,41,sequence++,data.length).array());out.write(data);out.flush();
    }
    void send(int type,byte[] data)throws Exception {write(type,data);}
    Wire.Message take(int type)throws Exception {
        var m=messages.computeIfAbsent(type,unused->new LinkedBlockingQueue<>()).poll(4,TimeUnit.SECONDS);
        if(m==null)throw new AssertionError("missing outbound message "+type+"; "+failure+"; "+link.lastError());
        return m;
    }
    void authority(boolean enabled)throws Exception {
        for(String name:new String[]{"serverLink","authorityLink"}){
            var f=GoldCraft.class.getDeclaredField(name);f.setAccessible(true);f.set(null,enabled?link:null);
        }
        var f=GoldCraft.class.getDeclaredField("generation");f.setAccessible(true);f.setLong(null,enabled?link.generation():0);
    }
    @Override public void close()throws Exception {
        closing=true;authority(false);peer.close();link.close();listener.close();reader.join(2000);
    }
}
