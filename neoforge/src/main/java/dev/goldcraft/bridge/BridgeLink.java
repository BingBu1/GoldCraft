package dev.goldcraft.bridge;

import java.io.IOException;
import java.io.EOFException;
import java.net.InetAddress;
import java.net.InetSocketAddress;
import java.net.SocketException;
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

    record Queued(int type, byte[] payload, long queuedAt) {}
    public record Metrics(long[] tcpFrames, long[] tcpBytes, long[] sharedFrames, long[] sharedBytes,
                          long offered, long enabled, long fallback, long ringFullWaits, long ringHighWater,
                          int queuedBytes, long queueHighWater, long replacements, long rejections,
                          long queueAgeNanos, long peakQueueAgeNanos, long pendingAgeNanos, long peakPendingAgeNanos,
                          long encodeNanos, long queueCopyNanos, long sharedCopyNanos,
                          long socketWriteNanos, long socketBytes, long drainNanos, int blockedBytes, int pendingBytes, boolean sharedActive) {}
    private static final byte[] EMPTY = new byte[0];
    static final int URGENT_RESERVED_BYTES=4096, URGENT_RESERVED_SLOTS=8;
    private final Config config;
    private final Thread worker;
    private final ArrayDeque<Queued> outgoing = new ArrayDeque<>();
    /** Metadata belongs to the authenticated connection that received this
     * message. A frozen batch never changes identity when the worker reconnects. */
    public record Received(Wire.Message message, long generation, long epoch,
                           long sequence, long receivedAtNanos) {}
    public record IncomingBatch(boolean connected, long generation, long epoch,
                                List<Received> messages) {}
    private final ArrayDeque<Received> incoming = new ArrayDeque<>();
    private long connectionEpoch, provenClockFrequency;
    private int acceptedCapabilities;
    private int outboundBytes, inboundBytes;
    private volatile boolean running = true, connected;
    private volatile long generation;
    private volatile String lastError = "";
    private volatile SocketChannel channel;
    private final long[] tcpFrames = new long[Wire.TYPE_COUNT], tcpBytes = new long[Wire.TYPE_COUNT], sharedFrames = new long[Wire.TYPE_COUNT], sharedBytes = new long[Wire.TYPE_COUNT];
    private long offered, enabled, fallback, ringFullWaits, ringHighWater, queueHighWater, replacements, rejections;
    private long peakQueueAgeNanos, peakPendingAgeNanos, encodeNanos, queueCopyNanos, sharedCopyNanos, socketWriteNanos, socketBytes, drainNanos;
    private long pendingSince;
    private int blockedBytes, pendingBytes;
    private boolean sharedActive;

    public BridgeLink(Config config) {
        this.config=config;
        worker=new Thread(this::run,"GoldCraft-bridge-"+config.role()+"-"+config.port());
        worker.setDaemon(true); worker.start();
    }
    public boolean connected() { return connected; }
    public long generation() { return generation; }
    public String lastError() { return lastError; }
    public synchronized long localQpcFrequency(long expectedGeneration) {
        return connected && expectedGeneration != 0 && generation == expectedGeneration ? provenClockFrequency : 0;
    }
    public synchronized boolean visibility(long expectedGeneration) {
        return connected && config.role()==Wire.FABRIC_SERVER && config.hostRole()==Wire.HOST_SERVER &&
            generation==expectedGeneration && expectedGeneration!=0 && (acceptedCapabilities & Wire.STATUS_VISIBILITY_CAP)!=0;
    }
    public synchronized boolean sendVisibility(long expectedGeneration,byte[] payload) {
        return visibility(expectedGeneration)&&sendForGeneration(expectedGeneration,Wire.STATUS_VISIBILITY,payload);
    }
    public synchronized boolean motion(long expectedGeneration) {
        return connected() && config.role()==Wire.FABRIC_SERVER && config.hostRole()==Wire.HOST_SERVER &&
            generation==expectedGeneration && expectedGeneration!=0 && (acceptedCapabilities & Wire.STATUS_MOTION_CAP)!=0;
    }
    public synchronized boolean sendMotion(long expectedGeneration,byte[] payload) {
        return motion(expectedGeneration)&&sendForGeneration(expectedGeneration,Wire.STATUS_MOTION,payload);
    }
    public synchronized boolean environment(long expectedGeneration) {
        return config.role()==Wire.FABRIC_SERVER && config.hostRole()==Wire.HOST_SERVER && connected &&
            generation==expectedGeneration && expectedGeneration!=0 && (acceptedCapabilities & Wire.STATUS_ENVIRONMENT_CAP)!=0;
    }
    public synchronized boolean sendEnvironment(long expectedGeneration,byte[] payload){
        return environment(expectedGeneration)&&sendForGeneration(expectedGeneration,Wire.STATUS_ENVIRONMENT,payload);
    }
    public synchronized boolean statusAttributes(long expectedGeneration) {
        return config.role()==Wire.FABRIC_SERVER && config.hostRole()==Wire.HOST_SERVER && connected &&
            generation==expectedGeneration && expectedGeneration!=0 && (acceptedCapabilities & Wire.STATUS_ATTRIBUTES_CAP)!=0;
    }
    public synchronized boolean sendAttributes(long expectedGeneration,int type,byte[] payload){
        return statusAttributes(expectedGeneration)&&sendForGeneration(expectedGeneration,type,payload);
    }
    public synchronized boolean nativeArmor(long expectedGeneration) {
        return statusAttributes(expectedGeneration) && (acceptedCapabilities & NativeArmorProtocol.CAPABILITY)!=0;
    }
    public synchronized boolean fluids(long expectedGeneration) {
        return config.role()==Wire.FABRIC_SERVER && config.hostRole()==Wire.HOST_SERVER && connected &&
            generation==expectedGeneration && expectedGeneration!=0 && (acceptedCapabilities & Wire.FLUID_CAPABILITY)!=0;
    }
    public synchronized boolean sendFluids(long expectedGeneration,byte[] payload){
        return fluids(expectedGeneration)&&sendForGeneration(expectedGeneration,Wire.FLUID_VOLUMES,payload);
    }
    public synchronized boolean statusEffects(long expectedGeneration) {
        return config.role()==Wire.FABRIC_SERVER && config.hostRole()==Wire.HOST_SERVER && connected &&
            generation==expectedGeneration && expectedGeneration!=0 && (acceptedCapabilities & Wire.STATUS_EFFECTS)!=0;
    }
    public synchronized boolean gameplayRules(long expectedGeneration) {
        return config.role()==Wire.FABRIC_SERVER && config.hostRole()==Wire.HOST_SERVER && connected &&
            generation==expectedGeneration && expectedGeneration!=0 && (acceptedCapabilities & Wire.GAMEPLAY_RULES_CAP)!=0;
    }
    public synchronized boolean hostExplosions(long expectedGeneration) {
        return config.role()==Wire.FABRIC_SERVER && config.hostRole()==Wire.HOST_SERVER && connected &&
            generation==expectedGeneration && expectedGeneration!=0 && (acceptedCapabilities & Wire.HOST_EXPLOSIONS_CAP)!=0;
    }
    public synchronized boolean playerGameplay(long expectedGeneration) {
        return config.role()==Wire.FABRIC_SERVER && config.hostRole()==Wire.HOST_SERVER && connected &&
            generation==expectedGeneration && expectedGeneration!=0 && (acceptedCapabilities & Wire.PLAYER_GAMEPLAY_CAP)!=0;
    }
    public synchronized boolean sendPlayerGameplay(long expectedGeneration,int type,byte[] payload) {
        return playerGameplay(expectedGeneration)&&(type==Wire.PLAYER_GAMEPLAY||type==Wire.PLAYER_ATTACK)&&sendForGeneration(expectedGeneration,type,payload);
    }
    public synchronized boolean tntDamage(long expectedGeneration) {
        return config.role()==Wire.FABRIC_SERVER && config.hostRole()==Wire.HOST_SERVER && connected &&
            generation==expectedGeneration && expectedGeneration!=0 && (acceptedCapabilities & Wire.TNT_DAMAGE_CAP)!=0;
    }
    public synchronized boolean sendTntDamage(long expectedGeneration,byte[] payload) {
        return tntDamage(expectedGeneration)&&sendForGeneration(expectedGeneration,Wire.DAMAGE_REQUEST,payload);
    }
    public synchronized boolean sendStatus(long expectedGeneration,int type,byte[] payload){
        return statusEffects(expectedGeneration)&&sendForGeneration(expectedGeneration,type,payload);
    }
    public synchronized boolean effectPositionHold(long expectedGeneration) {
        return connected && expectedGeneration != 0 && generation == expectedGeneration &&
            (acceptedCapabilities & Wire.EFFECT_POSITION_HOLD) != 0;
    }
    public int port() { return config.port(); }

    public synchronized Metrics metrics() {
        long now = System.nanoTime(), age = 0;
        for (Queued message : outgoing) age = Math.max(age, now - message.queuedAt());
        peakQueueAgeNanos = Math.max(peakQueueAgeNanos, age);
        long pendingAge = pendingSince == 0 ? 0 : now - pendingSince;
        peakPendingAgeNanos = Math.max(peakPendingAgeNanos, pendingAge);
        return new Metrics(tcpFrames.clone(), tcpBytes.clone(), sharedFrames.clone(), sharedBytes.clone(),
            offered, enabled, fallback, ringFullWaits, ringHighWater, outboundBytes, queueHighWater,
            replacements, rejections, age, peakQueueAgeNanos, pendingAge, peakPendingAgeNanos,
            encodeNanos, queueCopyNanos, sharedCopyNanos, socketWriteNanos, socketBytes, drainNanos, blockedBytes, pendingBytes, sharedActive);
    }

    /** Reject queued work derived from a previous authenticated connection. */
    public synchronized boolean sendForGeneration(long expectedGeneration, int type, byte[] payload) {
        if (!connected || expectedGeneration==0 || expectedGeneration!=generation) return false;
        return send(type,payload);
    }
    public synchronized boolean send(int type, byte[] payload) {
        if (!connected || !Wire.knownType(type) || Wire.internalType(type) || type==Wire.HELLO || type==Wire.WELCOME || payload.length>Wire.MAX_PAYLOAD) { rejections++; return false; }
        int bytes=payload.length+Wire.HEADER_BYTES;
        boolean replace=type==Wire.PLAYER_POSE||type==Wire.CAMERA||type==Wire.BLOCK_FEEDBACK||type==Wire.ENTITY_MESH||type==Wire.ENTITY_FRAME||type==Wire.PARTICLE_MESH||type==Wire.POST_EFFECT_PLAN||type==Wire.LIGHTS||type==Wire.ATLAS||type==Wire.ATLAS_PATCHES||type==Wire.HUD_FRAME;
        int replacedBytes=0,replacedCount=0;
        if(replace)for(var old:outgoing)if(old.type()==type){replacedBytes+=old.payload().length+Wire.HEADER_BYTES;replacedCount++;}
        boolean reserve=config.role()==Wire.FABRIC_CLIENT&&config.hostRole()==Wire.HOST_CLIENT&&!urgent(type);
        int byteLimit=Wire.MAX_QUEUED-(reserve?URGENT_RESERVED_BYTES:0);
        int entryLimit=4096-(reserve?URGENT_RESERVED_SLOTS:0);
        if (bytes>byteLimit-(outboundBytes-replacedBytes) || outgoing.size()-replacedCount>=entryLimit) { rejections++; return false; }
        // Allocate the complete replacement before removing any previously accepted frame.
        long start = System.nanoTime();
        Queued message=new Queued(type,payload.clone(),start);
        queueCopyNanos += System.nanoTime() - start;
        if(replace) {
            var iterator=outgoing.iterator();
            while(iterator.hasNext()){Queued old=iterator.next();if(old.type()==type){outboundBytes-=old.payload().length+Wire.HEADER_BYTES;iterator.remove();}}
            replacements += replacedCount;
        }
        if(type==Wire.PLAYER_POSE||type==Wire.CAMERA)outgoing.addFirst(message);else outgoing.addLast(message);
        outboundBytes+=bytes; queueHighWater = Math.max(queueHighWater, outboundBytes); return true;
    }
    // Compatibility API for existing consumers. New authority state uses the
    // atomic context API below instead of separately sampling generation().
    public synchronized List<Wire.Message> drain() {
        long start = System.nanoTime();
        List<Wire.Message> messages=new ArrayList<>(incoming.size());
        for (Received received : incoming) messages.add(received.message());
        incoming.clear(); inboundBytes=0;
        drainNanos += System.nanoTime() - start; return messages;
    }
    public synchronized IncomingBatch drainWithContext() {
        long start=System.nanoTime();
        IncomingBatch batch=new IncomingBatch(connected,generation,connectionEpoch,List.copyOf(incoming));
        incoming.clear();inboundBytes=0;drainNanos+=System.nanoTime()-start;
        return batch;
    }
    private synchronized void authenticated(long epoch,int capabilities) throws IOException {
        if (!running || connected || generation == -1L) throw new IOException("Invalid authority generation transition");
        connectionEpoch=epoch;acceptedCapabilities=capabilities;generation++;connected=true;lastError="";
    }
    private synchronized Queued next(boolean urgentOnly) {
        Queued message = null;
        if (urgentOnly) {
            var iterator = outgoing.iterator();
            while (iterator.hasNext()) {
                Queued candidate = iterator.next();
                if (urgent(candidate.type())) { message = candidate; iterator.remove(); break; }
            }
        } else message = outgoing.pollFirst();
        if (message!=null) {
            outboundBytes-=message.payload().length+Wire.HEADER_BYTES;
            peakQueueAgeNanos = Math.max(peakQueueAgeNanos, System.nanoTime() - message.queuedAt());
        }
        return message;
    }
    private static boolean urgent(int type) { return type == Wire.PLAYER_POSE || type == Wire.CAMERA || type == Wire.HEARTBEAT; }
    private synchronized void receive(Wire.Header header, byte[] payload) throws IOException {
        if (!running || !connected || header.epoch()!=connectionEpoch) throw new IOException("Retired incoming authority");
        if (payload.length>Wire.MAX_QUEUED-inboundBytes || incoming.size()>=4096) throw new IOException("Unconsumed message limit");
        incoming.addLast(new Received(new Wire.Message(header.type(),payload),generation,
            header.epoch(),header.sequence(),System.nanoTime()));
        inboundBytes+=payload.length;
    }
    private synchronized void reset() {
        connected=false; connectionEpoch=0; provenClockFrequency=0; acceptedCapabilities=0; outgoing.clear(); incoming.clear(); inboundBytes=outboundBytes=0; pendingSince=0; blockedBytes=pendingBytes=0; sharedActive=false;
    }
    private static final class HandshakeAttempt {
        boolean helloSent, received;
    }
    private void run() {
        int offerLevel=5;
        while (running) {
            long previousGeneration=generation;
            var attempt=new HandshakeAttempt();
            try (SocketChannel socket=SocketChannel.open()) {
                channel=socket;
                socket.socket().connect(new InetSocketAddress(InetAddress.getByName("127.0.0.1"),config.port()),2000);
                socket.socket().setTcpNoDelay(true);
                socket.configureBlocking(false);
                exchange(socket,offerLevel>=1,offerLevel>=2,offerLevel>=3,offerLevel>=4,offerLevel>=5,attempt);
            } catch (IOException | IllegalArgumentException e) {
                if (running) lastError=e.getClass().getSimpleName()+": "+e.getMessage();
                // Protocol30 hosts predating TNT reject unknown HELLO bits.
                // Retry their original capability set with identical credentials;
                // never send kind6 without an authenticated accepted capability.
                if(generation!=previousGeneration)offerLevel=5;
                else if(attempt.helloSent&&!attempt.received&&(e instanceof EOFException||e instanceof SocketException))
                    offerLevel=offerLevel==0?5:offerLevel-1;
            }
            finally { channel=null; reset(); }
            if (running) sleep(500);
        }
    }
    static final class Pending {
        final ByteBuffer[] buffers;
        final int type, size;
        final boolean bulk, ack;
        Pending(int type, byte[] payload, long epoch, long sequence, boolean ack) {
            this.type=type; size=payload.length; bulk=false; this.ack=ack;
            buffers = new ByteBuffer[]{Wire.frameHeader(type, epoch, sequence, size), ByteBuffer.wrap(payload)};
        }
        Pending(Queued message) {
            type=message.type(); size=message.payload().length; bulk=true; ack=false;
            // Construct this holder before publishing the ring reservation.
            buffers = new ByteBuffer[1];
        }
        boolean remaining() { for(ByteBuffer buffer:buffers)if(buffer.hasRemaining())return true;return false; }
        int write(SocketChannel socket,int budget)throws IOException {
            if(budget<=0)return 0;
            int firstLimit=buffers[0].limit(),lastLimit=buffers[buffers.length-1].limit(),available=budget;
            try {
                for(ByteBuffer buffer:buffers){int count=Math.min(available,buffer.remaining());buffer.limit(buffer.position()+count);available-=count;}
                return (int)socket.write(buffers);
            } finally {
                buffers[0].limit(firstLimit);buffers[buffers.length-1].limit(lastLimit);
            }
        }
    }
    private void exchange(SocketChannel socket,boolean offerTnt,boolean offerGameplay,boolean offerEnvironment,boolean offerMotion,boolean offerVisibility,HandshakeAttempt attempt) throws IOException {
        ByteBuffer input=ByteBuffer.allocate(Wire.MAX_PAYLOAD+Wire.HEADER_BYTES);
        boolean statusCapable=config.role()==Wire.FABRIC_SERVER&&config.hostRole()==Wire.HOST_SERVER;
        boolean capable=SharedBulkSender.supported(config.role(), config.hostRole());
        boolean clockCapable=LocalQpcProof.supported(config.role(), config.hostRole());
        byte[] hello=new Wire.Writer().bytes(config.session()).bytes(config.token()).i32(config.role()).i32((capable?Wire.SHARED_BULK_V1:0)|(clockCapable?Wire.LOCAL_QPC_V1|Wire.EFFECT_POSITION_HOLD:0)|(statusCapable?Wire.STATUS_EFFECTS|Wire.STATUS_ATTRIBUTES_CAP|Wire.FLUID_CAPABILITY|NativeArmorProtocol.CAPABILITY|Wire.GAMEPLAY_RULES_CAP | Wire.HOST_EXPLOSIONS_CAP | (offerTnt?Wire.TNT_DAMAGE_CAP:0)|(offerGameplay?Wire.PLAYER_GAMEPLAY_CAP:0)|(offerEnvironment?Wire.STATUS_ENVIRONMENT_CAP:0)|(offerMotion?Wire.STATUS_MOTION_CAP:0)|(offerVisibility?Wire.STATUS_VISIBILITY_CAP:0):0)).toByteArray();
        Pending pending=new Pending(Wire.HELLO,hello,0,1,false);
        Queued blocked=null;
        SharedBulkSender shared=null;
        boolean offerSeen=false, bulkEnabled=false;
        byte[] ack=null, clockAck=null;
        LocalQpcProof.Response clockResponse=null;
        boolean clockOfferSeen=false, clockAckSent=false, clockReadySeen=false;
        long epoch=0, receivedSequence=0, sentSequence=1, recordedCopyNanos=0;
        long lastReceive=System.nanoTime(), lastHeartbeat=lastReceive;
        try {
            while (running) {
                int writeBudget=4*1024*1024;
                for(int writes=0;writes<64&&writeBudget>0;writes++) {
                    if(pending==null) {
                        if(!connected)break;
                        long encodeStart=System.nanoTime();
                        if(clockAck!=null) {
                            pending=new Pending(Wire.CLOCK_ACK,clockAck,epoch,++sentSequence,false); clockAck=null;
                        } else if(ack!=null) {
                            pending=new Pending(Wire.BULK_ACK,ack,epoch,++sentSequence,true); ack=null;
                        } else {
                            Queued message=blocked!=null?blocked:next(false);
                            if(message==null)break;
                            boolean bulk=bulkEnabled&&Wire.bulkType(message.type())&&message.payload().length>=SharedBulkSender.MIN_PAYLOAD;
                            if(bulk&&!shared.canPublish(message.payload().length)) {
                                blocked=message;
                                synchronized(this) { if(pendingSince==0)pendingSince=message.queuedAt(); ringFullWaits++; blockedBytes=message.payload().length; }
                                message=next(true); if(message==null)break;
                                bulk=false;
                            } else if(message==blocked) {blocked=null;synchronized(this){blockedBytes=0;}}
                            if(bulk) {
                                pending=new Pending(message);
                                pending.buffers[0]=shared.frame(message.type(),message.payload(),epoch,++sentSequence);
                            } else pending=new Pending(message.type(),message.payload(),epoch,++sentSequence,false);
                            synchronized(this) { if(pendingSince==0)pendingSince=message.queuedAt(); }
                        }
                        synchronized(this) { encodeNanos+=System.nanoTime()-encodeStart; pendingBytes=pending.bulk?Wire.HEADER_BYTES+16:pending.size+Wire.HEADER_BYTES; }
                    }
                    long writeStart=System.nanoTime();
                    int count=pending.write(socket,writeBudget);writeBudget-=count;
                    synchronized(this) { socketWriteNanos+=System.nanoTime()-writeStart; socketBytes+=count; }
                    if(pending.remaining()||count==0)break;
                    synchronized(this) {
                        if(pending.bulk) {sharedFrames[pending.type]++; sharedBytes[pending.type]+=pending.size;}
                        else {tcpFrames[pending.type]++; tcpBytes[pending.type]+=pending.size;}
                        if(shared!=null) {ringHighWater=Math.max(ringHighWater,shared.highWater());sharedCopyNanos+=shared.copyNanos()-recordedCopyNanos;recordedCopyNanos=shared.copyNanos();}
                        if(pendingSince!=0)peakPendingAgeNanos=Math.max(peakPendingAgeNanos,System.nanoTime()-pendingSince);
                        if(blocked==null)pendingSince=0;
                        pendingBytes=0;
                    }
                    // A complete ACK must be on TCP before any bulk reference can be constructed.
                    if(pending.type==Wire.HELLO)attempt.helloSent=true;
                    if(pending.type==Wire.CLOCK_ACK)clockAckSent=true;
                    if(pending.ack&&shared!=null) {bulkEnabled=true; synchronized(this){enabled++;sharedActive=true;}}
                    pending=null;
                }
                int read=socket.read(input);
                if (read<0) throw new EOFException("Host disconnected");
                if (read>0) attempt.received=true;
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
                        if (!Arrays.equals(config.session(),r.bytes(16)) || r.i32()!=config.hostRole())
                            throw new IOException("Host identity mismatch");
                        int accepted=r.i32();
                        if((accepted&Wire.STATUS_VISIBILITY_CAP)!=0&&!offerVisibility)throw new IOException("Unrequested visibility capability");
                        if((accepted&Wire.STATUS_MOTION_CAP)!=0&&!offerMotion)throw new IOException("Unrequested motion capability");
                        if((accepted&Wire.STATUS_ENVIRONMENT_CAP)!=0&&!offerEnvironment)throw new IOException("Unrequested environment capability");
                        if((accepted&Wire.PLAYER_GAMEPLAY_CAP)!=0&&!offerGameplay)throw new IOException("Unrequested player gameplay capability");
                        if((accepted&Wire.TNT_DAMAGE_CAP)!=0&&!offerTnt)throw new IOException("Unrequested TNT capability");
                        if (((accepted&NativeArmorProtocol.CAPABILITY)!=0&&(accepted&Wire.STATUS_ATTRIBUTES_CAP)==0) || (accepted & ~(Wire.EFFECT_POSITION_HOLD|Wire.STATUS_EFFECTS|Wire.STATUS_ATTRIBUTES_CAP|Wire.FLUID_CAPABILITY|NativeArmorProtocol.CAPABILITY|Wire.GAMEPLAY_RULES_CAP | Wire.HOST_EXPLOSIONS_CAP | Wire.TNT_DAMAGE_CAP | Wire.PLAYER_GAMEPLAY_CAP | Wire.STATUS_ENVIRONMENT_CAP | Wire.STATUS_MOTION_CAP | Wire.STATUS_VISIBILITY_CAP))!=0 || ((accepted&Wire.EFFECT_POSITION_HOLD)!=0&&!clockCapable) || ((accepted&(Wire.STATUS_EFFECTS|Wire.STATUS_ATTRIBUTES_CAP|Wire.FLUID_CAPABILITY|NativeArmorProtocol.CAPABILITY|Wire.GAMEPLAY_RULES_CAP | Wire.HOST_EXPLOSIONS_CAP | Wire.TNT_DAMAGE_CAP | Wire.PLAYER_GAMEPLAY_CAP | Wire.STATUS_ENVIRONMENT_CAP | Wire.STATUS_MOTION_CAP | Wire.STATUS_VISIBILITY_CAP))!=0&&!statusCapable) ||
                            r.f32()!=Wire.UNITS_PER_BLOCK || r.f32()!=Wire.Y_OFFSET)
                            throw new IOException("Host capability/coordinate contract mismatch");
                        r.finish(); epoch=header.epoch(); authenticated(epoch,accepted);
                    } else {
                        if (header.epoch()!=epoch || header.type()==Wire.HELLO || header.type()==Wire.WELCOME) throw new IOException("Stale epoch/repeated handshake");
                        if (header.type()==Wire.CLOCK_OFFER) {
                            if(!clockCapable||clockOfferSeen||clockReadySeen)throw new IOException("Unsolicited/repeated clock offer");
                            clockOfferSeen=true;
                            clockResponse=LocalQpcProof.respond(payload,epoch);
                            clockAck=clockResponse.ack();
                        } else if(header.type()==Wire.CLOCK_READY) {
                            if(!clockCapable||clockReadySeen||(clockOfferSeen&&!clockAckSent))
                                throw new IOException("Unsolicited/repeated clock ready");
                            Wire.Reader reply=new Wire.Reader(payload);
                            int version=reply.i32(), accepted=reply.i32();
                            long proofEpoch=reply.i64(), frequency=reply.i64(); reply.finish();
                            if(version!=LocalQpcProof.LAYOUT||accepted<0||accepted>1||proofEpoch!=epoch||
                               (accepted==0&&frequency!=0)||
                               (accepted==1&&(!clockAckSent||clockResponse==null||!clockResponse.accepted()||
                                   frequency<=0||frequency!=clockResponse.frequency())))
                                throw new IOException("Invalid local clock ready");
                            if(accepted==1) {
                                var local=LocalQpcClock.system().read();
                                if(local==null||local.frequency()!=frequency)throw new IOException("Local clock unavailable after proof");
                            }
                            synchronized(this) {
                                if(!running||!connected||connectionEpoch!=epoch)throw new IOException("Retired clock authority");
                                provenClockFrequency=accepted==1?frequency:0;
                            }
                            clockReadySeen=true;
                        } else if (header.type()==Wire.BULK_OFFER) {
                            if(!capable||offerSeen)throw new IOException("Unsolicited/repeated bulk offer");
                            offerSeen=true;
                            var offer=SharedBulkSender.Offer.parse(payload,epoch);
                            synchronized(this){offered++;}
                            shared=SharedBulkSender.open(offer);
                            ack=new Wire.Writer().i32(SharedBulkSender.LAYOUT).i32(shared==null?0:1).toByteArray();
                            if(shared==null)synchronized(this){fallback++;}
                        } else if(Wire.internalType(header.type()))throw new IOException("Unexpected bulk control");
                        else if (header.type()==Wire.HEARTBEAT) { if (payload.length!=0) throw new IOException("Invalid heartbeat"); }
                        else receive(header,payload);
                    }
                    receivedSequence=header.sequence(); lastReceive=System.nanoTime();
                }
                input.compact();
                long now=System.nanoTime();
                if (now-lastReceive>(connected?10_000_000_000L:3_000_000_000L)) throw new IOException("Host timeout");
                if (connected && now-lastHeartbeat>1_000_000_000L) { send(Wire.HEARTBEAT,EMPTY); lastHeartbeat=now; }
                sleep(2);
            }
        } finally { if(shared!=null)shared.close(); }
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
