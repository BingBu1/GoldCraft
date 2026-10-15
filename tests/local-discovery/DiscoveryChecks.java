package dev.goldcraft.client;

import com.sun.jna.platform.win32.Kernel32;
import com.sun.jna.platform.win32.WinBase;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;
import java.util.Map;

public final class DiscoveryChecks {
    static int checks;
    static void check(boolean value,String label){checks++;if(!value)throw new AssertionError(label);}
    static String record(long pid,long created,int port,long world,boolean busy){
        return "GOLDCRAFT_LOCAL_V1\n30\n"+pid+"\n"+created+"\n"+port+"\n"+"a".repeat(32)+"\n"+"b".repeat(32)+"\n"+Long.toUnsignedString(world)+"\n"+(busy?"1":"0")+"\n";
    }
    static LocalClientDiscovery.Endpoint endpoint(long pid,long world,boolean busy){return LocalClientDiscovery.parse(record(pid,1000+pid,40000+(int)pid,world,busy),30);}
    static void rejected(String text,String label){try{LocalClientDiscovery.parse(text,30);throw new AssertionError(label);}catch(IllegalArgumentException expected){checks++;}}
    static boolean await(java.util.function.BooleanSupplier condition)throws InterruptedException{
        long deadline=System.nanoTime()+2_000_000_000L;
        while(!condition.getAsBoolean()&&System.nanoTime()-deadline<0)Thread.sleep(20);
        return condition.getAsBoolean();
    }
    public static void main(String[] args)throws Exception{
        Path root=Path.of(args[0]),privateRoot=root.resolve("private"),scanRoot=root.resolve("scan");
        var a=endpoint(1,99,false);var b=endpoint(2,99,false);var other=endpoint(3,100,false);var busy=endpoint(1,99,true);
        check(LocalClientDiscovery.select(List.of(a,other),99,null).endpoint().equals(a),"exact server world");
        check(LocalClientDiscovery.select(List.of(a,b),99,null).endpoint()==null,"ambiguous free clients");
        check(LocalClientDiscovery.select(List.of(busy),99,null).endpoint()==null,"occupied endpoint cannot be stolen");
        check(LocalClientDiscovery.select(List.of(busy),99,a).endpoint().equals(busy),"current pair keeps ownership");
        check(LocalClientDiscovery.select(List.of(busy,b),99,null).endpoint().equals(b),"unique available endpoint");
        check(LocalClientDiscovery.select(List.of(a,b),99,a).endpoint().equals(a),"second client does not steal active pair");
        check(LocalClientDiscovery.select(List.of(other),99,null).endpoint()==null,"wrong server");
        check(LocalClientDiscovery.select(List.of(a),0,null).endpoint()==null,"no challenge no automatic attach");
        check(LocalClientDiscovery.select(List.of(endpoint(1,100,true)),99,a,true).endpoint()!=null,"admitted transport survives map change");
        check(LocalClientDiscovery.select(List.of(endpoint(1,0,true)),99,a,true).endpoint()!=null,"admitted transport survives CS reconnect transition");
        check(LocalClientDiscovery.select(List.of(endpoint(1,100,true)),99,a,false).endpoint()==null,"new admission never retains wrong world");
        check(LocalClientDiscovery.select(List.of(endpoint(1,100,true)),99,null,true).endpoint()==null,"retention cannot select unowned transport");
        check(LocalClientDiscovery.select(List.of(endpoint(1,0,false)),99,null).endpoint()==null,"disconnected native world");
        check(!a.toString().contains(a.token())&&!a.toString().contains(a.session()),"secrets redacted");
        check(LocalClientDiscovery.parse(record(1,1001,40001,-1L,false),30).world()==-1L,"unsigned epoch interoperability");
        String good=record(1,1001,40001,99,false);
        rejected(good.replace("V1","V2"),"schema");rejected(good.replace("\n30\n","\n29\n"),"protocol");
        rejected(good+"extra\n","trailing field");rejected(good.replace("40001","80"),"reserved port");
        rejected(good.replace("40001","65536"),"large port");rejected(good.replace("1001","-1"),"creation identity");
        rejected(good.replace("a".repeat(32),"0".repeat(32)),"zero session");
        rejected(good.replace("b".repeat(32),"bad"),"key width");rejected("a".repeat(513),"oversize record");
        rejected(good.replace("\n1\n1001","\n01\n1001"),"noncanonical pid");
        check(LocalClientDiscovery.directory(Map.of("LOCALAPPDATA",root.toString())).equals(root.resolve("GoldCraft-client-links-v1")),"default matches native");
        check(LocalClientDiscovery.directory(Map.of("GOLDCRAFT_DISCOVERY_DIR",scanRoot.toString())).equals(scanRoot),"workspace test override");
        try{LocalClientDiscovery.directory(Map.of("GOLDCRAFT_DISCOVERY_DIR","relative"));throw new AssertionError();}catch(IllegalArgumentException expected){checks++;}
        var windows=new LocalClientDiscovery.WindowsIdentity();
        check(windows.privatePath(privateRoot),"real protected same-user directory");
        check(windows.privatePath(privateRoot.resolve("private.gclink")),"real protected same-user file");
        check(!windows.privatePath(root.resolve("public")),"real permissive ACL rejected");
        check(!windows.privatePath(root.resolve("redirected")),"real junction rejected");
        WinBase.FILETIME start=new WinBase.FILETIME(),exit=new WinBase.FILETIME(),kernel=new WinBase.FILETIME(),user=new WinBase.FILETIME();
        check(Kernel32.INSTANCE.GetProcessTimes(Kernel32.INSTANCE.GetCurrentProcess(),start,exit,kernel,user),"current process time readable");
        long pid=Integer.toUnsignedLong(Kernel32.INSTANCE.GetCurrentProcessId()),created=start.toDWordLong().longValue();
        var live=LocalClientDiscovery.parse(record(pid,created,41000,99,false),30);
        check(windows.live(live),"real Windows PID and creation identity");
        check(!windows.live(LocalClientDiscovery.parse(record(pid,created+1,41000,99,false),30)),"PID reuse mismatch rejected");
        check(!windows.live(LocalClientDiscovery.parse(record(4294967295L,created,41000,99,false),30)),"dead process rejected");
        Files.createDirectories(scanRoot);
        var identity=new LocalClientDiscovery.Identity(){public boolean privatePath(Path p){return !p.getFileName().toString().startsWith("foreign");}public boolean live(LocalClientDiscovery.Endpoint e){return e.created()==1000+e.pid();}};
        Files.writeString(scanRoot.resolve("1-1001.gclink"),good);
        check(LocalClientDiscovery.scan(scanRoot,30,99,null,identity).endpoint().equals(a),"bounded record scan");
        Files.writeString(scanRoot.resolve("2-1002.gclink"),record(2,1002,40002,99,false));
        check(LocalClientDiscovery.scan(scanRoot,30,99,null,identity).endpoint()==null,"scan ambiguity");
        Files.delete(scanRoot.resolve("2-1002.gclink"));
        Files.writeString(scanRoot.resolve("2-1003.gclink"),record(2,1003,40002,99,false));
        check(LocalClientDiscovery.scan(scanRoot,30,99,null,identity).endpoint().equals(a),"stale PID reuse record ignored");
        Files.writeString(scanRoot.resolve("3-1003.gclink"),"x".repeat(4096));
        check(LocalClientDiscovery.scan(scanRoot,30,99,null,identity).endpoint().equals(a),"oversize file ignored");
        Files.writeString(scanRoot.resolve("4-1004.gclink"),record(4,1004,40004,100,false));
        check(LocalClientDiscovery.scan(scanRoot,30,99,null,identity).endpoint().equals(a),"wrong world record ignored");
        Files.writeString(scanRoot.resolve("5-1005.gclink"),record(6,1006,40006,99,false));
        check(LocalClientDiscovery.scan(scanRoot,30,99,null,identity).endpoint().equals(a),"record filename identity mismatch ignored");
        Files.writeString(scanRoot.resolve("1-1001.gclink"),record(1,1001,40001,0,false));
        check(LocalClientDiscovery.scan(scanRoot,30,99,a,identity).endpoint()==null,"map/disconnect invalidates active pair");
        Files.writeString(scanRoot.resolve("1-1001.gclink"),record(1,1001,40001,101,false));
        check(LocalClientDiscovery.scan(scanRoot,30,101,null,identity).endpoint()!=null,"new map challenge selects fresh world");
        try(var discovery=new LocalClientDiscovery(scanRoot,30,identity)){
            discovery.request(101,null);
            check(await(()->discovery.result().endpoint()!=null),"background discovery completes");
            discovery.request(0,null);
            check(await(()->discovery.result().endpoint()==null),"ordinary standalone MC remains unattached");
        }
        for(int i=0;i<129;i++)Files.writeString(scanRoot.resolve((500+i)+"-"+(1500+i)+".gclink"),record(500+i,1500+i,42000,101,false));
        check(LocalClientDiscovery.scan(scanRoot,30,101,null,identity).endpoint()==null,"scan bounded under stale-record flood");
        System.out.println("PASS "+checks+" discovery checks; no gameplay, server admission or native publisher execution.");
    }
}
