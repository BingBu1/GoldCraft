import dev.goldcraft.bridge.BridgeLink;
import dev.goldcraft.bridge.Wire;
import dev.goldcraft.bridge.HudPixels;
import dev.goldcraft.bridge.ParticleSnapshot;
import java.io.*;
import java.net.*;
import java.nio.*;
import java.util.*;
import java.util.function.BooleanSupplier;

/** Runs real Java 21/x64 sockets against the built native host, including negative handshakes. */
public final class LinkInteropMain {
    private static int checks;
    private static void check(boolean condition,String message) { checks++; if(!condition) throw new AssertionError(message); }
    private static void until(BooleanSupplier condition,String message) throws Exception {
        long deadline=System.nanoTime()+5_000_000_000L;
        while(!condition.getAsBoolean()&&System.nanoTime()<deadline) Thread.sleep(10);
        check(condition.getAsBoolean(),message);
    }
    private static byte[] receive(InputStream in,int bytes) throws IOException {
        byte[] result=in.readNBytes(bytes); if(result.length!=bytes) throw new EOFException(); return result;
    }
    private static long hello(Socket socket,byte[] session,byte[] token,int role,boolean fragment) throws Exception {
        byte[] payload=new Wire.Writer().bytes(session).bytes(token).i32(role).i32(0).toByteArray();
        byte[] frame=Wire.frame(Wire.HELLO,0,1,payload).array();
        if(fragment) { for(byte b:frame) {socket.getOutputStream().write(b); socket.getOutputStream().flush();} }
        else socket.getOutputStream().write(frame);
        Wire.Header header=Wire.header(ByteBuffer.wrap(receive(socket.getInputStream(),Wire.HEADER_BYTES)));
        check(header.type()==Wire.WELCOME&&header.sequence()==1&&header.epoch()!=0,"native welcome");
        Wire.Reader r=new Wire.Reader(receive(socket.getInputStream(),header.size()));
        check(Arrays.equals(session,r.bytes(16)),"native session identity");
        check(r.i32()==Wire.HOST_CLIENT&&r.i32()==0,"native role/features");
        check(r.f32()==32.0f&&r.f32()==64.0f,"native coordinate constants"); r.finish();
        return header.epoch();
    }
    private static Socket socket(int port) throws IOException {
        Socket s=new Socket(InetAddress.getByName("127.0.0.1"),port); s.setSoTimeout(3000); s.setTcpNoDelay(true); return s;
    }
    private static void rejected(int port,byte[] session,byte[] token,int role) throws Exception {
        try(Socket socket=socket(port)) {
            boolean failed=false;
            try { hello(socket,session,token,role,false); } catch(EOFException | SocketException e) { failed=true; }
            check(failed,"invalid pairing rejected");
        }
        Thread.sleep(30);
    }
    private static byte[] awaitEcho(BridgeLink link) throws Exception {
        long deadline=System.nanoTime()+5_000_000_000L;
        while(System.nanoTime()<deadline) {
            for(Wire.Message message:link.drain()) if(message.type()==Wire.PLAYER_POSE) return message.payload();
            Thread.sleep(5);
        }
        throw new AssertionError("echo timeout: "+link.lastError());
    }
    public static void main(String[] args) throws Exception {
        if(args.length!=1) throw new IllegalArgumentException("native probe executable required");
        Process process=new ProcessBuilder(args[0]).redirectError(ProcessBuilder.Redirect.INHERIT).start();
        try {
            String line=new BufferedReader(new InputStreamReader(process.getInputStream())).readLine();
            check(line!=null&&line.startsWith("READY "),"native probe started");
            String[] parts=line.split(" "); int a=Integer.parseInt(parts[1]),b=Integer.parseInt(parts[2]); check(a!=b,"exclusive ephemeral ports");
            byte[] keyA=Wire.key("00112233445566778899aabbccddeeff"),keyB=Wire.key("ffeeddccbbaa99887766554433221100");
            rejected(b,keyA,keyA,Wire.FABRIC_CLIENT);
            rejected(a,keyA,keyB,Wire.FABRIC_CLIENT);
            rejected(a,keyA,keyA,Wire.FABRIC_SERVER);
            long firstEpoch;
            try(Socket raw=socket(a)) {
                firstEpoch=hello(raw,keyA,keyA,Wire.FABRIC_CLIENT,true);
                byte[] body=new Wire.Writer().i64(0x1122334455667788L).f32(-12.5f).string("fragmented").toByteArray();
                raw.getOutputStream().write(Wire.frame(Wire.INPUT,firstEpoch,2,body).array());
                Wire.Header h=Wire.header(ByteBuffer.wrap(receive(raw.getInputStream(),Wire.HEADER_BYTES)));
                check(h.type()==Wire.PLAYER_POSE&&h.epoch()==firstEpoch,"framed native response");
                check(Arrays.equals(body,receive(raw.getInputStream(),h.size())),"exact C++/Java payload roundtrip");
                raw.getOutputStream().write(Wire.frame(Wire.INPUT,firstEpoch,2,body).array());
                check(raw.getInputStream().read()==-1,"duplicate sequence disconnects");
            }
            Thread.sleep(50);
            try(Socket raw=socket(a)) {
                long epoch=hello(raw,keyA,keyA,Wire.FABRIC_CLIENT,false); check(epoch!=firstEpoch,"reconnect assigns a new epoch");
                raw.getOutputStream().write(Wire.frame(Wire.INPUT,firstEpoch,2,new byte[0]).array());
                check(raw.getInputStream().read()==-1,"stale connection epoch rejected");
            }
            Thread.sleep(50);
            try(BridgeLink linkA=new BridgeLink(new BridgeLink.Config(a,keyA,keyA,Wire.FABRIC_CLIENT,Wire.HOST_CLIENT));
                BridgeLink linkB=new BridgeLink(new BridgeLink.Config(b,keyB,keyB,Wire.FABRIC_CLIENT,Wire.HOST_CLIENT))) {
                until(()->linkA.connected()&&linkB.connected(),"two real client links authenticated");
                byte[] bodyA=new byte[1024*1024+17],bodyB=new byte[65537];
                new Random(100).nextBytes(bodyA); new Random(200).nextBytes(bodyB);
                check(linkA.send(Wire.INPUT,bodyA)&&linkB.send(Wire.INPUT,bodyB),"independent streams enqueued");
                check(Arrays.equals(bodyA,awaitEcho(linkA)),"large fragmented A payload isolation");
                check(Arrays.equals(bodyB,awaitEcho(linkB)),"B payload isolation");
                check(linkA.drain().isEmpty()&&linkB.drain().isEmpty(),"no cross-pair messages");
                for(int mode=0;mode<4;mode++) {
                    int width=mode==0?1:mode==2?1280:512,height=mode==0?1:mode==2?720:129;
                    byte[] rgba=new byte[width*height*4];
                    if(mode==0)rgba=new byte[]{12,34,56,(byte)128};
                    else if(mode==2)new Random(300).nextBytes(rgba);
                    else if(mode==3){new Random(400).nextBytes(rgba);Arrays.fill(rgba,0,32769*4,(byte)0);Arrays.fill(rgba,40000*4,rgba.length,(byte)255);}
                    byte[] encoded=HudPixels.encode(ByteBuffer.wrap(rgba),width,height);
                    check(linkA.send(Wire.HUD_FRAME,new Wire.Writer().i32(width).i32(height).bytes(encoded).toByteArray()),"HUD snapshot enqueued");
                    check(Arrays.equals(rgba,awaitEcho(linkA)),"Java encoded RGBA decoded exactly by real x86 native endpoint");
                }
                Wire.Writer particleVertices=new Wire.Writer();
                for(int v=0;v<4;v++)particleVertices.f32(1).f32(66).f32(-3).f32((v&1)==0?0:1).f32(v<2?0:1).i32(0x80102030);
                byte[] particleMesh=ParticleSnapshot.encode(123,7,10,2,1,List.of(new ParticleSnapshot.Batch(0,1,particleVertices.toByteArray())));
                check(linkA.send(Wire.PARTICLE_MESH,particleMesh),"particle snapshot crosses actual Java/native socket");
                Wire.Reader particle=new Wire.Reader(awaitEcho(linkA));
                check(particle.i64()==123&&particle.i64()==7&&particle.i64()==10&&particle.i32()==2,"native particle identity preserved");
                check(particle.i32()==1&&particle.i32()==1&&particle.i32()==0&&particle.i32()==1&&particle.i32()==4,"particle material and quad count preserved");
                boolean exact=true;
                for(int v=0;v<4;v++)exact&=particle.f32()==32&&particle.f32()==96&&particle.f32()==64&&particle.f32()==((v&1)==0?0:1)&&particle.f32()==(v<2?0:1)&&particle.i32()==0x80102030;
                particle.finish();check(exact,"real x86 particle conversion preserves UV and translucent RGBA");
                byte[] clearParticles=ParticleSnapshot.encode(123,7,11,2,0,List.of());
                check(linkB.send(Wire.PARTICLE_MESH,clearParticles)&&Arrays.equals(clearParticles,awaitEcho(linkB)),"empty B snapshot removes particles independently");
                check(linkA.drain().isEmpty()&&linkB.drain().isEmpty(),"particle frames never cross client pairs");
                Thread.sleep(1100); check(linkA.connected()&&linkB.connected(),"bidirectional keepalive");
            }
            Thread.sleep(100);
            try(BridgeLink reconnect=new BridgeLink(new BridgeLink.Config(a,keyA,keyA,Wire.FABRIC_CLIENT,Wire.HOST_CLIENT))) {
                until(reconnect::connected,"clean Java reconnect");
                check(reconnect.send(Wire.INPUT,new byte[]{5,4,3,2,1}),"reconnect send");
                check(Arrays.equals(new byte[]{5,4,3,2,1},awaitEcho(reconnect)),"reconnect has no stale queued data");
            }
            System.out.println("{\"passed\":true,\"checks\":"+checks+",\"java\":\""+System.getProperty("java.version")+"\",\"native\":\""+args[0].replace('\\','/')+"\"}");
        } finally {
            process.destroy(); if(!process.waitFor(2,java.util.concurrent.TimeUnit.SECONDS)) process.destroyForcibly();
        }
    }
}
