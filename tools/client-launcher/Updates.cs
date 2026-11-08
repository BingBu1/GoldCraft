using System.Net;
using System.Security.Cryptography;
using System.Text.RegularExpressions;

namespace GoldCraft.Launcher;

public sealed class Updates : IDisposable
{
    private readonly BundleConfig config;
    private readonly Uri endpoint;
    private readonly HttpClient http;
    public Updates(BundleConfig config,string address)
    {
        this.config=config;endpoint=BaseUri(address);
        http=new(new HttpClientHandler { AllowAutoRedirect=false, AutomaticDecompression=DecompressionMethods.None }) {Timeout=TimeSpan.FromSeconds(60)};
    }
    public static Uri BaseUri(string text)
    {
        if(!Uri.TryCreate(text,UriKind.Absolute,out var uri) || uri.Scheme is not ("http" or "https") ||
            uri.UserInfo.Length!=0 || uri.Query.Length!=0 || uri.Fragment.Length!=0 || uri.AbsolutePath!="/goldcraft/")
            throw new InvalidDataException("下载地址应为 http(s)://服务器:端口/goldcraft/。");
        return new Uri(uri,"launcher/v1/");
    }
    public static string GameAddress(string value)
    {
        if(string.IsNullOrEmpty(value)||!Regex.IsMatch(value,@"\A[A-Za-z0-9.-]+:[0-9]{1,5}\z") ||
            !int.TryParse(value[(value.LastIndexOf(':')+1)..],out int port)||port<1||port>65535)
            throw new InvalidDataException("服务器游戏地址无效。");
        return value;
    }
    private async Task<byte[]> Small(string name,int limit,CancellationToken cancel)
    {
        using var timeout=CancellationTokenSource.CreateLinkedTokenSource(cancel);
        timeout.CancelAfter(TimeSpan.FromSeconds(20));cancel=timeout.Token;
        using var response=await http.GetAsync(new Uri(endpoint,name),HttpCompletionOption.ResponseHeadersRead,cancel);
        response.EnsureSuccessStatusCode();
        if(response.Content.Headers.ContentLength>limit)throw new InvalidDataException("服务器响应过大。");
        await using var input=await response.Content.ReadAsStreamAsync(cancel);using var output=new MemoryStream();
        byte[] buffer=new byte[8192];int n;
        while((n=await input.ReadAsync(buffer,cancel))!=0){if(output.Length+n>limit)throw new InvalidDataException("服务器响应过大。");output.Write(buffer,0,n);}
        return output.ToArray();
    }
    public async Task<ServerStatus> Status(CancellationToken cancel)
    {
        var state=JsonData.Read<ServerStatus>(await Small("status.json",16384,cancel));
        if(state.Schema!=1||state.Players<0||state.MaxPlayers<0||state.MaxPlayers>256||state.Players>state.MaxPlayers||
            state.Name==null||state.Name.Length>128||state.Map==null||state.Map.Length>64)throw new InvalidDataException("服务器状态格式无效。");
        return state;
    }
    public Release VerifyRelease(byte[] envelopeBytes,long previous)
    {
        if(envelopeBytes.Length>ReleasePolicy.ManifestLimit)throw new InvalidDataException("更新清单过大。");
        return ReleasePolicy.Verify(config,JsonData.Read<SignedEnvelope>(envelopeBytes),previous);
    }
    public async Task<Release> Apply(string game,string work,Bundle bundle,IProgress<string> progress,CancellationToken cancel)
    {
        SafeFiles.GameStopped(game);
        using var gate=Transactions.Lock(game);Transactions.Recover(game);
        var installed=bundle.ReadState(game)??throw new IOException("请先安装或修复 MetaHook。");
        if(installed.Publication==null&&bundle.Installed(game)==null)throw new IOException("请先安装或修复 MetaHook。");
        progress.Report("正在检查插件及配套文件更新…");
        byte[] envelopeBytes=await Small("manifest.json",ReleasePolicy.ManifestLimit,cancel);
        var envelope=JsonData.Read<SignedEnvelope>(envelopeBytes);
        var release=VerifyRelease(envelopeBytes,installed.Sequence);
        var previous=installed.Publication==null?null:ReleasePolicy.Verify(config,installed.Publication,installed.Sequence);
        if(previous!=null&&release.Schema!=2)throw new InvalidDataException("不能退回旧版更新协议。");
        if(release.Sequence==installed.Sequence && (previous!=null
            ? envelope.Payload!=installed.Publication!.Payload
            : release.Schema!=1||!release.Files.SequenceEqual(installed.CoreFiles)))throw new InvalidDataException("相同版本的更新内容发生变化。");
        bool Different(Asset asset){string file=SafeFiles.Under(game,asset.Path);return !File.Exists(file)||SafeFiles.Hash(file)!=asset.Sha256;}
        var wanted=release.Files.Where(Different).ToArray();
        string stage=SafeFiles.Under(work,"update-"+Guid.NewGuid().ToString("N"));Directory.CreateDirectory(stage);
        try
        {
            var changes=new List<Change>();long done=0,total=wanted.Sum(x=>x.Bytes);
            foreach(var asset in wanted)
            {
                string file=SafeFiles.Under(stage,asset.Path);Directory.CreateDirectory(Path.GetDirectoryName(file)!);
                string route="blobs/"+asset.Sha256+"/"+Path.GetFileName(asset.Path);
                await CoreDownload.Fetch(http,new Uri(endpoint,route),file,asset,
                    size=>progress.Report($"正在下载核心插件 {(done+size)/1048576.0:0.0} / {total/1048576.0:0.0} MB"),
                    ()=>progress.Report("下载内容异常，正在自动分段重试…"),cancel);
                done+=asset.Bytes;changes.Add(new(asset.Path,file));
            }
            if(release.Schema==2)
            {
                foreach(var resource in release.Resources!)
                {
                    if(!Different(ReleasePolicy.Asset(resource)))continue;
                    string file=SafeFiles.Under(stage,resource.Path);Directory.CreateDirectory(Path.GetDirectoryName(file)!);
                    File.WriteAllBytes(file,ReleasePolicy.Decode(resource));changes.Add(new(resource.Path,file));
                }
                var next=ReleasePolicy.Owned(config,release).Select(x=>x.Path).ToHashSet(StringComparer.OrdinalIgnoreCase);
                foreach(var old in ReleasePolicy.Owned(config,previous))
                {
                    if(next.Contains(old.Path))continue;
                    string file=SafeFiles.Under(game,old.Path);
                    if(!File.Exists(file))continue;
                    SafeFiles.Verify(file,old);changes.Add(new(old.Path,null));
                }
                string rel="cstrike/metahook/configs/plugins.lst",path=SafeFiles.Under(game,rel);
                string text=File.Exists(path)?File.ReadAllText(path):"";
                var retired=(previous?.Plugins??config.Plugins).Except(release.Plugins!,StringComparer.OrdinalIgnoreCase).ToHashSet(StringComparer.OrdinalIgnoreCase);
                text=string.Join("\n",text.Split('\n').Where(x=>!retired.Contains(x.Split("//")[0].Trim())));
                string merged=Bundle.MergePlugins(text,release.Plugins!);
                if(!File.Exists(path)||File.ReadAllText(path)!=merged){string file=SafeFiles.Under(stage,"plugins.lst");File.WriteAllText(file,merged);changes.Add(new(rel,file));}
            }
            var state=new InstallState(config.BundleId,release.Sequence,release.Version,release.Files,release.Schema==2?envelope:null);
            string stateFile=SafeFiles.Under(stage,"installed.json");File.WriteAllBytes(stateFile,JsonData.Write(state));
            if(changes.Count>0||installed.Sequence!=release.Sequence)changes.Add(new(".goldcraft-launcher/installed.json",stateFile));
            cancel.ThrowIfCancellationRequested();SafeFiles.GameStopped(game);
            if(changes.Count>0){progress.Report("正在应用更新…");Transactions.Apply(game,changes);}
            if(bundle.Installed(game)==null)throw new IOException("组件校验未通过，请点击安装 MetaHook 修复后重试。");
            progress.Report("插件及配套文件已是最新版本。");return release;
        }
        finally{SafeFiles.RemoveTree(work,stage);}
    }
    public void Dispose()=>http.Dispose();
}
