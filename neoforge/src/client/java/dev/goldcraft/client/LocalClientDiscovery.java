package dev.goldcraft.client;

import com.sun.jna.platform.win32.Advapi32;
import com.sun.jna.platform.win32.Advapi32Util;
import com.sun.jna.platform.win32.Kernel32;
import com.sun.jna.platform.win32.WinBase;
import com.sun.jna.platform.win32.WinNT;
import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.channels.FileChannel;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.LinkOption;
import java.nio.file.Path;
import java.nio.file.StandardOpenOption;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.Objects;

/** Same-user discovery only. BridgeLink and the server still authenticate the pair. */
final class LocalClientDiscovery implements AutoCloseable {
    static final int MAX_RECORD_BYTES=512, MAX_RECORDS=128;
    record Endpoint(long pid,long created,int port,String session,String token,long world,boolean busy) {
        boolean sameTransport(Endpoint other) {
            return other!=null&&pid==other.pid&&created==other.created&&port==other.port
                &&session.equals(other.session)&&token.equals(other.token);
        }
        @Override public String toString(){return "Local CS endpoint [credentials redacted]";}
    }
    record Result(Endpoint endpoint,String reason) {}
    interface Identity {
        boolean privatePath(Path path) throws IOException;
        boolean live(Endpoint endpoint);
    }
    private final Path directory;
    private final int protocol;
    private final Identity identity;
    private record Request(long world,Endpoint active,boolean keepTransport) {}
    private volatile Request request=new Request(0,null,false);
    private volatile Result result=new Result(null,"No local CS discovery requested.");
    private volatile boolean running=true;
    private final Thread worker;

    LocalClientDiscovery(int protocol) {
        this(directory(System.getenv()),protocol,new WindowsIdentity());
    }
    LocalClientDiscovery(Path directory,int protocol,Identity identity) {
        this.directory=directory;this.protocol=protocol;this.identity=identity;
        worker=new Thread(this::run,"GoldCraft-local-discovery");worker.setDaemon(true);worker.start();
    }
    static Path directory(Map<String,String> env) {
        String override=env.get("GOLDCRAFT_DISCOVERY_DIR"),local=env.get("LOCALAPPDATA");
        if(override==null&&local==null)throw new IllegalArgumentException("Local CS discovery is unavailable on this platform.");
        Path path=override!=null?Path.of(override):Path.of(local,"GoldCraft-client-links-v1");
        if(!path.isAbsolute()||path.toString().startsWith("\\\\"))throw new IllegalArgumentException("Local CS discovery requires a local absolute directory.");
        return path.normalize();
    }
    void request(long world,Endpoint current){request(world,current,false);}
    void request(long world,Endpoint current,boolean keepTransport){request=new Request(world,current,keepTransport);}
    Result result(){return result;}
    private void run(){
        while(running){
            Request current=request;
            if(current.world!=0){
                Result next=scan(directory,protocol,current.world,current.active,identity,current.keepTransport);
                // Compare values: the game thread refreshes the same request every tick.
                if(current.equals(request))result=next;
            }else result=new Result(null,"Connect to a GoldCraft Minecraft server to select its matching CS client.");
            try{Thread.sleep(250);}catch(InterruptedException ignored){/* close wakes the worker */}
        }
    }
    @Override public void close(){running=false;worker.interrupt();}

    static Result scan(Path directory,int protocol,long world,Endpoint active,Identity identity){
        return scan(directory,protocol,world,active,identity,false);
    }
    static Result scan(Path directory,int protocol,long world,Endpoint active,Identity identity,boolean keepTransport){
        if(world==0)return new Result(null,"The Minecraft server has not requested a CS pair.");
        try{
            if(!identity.privatePath(directory))return new Result(null,"No private local CS discovery directory; start the matching CS client with GoldCraft.");
            List<Endpoint> entries=new ArrayList<>();int count=0;
            try(var files=Files.newDirectoryStream(directory,"*.gclink")){
                for(Path file:files){
                    if(++count>MAX_RECORDS)return new Result(null,"Too many local CS discovery records; close stale clients and retry.");
                    try{
                        if(!identity.privatePath(file)||!Files.isRegularFile(file,LinkOption.NOFOLLOW_LINKS))continue;
                        // The opened file is bounded even if it changes after the metadata check.
                        ByteBuffer bytes=ByteBuffer.allocate(MAX_RECORD_BYTES+1);
                        try(var input=FileChannel.open(file,StandardOpenOption.READ,LinkOption.NOFOLLOW_LINKS)){
                            while(bytes.hasRemaining()&&input.read(bytes)>0){}
                        }
                        if(bytes.position()>MAX_RECORD_BYTES)continue;
                        Endpoint endpoint=parse(new String(bytes.array(),0,bytes.position(),StandardCharsets.US_ASCII),protocol);
                        if(!file.getFileName().toString().equals(endpoint.pid+"-"+endpoint.created+".gclink"))continue;
                        if(identity.live(endpoint))entries.add(endpoint);
                    }catch(IOException|IllegalArgumentException invalid){/* Ignore incomplete, stale or foreign records. */}
                }
            }
            return select(entries,world,active,keepTransport);
        }catch(IOException|RuntimeException invalid){return new Result(null,"Local CS discovery could not validate its private records.");}
    }
    static Endpoint parse(String text,int protocol){
        if(text.length()>MAX_RECORD_BYTES)throw new IllegalArgumentException("Oversize local endpoint");
        String[] fields=text.split("\n",-1);
        if(fields.length!=10||!fields[0].equals("GOLDCRAFT_LOCAL_V1")||!fields[9].isEmpty()||
            !fields[1].equals(Integer.toString(protocol)))throw new IllegalArgumentException("Unsupported local endpoint");
        for(int field:new int[]{2,3,4,7})if(!fields[field].matches("0|[1-9][0-9]{0,19}"))throw new IllegalArgumentException("Invalid local endpoint integer");
        long pid=Long.parseLong(fields[2]),created=Long.parseLong(fields[3]);int port=Integer.parseInt(fields[4]);
        long world=Long.parseUnsignedLong(fields[7]);
        if(pid<=0||pid>0xffffffffL||created<=0||port<1024||port>65535||
            !fields[5].matches("[0-9a-f]{32}")||!fields[6].matches("[0-9a-f]{32}")||
            fields[5].equals("0".repeat(32))||fields[6].equals("0".repeat(32))||
            !(fields[8].equals("0")||fields[8].equals("1")))throw new IllegalArgumentException("Invalid local endpoint");
        return new Endpoint(pid,created,port,fields[5],fields[6],world,fields[8].equals("1"));
    }
    static Result select(List<Endpoint> entries,long world,Endpoint active){
        return select(entries,world,active,false);
    }
    static Result select(List<Endpoint> entries,long world,Endpoint active,boolean keepTransport){
        if(world==0)return new Result(null,"The Minecraft server has not requested a CS pair.");
        // Once admitted, the authenticated bridge survives CS map changes/reconnects.
        // A new server challenge always disables this and requires its exact world.
        if(keepTransport&&active!=null)for(Endpoint e:entries)if(e.sameTransport(active))return new Result(e,"");
        List<Endpoint> matches=entries.stream().filter(e->e.world==world).toList();
        // An authenticated existing pair keeps ownership when another CS instance starts.
        if(active!=null)for(Endpoint e:matches)if(e.sameTransport(active))return new Result(e,"");
        List<Endpoint> free=matches.stream().filter(e->!e.busy).toList();
        if(free.size()==1)return new Result(free.getFirst(),"");
        if(free.size()>1)return new Result(null,"Multiple CS clients joined this server; use a paired GoldCraft launcher to select one explicitly.");
        if(!matches.isEmpty())return new Result(null,"The matching CS client is already paired with another Minecraft instance.");
        return new Result(null,entries.isEmpty()?"No live local CS client with GoldCraft was found.":
            "No local CS client is joined to this Minecraft server's matching CS server.");
    }
    static final class WindowsIdentity implements Identity {
        private final String sid;
        WindowsIdentity(){
            if(!System.getProperty("os.name","").startsWith("Windows"))throw new IllegalArgumentException("Automatic CS discovery requires Windows.");
            sid=processSid(Kernel32.INSTANCE.GetCurrentProcess());
        }
        private static String processSid(WinNT.HANDLE process){
            WinNT.HANDLEByReference token=new WinNT.HANDLEByReference();
            if(!Advapi32.INSTANCE.OpenProcessToken(process,WinNT.TOKEN_QUERY,token))throw new IllegalArgumentException("Cannot inspect local CS owner.");
            try{return Advapi32Util.getTokenAccount(token.getValue()).sidString;}
            finally{Kernel32.INSTANCE.CloseHandle(token.getValue());}
        }
        @Override public boolean privatePath(Path path){
            int drive=Kernel32.INSTANCE.GetDriveType(path.getRoot().toString());
            if(drive!=2&&drive!=3&&drive!=6)return false;
            for(Path current=path;current!=null;current=current.getParent()){
                int attributes=Kernel32.INSTANCE.GetFileAttributes(current.toString());
                if(attributes==-1||(attributes&0x400)!=0)return false;
            }
            var descriptor=Advapi32Util.getFileSecurityDescriptor(path.toFile(),false);
            if(!sid.equals(Advapi32Util.convertSidToStringSid(descriptor.getOwner()))||(descriptor.Control&0x1000)==0)return false;
            var acl=descriptor.getDiscretionaryACL();if(acl==null)return false;
            var aces=acl.getACEs();
            return aces.length==1&&aces[0] instanceof WinNT.ACCESS_ALLOWED_ACE allowed&&
                (allowed.AceFlags&8)==0&&sid.equals(allowed.getSidString())&&(allowed.Mask&0x1f01ff)==0x1f01ff;
        }
        @Override public boolean live(Endpoint endpoint){
            WinNT.HANDLE process=Kernel32.INSTANCE.OpenProcess(0x1000|0x100000,false,(int)endpoint.pid);
            if(process==null)return false;
            try{
                if(Kernel32.INSTANCE.WaitForSingleObject(process,0)!=0x102||!Objects.equals(sid,processSid(process)))return false;
                WinBase.FILETIME created=new WinBase.FILETIME(),exit=new WinBase.FILETIME(),kernel=new WinBase.FILETIME(),user=new WinBase.FILETIME();
                return Kernel32.INSTANCE.GetProcessTimes(process,created,exit,kernel,user)&&created.toDWordLong().longValue()==endpoint.created;
            }catch(RuntimeException denied){return false;}
            finally{Kernel32.INSTANCE.CloseHandle(process);}
        }
    }
}
