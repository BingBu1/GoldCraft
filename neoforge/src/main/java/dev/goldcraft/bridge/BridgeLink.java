package dev.goldcraft.bridge;

import java.io.IOException;
import java.net.InetAddress;
import java.net.InetSocketAddress;
import java.nio.ByteBuffer;
import java.nio.channels.SocketChannel;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Map;
import java.util.Optional;

/** One worker per configured instance. Game objects are accessed only by the game thread. */
public final class BridgeLink implements AutoCloseable {
    public record Config(int port, byte[] session, byte[] token, int role, int hostRole) {
        public Config {
            if (port < 1024 || port > 65535 || session.length != 16 || token.length != 16
                || !((role == Wire.FABRIC_CLIENT && hostRole == Wire.HOST_CLIENT) || (role == Wire.FABRIC_SERVER && hostRole == Wire.HOST_SERVER))) {
                throw new IllegalArgumentException("Invalid endpoint configuration");
            }
            session = session.clone(); token = token.clone();
        }
        public static Optional<Config> environment(String prefix, int role, int hostRole) {
            Map<String,String> env = System.getenv();
            String port=env.get(prefix+"_PORT"), session=env.get(prefix+"_SESSION"), token=env.get(prefix+"_TOKEN");
            if (port==null && session==null && token==null) return Optional.empty();
            if (port==null || session==null || token==null) throw new IllegalArgumentException("Incomplete "+prefix+" environment");
            return Optional.of(new Config(Integer.parseInt(port),Wire.key(session),Wire.key(token),role,hostRole));
        }
    }

    private final Config config;
    private final Thread worker;
    private final ArrayDeque<Wire.Message> outgoing = new ArrayDeque<>(), incoming = new ArrayDeque<>();
    private int outboundBytes, inboundBytes;
    private volatile boolean running = true, connected;
    private volatile long generation;
    private volatile String lastError = "";
    private volatile SocketChannel channel;

    public BridgeLink(Config config) {
        this.config=config;
        worker=new Thread(this::run,"GoldCraft-bridge-"+config.role()+"-"+config.port());
        worker.setDaemon(true); worker.start();
    }
    public boolean connected() { return connected; }
    public long generation() { return generation; }
    public String lastError() { return lastError; }
    public int port() { return config.port(); }

    public synchronized boolean send(int type, byte[] payload) {
        if (!connected || !Wire.knownType(type) || type==Wire.HELLO || type==Wire.WELCOME || payload.length>Wire.MAX_PAYLOAD) return false;
        int bytes=payload.length+Wire.HEADER_BYTES;
        if(type==Wire.PLAYER_POSE||type==Wire.CAMERA||type==Wire.BLOCK_FEEDBACK||type==Wire.ENTITY_MESH||type==Wire.PARTICLE_MESH||type==Wire.LIGHTS||type==Wire.ATLAS||type==Wire.ATLAS_PATCHES||type==Wire.HUD_FRAME) {
            var iterator=outgoing.iterator();
            while(iterator.hasNext()){Wire.Message old=iterator.next();if(old.type()==type){outboundBytes-=old.payload().length+Wire.HEADER_BYTES;iterator.remove();}}
        }
        if (bytes>Wire.MAX_QUEUED-outboundBytes || outgoing.size()>=4096) return false;
        Wire.Message message=new Wire.Message(type,payload.clone());
        // Position/input acknowledgements must not sit behind unsent texture/mesh batches.
        if(type==Wire.PLAYER_POSE||type==Wire.CAMERA)outgoing.addFirst(message);else outgoing.addLast(message);
        outboundBytes+=bytes; return true;
    }
    public synchronized List<Wire.Message> drain() {
        List<Wire.Message> messages=new ArrayList<>(incoming); incoming.clear(); inboundBytes=0; return messages;
    }
    private synchronized Wire.Message next() {
        Wire.Message message=outgoing.pollFirst();
        if (message!=null) outboundBytes-=message.payload().length+Wire.HEADER_BYTES;
        return message;
    }
    private synchronized void receive(Wire.Message message) throws IOException {
        if (message.payload().length>Wire.MAX_QUEUED-inboundBytes || incoming.size()>=4096) throw new IOException("Unconsumed message limit");
        incoming.addLast(message); inboundBytes+=message.payload().length;
    }
    private synchronized void reset() {
        connected=false; outgoing.clear(); incoming.clear(); inboundBytes=outboundBytes=0;
    }
    private void run() {
        while (running) {
            try (SocketChannel socket=SocketChannel.open()) {
                channel=socket;
                socket.socket().connect(new InetSocketAddress(InetAddress.getByName("127.0.0.1"),config.port()),2000);
                socket.socket().setTcpNoDelay(true);
                socket.configureBlocking(false);
                exchange(socket);
            } catch (IOException | IllegalArgumentException e) { if (running) lastError=e.getClass().getSimpleName()+": "+e.getMessage(); }
            finally { channel=null; reset(); }
            if (running) sleep(500);
        }
    }
    private void exchange(SocketChannel socket) throws IOException {
        ByteBuffer input=ByteBuffer.allocate(Wire.MAX_PAYLOAD+Wire.HEADER_BYTES);
        byte[] hello=new Wire.Writer().bytes(config.session()).bytes(config.token()).i32(config.role()).i32(0).toByteArray();
        ByteBuffer pending=Wire.frame(Wire.HELLO,0,1,hello);
        long epoch=0, receivedSequence=0, sentSequence=1;
        long lastReceive=System.nanoTime(), lastHeartbeat=lastReceive;
        while (running) {
            int writeBudget=4*1024*1024;
            for(int writes=0;writes<64&&writeBudget>0;writes++) {
                if(pending==null) {
                    if(!connected)break;
                    Wire.Message message=next();if(message==null)break;
                    pending=Wire.frame(message.type(),epoch,++sentSequence,message.payload());
                }
                int count=socket.write(pending);writeBudget-=count;
                if(pending.hasRemaining()||count==0)break;
                pending=null;
            }
            int read=socket.read(input);
            if (read<0) throw new IOException("Host disconnected");
            input.flip();
            while (input.remaining()>=Wire.HEADER_BYTES) {
                int position=input.position();
                Wire.Header header=Wire.header(input.slice(position,Wire.HEADER_BYTES));
                if (input.remaining()<Wire.HEADER_BYTES+header.size()) break;
                input.position(position+Wire.HEADER_BYTES);
                byte[] payload=new byte[header.size()]; input.get(payload);
                if (header.sequence()!=receivedSequence+1) throw new IOException("Duplicate/skipped host sequence");
                if (!connected) {
                    if (header.type()!=Wire.WELCOME || header.epoch()==0) throw new IOException("Welcome required");
                    Wire.Reader r=new Wire.Reader(payload);
                    if (!Arrays.equals(config.session(),r.bytes(16)) || r.i32()!=config.hostRole() || r.i32()!=0
                        || r.f32()!=Wire.UNITS_PER_BLOCK || r.f32()!=Wire.Y_OFFSET) throw new IOException("Host identity/coordinate contract mismatch");
                    r.finish(); epoch=header.epoch(); generation++; connected=true; lastError="";
                } else {
                    if (header.epoch()!=epoch || header.type()==Wire.HELLO || header.type()==Wire.WELCOME) throw new IOException("Stale epoch/repeated handshake");
                    if (header.type()==Wire.HEARTBEAT) { if (payload.length!=0) throw new IOException("Invalid heartbeat"); }
                    else receive(new Wire.Message(header.type(),payload));
                }
                receivedSequence=header.sequence(); lastReceive=System.nanoTime();
            }
            input.compact();
            long now=System.nanoTime();
            if (now-lastReceive>(connected?10_000_000_000L:3_000_000_000L)) throw new IOException("Host timeout");
            if (connected && now-lastHeartbeat>1_000_000_000L) { send(Wire.HEARTBEAT,new byte[0]); lastHeartbeat=now; }
            sleep(2);
        }
    }
    private void sleep(long millis) {
        try { Thread.sleep(millis); } catch (InterruptedException ignored) { /* close() interrupts the worker */ }
    }
    @Override public void close() {
        running=false; reset(); worker.interrupt();
        SocketChannel socket=channel;
        if (socket!=null) try { socket.close(); } catch (IOException ignored) {}
        try { worker.join(2500); } catch (InterruptedException e) { Thread.currentThread().interrupt(); }
    }
}
