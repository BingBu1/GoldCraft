using GoldCraft.Launcher;
using System.IO.Compression;
using System.Net;
using System.Net.Sockets;
using System.Reflection;
using System.Security.Cryptography;
using System.Text;

// Actual shipped old payload -> incompatible DLL-only release -> new EXE repair.
// No fixture binary is executed. All writes are restricted to a fresh output.
if(args.Length!=6)throw new ArgumentException("output old-assets new-exe runtime old-publication new-publication");
string root=SafeFiles.Plain(args[0]);
if(Directory.Exists(root))throw new IOException("Use a fresh fixture directory");
Directory.CreateDirectory(root);
var checks=new List<string>();
void Check(bool ok,string name){if(!ok)throw new Exception(name);checks.Add(name);}
void Reject(Action act,string name){try{act();}catch(InvalidDataException){checks.Add(name);return;}throw new Exception("Expected rejection: "+name);}
string locked=Path.Combine(root,"locked-target.txt"),replacement=Path.Combine(root,"replacement.txt");
File.WriteAllText(locked,"before");File.WriteAllText(replacement,"after");
var held=File.Open(locked,FileMode.Open,FileAccess.Read,FileShare.Read);
var releaseLock=Task.Run(async()=>{await Task.Delay(220);held.Dispose();});
SafeFiles.Atomic(replacement,locked);await releaseLock;
Check(File.ReadAllText(locked)=="after","real transient Windows sharing lock is retried atomically");
File.WriteAllText(locked,"before");
var mutableLock=File.Open(locked,FileMode.Open,FileAccess.Read,FileShare.ReadWrite);
var mutate=Task.Run(async()=>{await Task.Delay(220);File.WriteAllText(locked,"external edit");mutableLock.Dispose();});
bool refused=false;
try{SafeFiles.Atomic(replacement,locked);}catch(IOException){refused=true;}
await mutate;
Check(refused&&File.ReadAllText(locked)=="external edit","external edit during retry delay is preserved and replacement refused");
string oldAssets=SafeFiles.Plain(args[1]),exe=SafeFiles.Plain(args[2]),runtime=SafeFiles.Plain(args[3]);
string oldPub=SafeFiles.Plain(args[4]),newPub=SafeFiles.Plain(args[5]);
byte[] exeBytes=File.ReadAllBytes(exe);
byte[] marker=Convert.FromHexString("8b1202b96a612038727b930214d7a03213f5b9e6efae3318ee3b2dce24b36aae");
int markerAt=exeBytes.AsSpan().IndexOf(marker);
Check(markerAt>=8,"single-file .NET bundle marker present");
using var input=new MemoryStream(exeBytes);using var reader=new BinaryReader(input,Encoding.UTF8,true);
input.Position=BitConverter.ToInt64(exeBytes,markerAt-8);
Check(reader.ReadUInt32()==6&&reader.ReadUInt32()==0,"supported single-file header");
int count=reader.ReadInt32();reader.ReadString();input.Position+=40;
byte[]? assemblyBytes=null;
for(int i=0;i<count;i++){
    long offset=reader.ReadInt64(),size=reader.ReadInt64(),compressed=reader.ReadInt64();reader.ReadByte();string name=reader.ReadString();
    if(name!="冰桑启动器.dll")continue;
    using var contents=new MemoryStream(exeBytes,checked((int)offset),checked((int)(compressed>0?compressed:size)));
    using var output=new MemoryStream();
    if(compressed>0){using var inflate=new DeflateStream(contents,CompressionMode.Decompress);inflate.CopyTo(output);}else contents.CopyTo(output);
    assemblyBytes=output.ToArray();Check(assemblyBytes.LongLength==size,"embedded managed assembly length verified");
}
var assembly=Assembly.Load(assemblyBytes??throw new Exception("Launcher assembly absent"));
byte[] Resource(string name){using var stream=assembly.GetManifestResourceStream(name)??throw new Exception(name);using var copy=new MemoryStream();stream.CopyTo(copy);return copy.ToArray();}
byte[] configBytes=Resource("bundle.json"),payloadBytes=Resource("payload.zip");
File.WriteAllBytes(Path.Combine(root,"embedded-bundle.json"),configBytes);
var cfg=JsonData.Read<BundleConfig>(configBytes);
var oldCfg=JsonData.Read<BundleConfig>(File.ReadAllBytes(Path.Combine(oldAssets,"bundle.json")));
var previous=new Bundle(oldCfg,()=>File.OpenRead(Path.Combine(oldAssets,"payload.zip")));
var current=new Bundle(cfg,()=>new MemoryStream(payloadBytes,false));
string game=Path.Combine(root,"game"),work=Path.Combine(root,"work");
Directory.CreateDirectory(Path.Combine(game,"cstrike/cl_dlls"));Directory.CreateDirectory(work);
foreach(string rel in new[]{"hl.exe","hw.dll","cstrike/cl_dlls/client.dll"})File.Copy(SafeFiles.Under(runtime,rel),SafeFiles.Under(game,rel));
const string settings="bind F6 goldcraft_menu\r\nbind z custom_menu\r\nname upgrade-fixture\r\n";
File.WriteAllText(Path.Combine(game,"cstrike/config.cfg"),settings);
string pluginList=Path.Combine(game,"cstrike/metahook/configs/plugins.lst");Directory.CreateDirectory(Path.GetDirectoryName(pluginList)!);
File.WriteAllText(pluginList,"OtherPlugin.dll\r\n");
var progress=new QuietProgress();
previous.Install(game,work,progress,CancellationToken.None);
Check(previous.Installed(game)!=null,"actual old embedded payload installed");
using var server=new FixtureHttp();
server.Publication=oldPub;
using var oldUpdates=new Updates(oldCfg,server.Url);
await oldUpdates.Apply(game,work,previous,progress,CancellationToken.None);
Check(previous.Installed(game)?.Sequence==2026110701,"old launcher accepts incompatible November 7 DLL-only publication");
Check(server.BlobReads==2,"exactly two changed old core DLLs downloaded over HTTP");
const string precache="cstrike/metahook/gamedata/goldcraft-precache/goldcraft-precache-hl-10210.json";
const string cs="cstrike/metahook/gamedata/goldcraft-cs/cs-client-10210-goldcraft.json";
Check(!File.ReadAllText(SafeFiles.Under(game,precache)).Contains("CSOL_CRC32_ProcessBuffer"),"old launchable receipt reproduces missing CRC catalog");
Check(!File.ReadAllText(SafeFiles.Under(game,cs)).Contains("UTIL_SharedRandomFloat"),"old catalog also lacks shared random symbol");
Check(current.Installed(game)==null,"new EXE requires repair of the old static contract");
using var newUpdates=new Updates(cfg,server.Url);
byte[] newSigned=File.ReadAllBytes(Path.Combine(newPub,"manifest.json")),oldSigned=File.ReadAllBytes(Path.Combine(oldPub,"manifest.json"));
Reject(()=>oldUpdates.VerifyRelease(newSigned,0),"old launcher rejects new static contract");
Reject(()=>newUpdates.VerifyRelease(oldSigned,0),"new launcher rejects old static contract");
current.Install(game,work,progress,CancellationToken.None);
Check(current.Installed(game)?.BundleId==cfg.BundleId,"new EXE repaired actual old installation");
foreach(var asset in cfg.Files)SafeFiles.Verify(SafeFiles.Under(game,asset.Path),asset);
Check(cfg.Files.Length==374&&cfg.CorePaths.Length==19,"all 374 shipped assets and 19 core files verified");
foreach(var pair in new[]{(precache,"CSOL_CRC32_ProcessBuffer"),(cs,"UTIL_SharedRandomFloat"),(cs,"CSOL_ClientCRTRand")})
    Check(File.ReadAllText(SafeFiles.Under(game,pair.Item1)).Contains(pair.Item2),"required symbol installed: "+pair.Item2);
foreach(string rel in new[]{"cstrike/metahook/gamedata/goldcraft-cs/index.json","cstrike/metahook/gamedata/goldcraft-precache/index.json"}){
    SafeFiles.Verify(SafeFiles.Under(game,rel),cfg.Files.Single(a=>a.Path==rel));
    Check(SafeFiles.Hash(SafeFiles.Under(game,rel))!=oldCfg.Files.Single(a=>a.Path==rel).Sha256,"matching catalog index replaced: "+rel);
}
Check(File.ReadAllText(Path.Combine(game,"cstrike/config.cfg"))==settings,"player configuration and bindings preserved");
Check(File.ReadAllText(pluginList).Contains("OtherPlugin.dll"),"unrelated plugin preserved");
server.Publication=newPub;int before=server.BlobReads;
await newUpdates.Apply(game,work,current,progress,CancellationToken.None);
Check(current.Installed(game)?.Sequence==2026110801,"new signed HTTP release accepted after repair");
Check(server.BlobReads==before,"embedded current DLLs avoid redundant downloads");
current.Install(game,work,progress,CancellationToken.None);
await newUpdates.Apply(game,work,current,progress,CancellationToken.None);
Check(current.Installed(game)?.Sequence==2026110801&&server.BlobReads==before,"repeated install and update are idempotent");
File.WriteAllBytes(Path.Combine(root,"receipt.json"),System.Text.Json.JsonSerializer.SerializeToUtf8Bytes(new{
    passed=true,checks,exeSha256=SafeFiles.Hash(exe),assemblySha256=Convert.ToHexStringLower(SHA256.HashData(assemblyBytes!)),
    embeddedConfigSha256=Convert.ToHexStringLower(SHA256.HashData(configBytes)),embeddedPayloadSha256=Convert.ToHexStringLower(SHA256.HashData(payloadBytes)),
    previousBundleId=oldCfg.BundleId,bundleId=cfg.BundleId,sequence=2026110801,assets=cfg.Files.Length,gameExecuted=false},JsonData.Options));
Console.WriteLine($"{checks.Count} upgrade checks passed using actual shipped payload and final single-file EXE.");

sealed class QuietProgress:IProgress<string>{public void Report(string value){}}
sealed class FixtureHttp:IDisposable{
    private readonly TcpListener listener=new(IPAddress.Loopback,0);private readonly CancellationTokenSource cancel=new();private readonly Task worker;
    public string Publication="";public int BlobReads;public string Url{get;}
    public FixtureHttp(){listener.Start();Url=$"http://127.0.0.1:{((IPEndPoint)listener.LocalEndpoint).Port}/goldcraft/";worker=Task.Run(Serve);}
    private async Task Serve(){try{while(!cancel.IsCancellationRequested){using var client=await listener.AcceptTcpClientAsync(cancel.Token);await using var stream=client.GetStream();using var r=new StreamReader(stream,Encoding.ASCII,false,1024,true);string first=(await r.ReadLineAsync(cancel.Token))??"";while(!string.IsNullOrEmpty(await r.ReadLineAsync(cancel.Token))){}string route=first.Split(' ')[1];string rel=route["/goldcraft/launcher/v1/".Length..];string path=SafeFiles.Under(Publication,rel);byte[] body=File.Exists(path)?File.ReadAllBytes(path):[];if(rel.StartsWith("blobs/"))Interlocked.Increment(ref BlobReads);byte[] head=Encoding.ASCII.GetBytes($"HTTP/1.1 {(body.Length>0?"200 OK":"404 Not Found")}\r\nContent-Length: {body.Length}\r\nConnection: close\r\n\r\n");await stream.WriteAsync(head,cancel.Token);await stream.WriteAsync(body,cancel.Token);}}catch(OperationCanceledException){}}
    public void Dispose(){cancel.Cancel();listener.Stop();try{worker.GetAwaiter().GetResult();}catch(SocketException){}cancel.Dispose();}
}
