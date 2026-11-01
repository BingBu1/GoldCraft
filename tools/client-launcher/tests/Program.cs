using GoldCraft.Launcher;
using System.IO.Compression;
using System.Net;
using System.Net.Sockets;
using System.Security.Cryptography;
using System.Text;

if(args.Length==4 && args[0]=="--live-download")
{
    await DownloadChecks.Live(args[1],args[2],args[3]);return;
}
int checks=0;
void Check(bool value,string name){checks++;if(!value)throw new Exception(name);}
void Reject(Action action,string name){try{action();}catch(Exception ex)when(ex is IOException or InvalidDataException or ArgumentException or FormatException){checks++;return;}throw new Exception("Expected rejection: "+name);}
string root=SafeFiles.Plain(args[0]);Directory.CreateDirectory(root);
await DownloadChecks.Run(Check,root);
string game=Path.Combine(root,"game"),work=Path.Combine(root,"work");Directory.CreateDirectory(game);Directory.CreateDirectory(work);
Directory.CreateDirectory(Path.Combine(game,"cstrike/cl_dlls"));
File.WriteAllText(Path.Combine(game,"hl.exe"),"test fixture; never execute");
File.WriteAllText(Path.Combine(game,"hw.dll"),"fixture engine");File.WriteAllText(Path.Combine(game,"cstrike/cl_dlls/client.dll"),"fixture client");
File.WriteAllText(Path.Combine(game,"cstrike/config.cfg"),"bind F6 goldcraft_menu\nname preserved");
using var rsa=RSA.Create(3072);
string hash(byte[] bytes)=>Convert.ToHexStringLower(SHA256.HashData(bytes));
var payload=new Dictionary<string,byte[]>{["MetaHook.exe"]=Encoding.UTF8.GetBytes("fixture launcher"),["cstrike/metahook/plugins/GoldCraft.dll"]=Encoding.UTF8.GetBytes("core-old"),["cstrike/metahook/shaders/resource.fsh"]=Encoding.UTF8.GetBytes("static-resource")};
var assets=payload.Select(x=>new Asset(x.Key,x.Value.Length,hash(x.Value))).ToArray();
var config=new BundleConfig(1,"fixture-bundle","base","http://localhost:1/goldcraft/","localhost:27015",rsa.ExportSubjectPublicKeyInfoPem(),SafeFiles.Hash(Path.Combine(game,"hw.dll")),SafeFiles.Hash(Path.Combine(game,"cstrike/cl_dlls/client.dll")),["GoldCraft.dll"],["FreeImage"],["cstrike/metahook/plugins/GoldCraft.dll"],assets);
byte[] Zip(Dictionary<string,byte[]> files){using var m=new MemoryStream();using(var z=new ZipArchive(m,ZipArchiveMode.Create,true))foreach(var f in files){using var s=z.CreateEntry(f.Key).Open();s.Write(f.Value);}return m.ToArray();}
byte[] zip=Zip(payload);var bundle=new Bundle(config,()=>new MemoryStream(zip));
Check(bundle.Installed(game)==null,"launch requires installation");
bundle.Install(game,work,new Progress<string>(),CancellationToken.None);
Check(bundle.Installed(game)!=null,"installation receipt and full payload verified");
Check(File.ReadAllText(Path.Combine(game,"cstrike/config.cfg")).Contains("preserved"),"native configuration retained");
Check(Bundle.MergePlugins("Other.dll\nGoldCraft_AVX2.dll\n// comment\n",["GoldCraft.dll"]).Contains("Other.dll"),"unrelated plugin retained");
Reject(()=>SafeFiles.Under(game,"../outside.dll"),"traversal");Reject(()=>SafeFiles.Under(game,"x.dll:stream"),"ADS");Reject(()=>SafeFiles.Under(game,"CON.dll"),"device");
Reject(()=>Updates.GameAddress("host:1234 +quit"),"command injection");Reject(()=>Updates.BaseUri("http://user:pass@localhost/goldcraft/"),"credentials in URL");
byte[] Signed(Release release){byte[] p=JsonData.Write(release);return JsonData.Write(new SignedEnvelope(Convert.ToBase64String(p),Convert.ToBase64String(rsa.SignData(p,HashAlgorithmName.SHA256,RSASignaturePadding.Pkcs1))));}
byte[] next=Encoding.UTF8.GetBytes("core-new");var file=new Asset(config.CorePaths[0],next.Length,hash(next));
var release=new Release(1,config.BundleId,1,"new","localhost:27015",[file]);
using var endpoint=new FixtureHttp();
endpoint.Files["/goldcraft/launcher/v1/manifest.json"]=Signed(release);
endpoint.Files["/goldcraft/launcher/v1/blobs/"+file.Sha256+"/GoldCraft.dll"]=next;
endpoint.Files["/goldcraft/launcher/v1/status.json"]=JsonData.Write(new ServerStatus(1,true,"测试服务器","zs_deadend",1,32,"localhost:27015"));
using var updates=new Updates(config,endpoint.Url);
Check((await updates.Status(CancellationToken.None)).Map=="zs_deadend","status over actual HTTP");
var envelope=JsonData.Read<SignedEnvelope>(Signed(release));
Reject(()=>updates.VerifyRelease(JsonData.Write(envelope with{Payload=Convert.ToBase64String(Encoding.UTF8.GetBytes("{}"))}),0),"tampered manifest");
Reject(()=>updates.VerifyRelease(Signed(release with{Files=[file with{Path="hw.dll"}]}),0),"non-core DLL");
Reject(()=>updates.VerifyRelease(Signed(release with{Sequence=1}),2),"rollback replay");
Reject(()=>updates.VerifyRelease(Signed(release with{BundleId="incompatible"}),0),"resource baseline mismatch");
await updates.Apply(game,work,bundle,new Progress<string>(),CancellationToken.None);
Check(SafeFiles.Hash(Path.Combine(game,file.Path))==file.Sha256,"core update applied");
Check(bundle.Installed(game)?.Sequence==1,"updated installation is launchable");
int blobs=endpoint.BlobReads;
await updates.Apply(game,work,bundle,new Progress<string>(),CancellationToken.None);
Check(endpoint.BlobReads==blobs,"unchanged DLL is not downloaded again");
Check(File.ReadAllText(Path.Combine(game,"cstrike/metahook/shaders/resource.fsh"))=="static-resource","online update does not touch resources");
var bad=release with{Sequence=2,Files=[file with{Sha256=new string('a',64)}]};
endpoint.Files["/goldcraft/launcher/v1/manifest.json"]=Signed(bad);
endpoint.Files["/goldcraft/launcher/v1/blobs/"+new string('a',64)+"/GoldCraft.dll"]=next;
try{await updates.Apply(game,work,bundle,new Progress<string>(),CancellationToken.None);throw new Exception("Bad hash accepted");}catch(InvalidDataException){checks++;}
Check(bundle.Installed(game)?.Sequence==1,"bad hash preserves installed version");
string replacement=Path.Combine(work,"replacement.dll");File.WriteAllText(replacement,"rollback test");
Reject(()=>Transactions.Apply(game,[new(file.Path,replacement),new("cstrike/second.dll",replacement)],i=>{if(i==1)throw new IOException("injected commit failure");}),"atomic batch rollback");
Check(SafeFiles.Hash(Path.Combine(game,file.Path))==file.Sha256&&!File.Exists(Path.Combine(game,"cstrike/second.dll")),"failed transaction rolled back all files");
// A hard-linked destination must detach during atomic replacement.
string original=Path.Combine(work,"immutable.dll"),linked=Path.Combine(game,"cstrike/linked.dll");File.WriteAllText(original,"shared bytes");
if(!Native.CreateHardLink(linked,original,IntPtr.Zero))throw new System.ComponentModel.Win32Exception();
Transactions.Apply(game,[new("cstrike/linked.dll",replacement)]);
Check(File.ReadAllText(original)=="shared bytes"&&File.ReadAllText(linked)=="rollback test","shared resource detached before replacement");
Reject(()=>updates.VerifyRelease(Encoding.UTF8.GetBytes("{}"),1),"missing signature fields");
Reject(()=>updates.VerifyRelease(Signed(release with{Files=[null!]}),1),"null update file");
string alias=Path.Combine(game,"cstrike/metahook/plugins/GoldCraft_AVX2.dll");File.WriteAllText(alias,"obsolete higher-priority alias");
Check(bundle.Installed(game)==null,"obsolete CPU alias blocks launch");
bundle.Install(game,work,new Progress<string>(),CancellationToken.None);
Check(!File.Exists(alias)&&bundle.Installed(game)!=null,"repair retires obsolete CPU alias");
string pending=Path.Combine(game,".goldcraft-launcher/backups/pending-recovery");Directory.CreateDirectory(pending);
File.WriteAllBytes(Path.Combine(pending,"transaction.json"),JsonData.Write(new Journal("prepared",[new("cstrike/interrupted.dll",false,null,hash(Encoding.UTF8.GetBytes("interrupted")))])));
File.WriteAllText(Path.Combine(game,"cstrike/interrupted.dll"),"interrupted");
Transactions.Recover(game);
Check(!File.Exists(Path.Combine(game,"cstrike/interrupted.dll")),"restart recovers an interrupted transaction");
if(args.Length>=3)
{
    string assetsRoot=SafeFiles.Plain(args[1]),runtime=SafeFiles.Plain(args[2]);
    var fullConfig=JsonData.Read<BundleConfig>(File.ReadAllBytes(Path.Combine(assetsRoot,"bundle.json")));
    var fullBundle=new Bundle(fullConfig,()=>File.OpenRead(Path.Combine(assetsRoot,"payload.zip")));
    string fullGame=Path.Combine(root,"full-game");Directory.CreateDirectory(Path.Combine(fullGame,"cstrike/cl_dlls"));
    foreach(string rel in new[]{"hl.exe","hw.dll","cstrike/cl_dlls/client.dll"})File.Copy(SafeFiles.Under(runtime,rel),SafeFiles.Under(fullGame,rel),false);
    fullBundle.Install(fullGame,work,new Progress<string>(),CancellationToken.None);
    Check(fullBundle.Installed(fullGame)!=null,"full shipped MetaHook payload installs and is launchable");
    var actual=Directory.EnumerateFiles(fullGame,"*",SearchOption.AllDirectories).Select(f=>Path.GetRelativePath(fullGame,f).Replace('\\','/')).ToHashSet(StringComparer.OrdinalIgnoreCase);
    Check(fullConfig.Files.All(a=>actual.Contains(a.Path)),"every embedded asset present");
    if(args.Length==4)
    {
        using var published=new Updates(fullConfig,endpoint.Url);
        byte[] signed=File.ReadAllBytes(args[3]);
        var shipped=published.VerifyRelease(signed,0);
        Check(shipped.Files.Length==fullConfig.CorePaths.Length,"actual publication matches embedded signing key and core allowlist");
        endpoint.Files["/goldcraft/launcher/v1/manifest.json"]=signed;
        int before=endpoint.BlobReads;
        await published.Apply(fullGame,work,fullBundle,new Progress<string>(),CancellationToken.None);
        Check(fullBundle.Installed(fullGame)?.Sequence==shipped.Sequence&&endpoint.BlobReads==before,"actual signed release accepted without redundant DLL download");
    }
}
const string privateEndpoint="http://downloads.example.test:28080/goldcraft/";
string visible=UserMessages.Clean("Failed at "+privateEndpoint+"; host downloads.example.test:28080",privateEndpoint);
Check(!visible.Contains("http",StringComparison.OrdinalIgnoreCase)&&!visible.Contains("example.test"),"HTTP URLs and host names removed from visible messages");
Check(!UserMessages.Error(new HttpRequestException("Failed "+privateEndpoint),privateEndpoint).Contains("example.test"),"raw network error is not displayed");
Check(!Encoding.UTF8.GetString(JsonData.Write(new Preferences("test-game"))).Contains("updateBase"),"user preferences do not persist download address");
Console.WriteLine($"{checks} checks passed; signed HTTP update, install gating, rollback, path restrictions, hard-link isolation and private UI messages verified.");

static class Native{[System.Runtime.InteropServices.DllImport("kernel32.dll",CharSet=System.Runtime.InteropServices.CharSet.Unicode,SetLastError=true)]public static extern bool CreateHardLink(string name,string existing,IntPtr security);}
sealed class FixtureHttp:IDisposable
{
    private readonly TcpListener listener=new(IPAddress.Loopback,0);
    private readonly CancellationTokenSource cancel=new();private readonly Task worker;
    public readonly System.Collections.Concurrent.ConcurrentDictionary<string,byte[]> Files=new();public int BlobReads;
    public string Url{get;}
    public FixtureHttp(){listener.Start();Url=$"http://127.0.0.1:{((IPEndPoint)listener.LocalEndpoint).Port}/goldcraft/";worker=Task.Run(Serve);}
    private async Task Serve()
    {
        try{while(!cancel.IsCancellationRequested){using var client=await listener.AcceptTcpClientAsync(cancel.Token);await using var stream=client.GetStream();using var reader=new StreamReader(stream,Encoding.ASCII,false,1024,true);string first=(await reader.ReadLineAsync(cancel.Token))??"";while(!string.IsNullOrEmpty(await reader.ReadLineAsync(cancel.Token))){}string target=first.Split(' ')[1];bool ok=Files.TryGetValue(target,out var body);body??=[];if(target.Contains("/blobs/"))Interlocked.Increment(ref BlobReads);byte[] head=Encoding.ASCII.GetBytes($"HTTP/1.1 {(ok?"200 OK":"404 Not Found")}\r\nContent-Length: {body.Length}\r\nConnection: close\r\n\r\n");await stream.WriteAsync(head,cancel.Token);await stream.WriteAsync(body,cancel.Token);}}catch(OperationCanceledException){}
    }
    public void Dispose(){cancel.Cancel();listener.Stop();try{worker.GetAwaiter().GetResult();}catch(SocketException){}cancel.Dispose();}
}
