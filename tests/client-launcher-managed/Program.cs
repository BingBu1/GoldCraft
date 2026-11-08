using GoldCraft.Launcher;
using System.IO.Compression;
using System.Net;
using System.Net.Sockets;
using System.Security.Cryptography;
using System.Text;

if(args.Length==7&&args[0]=="--publication-upgrade"){
    await PublicationUpgrade.Run(args[1],args[2],args[3],args[4],args[5],int.Parse(args[6]));return;
}
string root=SafeFiles.Plain(args[0]);if(Directory.Exists(root))throw new IOException("Fresh output required");Directory.CreateDirectory(root);
var checks=new List<string>();
void Check(bool value,string name){if(!value)throw new Exception(name);checks.Add(name);}
void Reject(Action action,string name){try{action();}catch(Exception e)when(e is IOException or InvalidDataException or ArgumentException or FormatException){checks.Add(name);return;}throw new Exception("Expected rejection: "+name);}
string Hash(byte[] b)=>Convert.ToHexStringLower(SHA256.HashData(b));
var quiet=new Quiet();
string game=Path.Combine(root,"game"),work=Path.Combine(root,"work");Directory.CreateDirectory(Path.Combine(game,"cstrike/cl_dlls"));Directory.CreateDirectory(work);
File.WriteAllText(Path.Combine(game,"hl.exe"),"fixture; never execute");File.WriteAllText(Path.Combine(game,"hw.dll"),"engine");File.WriteAllText(Path.Combine(game,"cstrike/cl_dlls/client.dll"),"client");
File.WriteAllText(Path.Combine(game,"cstrike/config.cfg"),"bindings remain");
const string core="cstrike/metahook/plugins/GoldCraft.dll",extra="cstrike/metahook/plugins/ParallelDownload.dll";
const string catalog="cstrike/metahook/gamedata/bridge/symbols.json",shader="cstrike/renderer/shader/blit.frag.glsl",added="cstrike/metahook/gamedata/parallel/index.json";
var contents=new Dictionary<string,byte[]>{{"MetaHook.exe","offline loader"u8.ToArray()},{core,"dll-zero"u8.ToArray()},{catalog,"catalog-zero"u8.ToArray()},{shader,"shader-zero"u8.ToArray()}};
Asset Row(string name,byte[] data)=>new(name,data.Length,Hash(data));
InlineResource Resource(string name,string value){byte[] data=Encoding.UTF8.GetBytes(value);using var m=new MemoryStream();using(var z=new GZipStream(m,CompressionLevel.Optimal,true))z.Write(data);return new(name,data.Length,Hash(data),Convert.ToBase64String(m.ToArray()));}
using var key=RSA.Create(3072);
var cfg=new BundleConfig(1,"stable-fixture","bootstrap","http://localhost:1/goldcraft/","localhost:27015",key.ExportSubjectPublicKeyInfoPem(),SafeFiles.Hash(Path.Combine(game,"hw.dll")),SafeFiles.Hash(Path.Combine(game,"cstrike/cl_dlls/client.dll")),["GoldCraft.dll"],["FreeImage"],[core],contents.Select(x=>Row(x.Key,x.Value)).ToArray());
using var zipBytes=new MemoryStream();using(var zip=new ZipArchive(zipBytes,ZipArchiveMode.Create,true))foreach(var entry in contents){using var stream=zip.CreateEntry(entry.Key).Open();stream.Write(entry.Value);}
var bundle=new Bundle(cfg,()=>new MemoryStream(zipBytes.ToArray()));bundle.Install(game,work,quiet,CancellationToken.None);
byte[] Sign(Release release){byte[] payload=JsonData.Write(release);return JsonData.Write(new SignedEnvelope(Convert.ToBase64String(payload),Convert.ToBase64String(key.SignData(payload,HashAlgorithmName.SHA256,RSASignaturePadding.Pkcs1))));}
using var server=new Endpoint();using var updates=new Updates(cfg,server.Url);
byte[] one="dll-one"u8.ToArray(),two="dll-two"u8.ToArray(),plugin="new parallel plugin"u8.ToArray();
var first=new Release(2,cfg.BundleId,10,"first",cfg.GameAddress,[Row(core,one),Row(extra,plugin)],
    [Resource(catalog,"catalog-one"),Resource(shader,"shader-one"),Resource(added,"new plugin index")],
    ["GoldCraft.dll","ParallelDownload.dll"],cfg.EngineSha256,cfg.ClientSha256);
void Publish(Release release,params byte[][] bodies){server.Files["manifest.json"]=Sign(release);for(int i=0;i<bodies.Length;i++)server.Files["blobs/"+release.Files[i].Sha256+"/"+Path.GetFileName(release.Files[i].Path)]=bodies[i];}
Publish(first,one,plugin);await updates.Apply(game,work,bundle,quiet,CancellationToken.None);
Check(bundle.Installed(game)?.Sequence==10,"first signed DLL and resource upgrade accepted by same bootstrap");
Check(File.ReadAllText(SafeFiles.Under(game,catalog))=="catalog-one"&&File.ReadAllText(SafeFiles.Under(game,shader))=="shader-one","DLL-required catalogs and shaders advance together");
Check(File.Exists(SafeFiles.Under(game,extra))&&File.ReadAllText(SafeFiles.Under(game,"cstrike/metahook/configs/plugins.lst")).Contains("ParallelDownload.dll"),"new plugin DLL and loading order installed");
int reads=server.BlobReads;await updates.Apply(game,work,bundle,quiet,CancellationToken.None);
Check(server.BlobReads==reads,"unchanged release performs no redundant DLL downloads");
var second=first with{Sequence=11,Version="second",Files=[Row(core,two)],Resources=[Resource(catalog,"catalog-two"),Resource(shader,"shader-two")],Plugins=["GoldCraft.dll"]};
Publish(second,two);
File.WriteAllText(SafeFiles.Under(game,added),"external modification");
try{await updates.Apply(game,work,bundle,quiet,CancellationToken.None);throw new Exception("modified retirement accepted");}catch(InvalidDataException){checks.Add("retirement refuses an externally modified managed resource");}
Check(bundle.ReadState(game)?.Sequence==10&&File.ReadAllText(SafeFiles.Under(game,core))=="dll-one","retirement refusal leaves installed DLL and sequence unchanged");
File.WriteAllBytes(SafeFiles.Under(game,added),ReleasePolicy.Decode(first.Resources![2]));
await updates.Apply(game,work,bundle,quiet,CancellationToken.None);
Check(bundle.Installed(game)?.Sequence==11&&File.ReadAllText(SafeFiles.Under(game,catalog))=="catalog-two","second DLL plus static-resource upgrade needs no new launcher");
Check(!File.Exists(SafeFiles.Under(game,extra))&&!File.Exists(SafeFiles.Under(game,added))&&!File.ReadAllText(SafeFiles.Under(game,"cstrike/metahook/configs/plugins.lst")).Contains("ParallelDownload.dll"),"retired signed plugin and catalog are removed together");
Check(File.ReadAllText(Path.Combine(game,"cstrike/config.cfg"))=="bindings remain","player bindings preserved across both updates");
File.WriteAllText(SafeFiles.Under(game,catalog),"damaged");
Check(bundle.Installed(game)==null,"corrupted managed resource blocks launch");
bundle.Install(game,work,quiet,CancellationToken.None);
Check(bundle.ReadState(game)?.Sequence==11&&File.ReadAllText(SafeFiles.Under(game,core))=="dll-two","repair never downgrades online DLL or anti-replay sequence");
await updates.Apply(game,work,bundle,quiet,CancellationToken.None);
Check(bundle.Installed(game)?.Sequence==11,"same signed release repairs a corrupt resource");
File.Delete(SafeFiles.Under(game,core));await updates.Apply(game,work,bundle,quiet,CancellationToken.None);
Check(bundle.Installed(game)?.Sequence==11,"same signed release repairs a missing DLL");
Reject(()=>updates.VerifyRelease(Sign(first),11),"older signed release rejected");
Reject(()=>updates.VerifyRelease(Sign(second with{EngineSha256=new string('a',64)}),0),"different engine identity rejected");
Reject(()=>updates.VerifyRelease(Sign(second with{Files=[Row("hw.dll",two)]}),0),"engine replacement rejected");
Reject(()=>updates.VerifyRelease(Sign(second with{Resources=[Resource("cstrike/config.cfg","overwrite")] }),0),"player configuration outside managed roots rejected");
Reject(()=>updates.VerifyRelease(Sign(second with{Resources=[Resource("cstrike/metahook/gamedata/../../config.json","escape")]}),0),"resource traversal rejected");
Reject(()=>updates.VerifyRelease(Sign(second with{Resources=[Resource(catalog,"body") with{Bytes=1}]}),0),"oversized decompression rejected");
Reject(()=>updates.VerifyRelease(Sign(second with{Resources=[Resource(catalog,"body") with{Sha256=new string('0',64)}]}),0),"inline resource hash mismatch rejected");
Reject(()=>updates.VerifyRelease(Sign(second with{Resources=[Resource(catalog,"body"),Resource(catalog.ToUpperInvariant(),"other")]}),0),"case duplicate resource rejected");
Reject(()=>updates.VerifyRelease(Sign(second with{Plugins=["GoldCraft.dll","Missing.dll"]}),0),"plugin list must match signed DLLs");
byte[] valid=server.Files["manifest.json"];
var changed=second with{Resources=[Resource(catalog,"changed same sequence"),Resource(shader,"shader-two")]};Publish(changed,two);
try{await updates.Apply(game,work,bundle,quiet,CancellationToken.None);throw new Exception("same sequence accepted");}catch(InvalidDataException){checks.Add("changed same-sequence signed resource rejected");}
server.Files["manifest.json"]=Sign(new Release(1,cfg.BundleId,20,"downgrade",cfg.GameAddress,second.Files));
try{await updates.Apply(game,work,bundle,quiet,CancellationToken.None);throw new Exception("v1 downgrade accepted");}catch(InvalidDataException){checks.Add("legacy protocol downgrade rejected after v2 adoption");}
var broken=second with{Sequence=12,Files=[Row(core,"next"u8.ToArray())],Resources=[Resource(catalog,"next"),Resource(shader,"next")]};Publish(broken,"wrong bytes"u8.ToArray());
try{await updates.Apply(game,work,bundle,quiet,CancellationToken.None);throw new Exception("bad download accepted");}catch(InvalidDataException){checks.Add("bad DLL bytes cannot commit paired resources");}
Check(bundle.Installed(game)?.Sequence==11&&File.ReadAllText(SafeFiles.Under(game,catalog))=="catalog-two","failed download retains complete previous release");
// Failure during a real transaction after replacing the DLL must restore it.
var failing=second with{Sequence=12,Files=[Row(core,"next"u8.ToArray())],Resources=[Resource(catalog,"next"),Resource(shader,"next"),Resource(added,"new")]};
Directory.CreateDirectory(SafeFiles.Under(game,added));Publish(failing,"next"u8.ToArray());
try{await updates.Apply(game,work,bundle,quiet,CancellationToken.None);throw new Exception("commit failure expected");}catch(IOException){checks.Add("mid-commit resource failure triggers transaction rollback");}
Check(bundle.Installed(game)?.Sequence==11&&File.ReadAllText(SafeFiles.Under(game,core))=="dll-two"&&File.ReadAllText(SafeFiles.Under(game,catalog))=="catalog-two","mid-commit rollback restores DLL catalog and receipt");
string stateFile=SafeFiles.Under(game,".goldcraft-launcher/installed.json");byte[] saved=File.ReadAllBytes(stateFile);
var state=JsonData.Read<InstallState>(saved);var envelope=state.Publication!;
File.WriteAllBytes(stateFile,JsonData.Write(state with{Publication=envelope with{Signature=Convert.ToBase64String(new byte[384])}}));
Check(bundle.Installed(game)==null,"unsigned installed resource receipt cannot authorize launch");File.WriteAllBytes(stateFile,saved);

string? exeHash=null;
if(args.Length is 5 or 6){
    string exe=args[1],runtime=args[2],publication=args[3],assets=args[4];exeHash=SafeFiles.Hash(exe);
    var shipped=ShippedBundle.Read(exe);
    Check(Hash(shipped.config)==SafeFiles.Hash(Path.Combine(assets,"bundle.json"))&&Hash(shipped.payload)==SafeFiles.Hash(Path.Combine(assets,"payload.zip")),"final EXE contains exact audited offline payload");
    string full=Path.Combine(root,"shipped-game");Directory.CreateDirectory(Path.Combine(full,"cstrike/cl_dlls"));
    foreach(string rel in new[]{"hl.exe","hw.dll","cstrike/cl_dlls/client.dll"})File.Copy(SafeFiles.Under(runtime,rel),SafeFiles.Under(full,rel));
    shipped.bundle.Install(full,work,quiet,CancellationToken.None);
    byte[] signed=File.ReadAllBytes(Path.Combine(publication,"manifest.json"));using var actual=new Updates(shipped.bundle.Config,server.Url);
    var release=actual.VerifyRelease(signed,0);server.Files["manifest.json"]=signed;
    foreach(var row in release.Files)server.Files["blobs/"+row.Sha256+"/"+Path.GetFileName(row.Path)]=File.ReadAllBytes(Path.Combine(publication,"blobs",row.Sha256,Path.GetFileName(row.Path)));
    if(args.Length==6){
        server.Files["manifest.json"]=File.ReadAllBytes(Path.Combine(args[5],"manifest.json"));
        await actual.Apply(full,work,shipped.bundle,quiet,CancellationToken.None);
        Check(shipped.bundle.Installed(full)?.Sequence==2026110801,"actual prior protocol-1 receipt remains compatible with new EXE");
        server.Files["manifest.json"]=signed;
    }
    int oldReads=server.BlobReads;await actual.Apply(full,work,shipped.bundle,quiet,CancellationToken.None);
    Check(shipped.bundle.Installed(full)?.Sequence==release.Sequence&&release.Schema==2,"actual RSA-signed server release installs from final EXE baseline");
    Check(release.Resources!.Length==264&&release.Files.Length==19,"all current catalogs shaders and cores included");
    Check(server.BlobReads==oldReads,"shipped current DLLs are not downloaded again");
    foreach(var row in release.Resources)SafeFiles.Verify(SafeFiles.Under(full,row.Path),ReleasePolicy.Asset(row));
    Check(SafeFiles.Hash(exe)==exeHash,"launcher EXE byte identity is unchanged after updates");
}
File.WriteAllBytes(Path.Combine(root,"receipt.json"),System.Text.Json.JsonSerializer.SerializeToUtf8Bytes(new{passed=true,checks,exeSha256=exeHash,gameExecuted=false},JsonData.Options));
Console.WriteLine($"{checks.Count} managed update checks passed; same bootstrap updated DLLs/catalogs/shaders twice, added and retired a plugin, and rolled back failed commits.");

sealed class Quiet:IProgress<string>{public void Report(string value){}}
sealed class Endpoint:IDisposable{
    readonly TcpListener listener=new(IPAddress.Loopback,0);readonly CancellationTokenSource cancel=new();readonly Task worker;
    public readonly System.Collections.Concurrent.ConcurrentDictionary<string,byte[]> Files=new();public int BlobReads;public string Url{get;}
    public Endpoint(){listener.Start();Url=$"http://127.0.0.1:{((IPEndPoint)listener.LocalEndpoint).Port}/goldcraft/";worker=Task.Run(Serve);}
    async Task Serve(){try{while(!cancel.IsCancellationRequested){using var client=await listener.AcceptTcpClientAsync(cancel.Token);await using var stream=client.GetStream();using var reader=new StreamReader(stream,Encoding.ASCII,false,1024,true);string first=(await reader.ReadLineAsync(cancel.Token))??"";while(!string.IsNullOrEmpty(await reader.ReadLineAsync(cancel.Token))){}string rel=first.Split(' ')[1]["/goldcraft/launcher/v1/".Length..];Files.TryGetValue(rel,out var body);body??=[];if(rel.StartsWith("blobs/"))Interlocked.Increment(ref BlobReads);await stream.WriteAsync(Encoding.ASCII.GetBytes($"HTTP/1.1 {(body.Length>0?"200 OK":"404 Not Found")}\r\nContent-Length: {body.Length}\r\nConnection: close\r\n\r\n"),cancel.Token);await stream.WriteAsync(body,cancel.Token);}}catch(OperationCanceledException){}}
    public void Dispose(){cancel.Cancel();listener.Stop();try{worker.GetAwaiter().GetResult();}catch(SocketException){}cancel.Dispose();}
}
