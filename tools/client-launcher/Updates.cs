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
        if(string.IsNullOrEmpty(value)||!Regex.IsMatch(value,@"^[A-Za-z0-9.-]+:[0-9]{1,5}$") ||
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
        var envelope=JsonData.Read<SignedEnvelope>(envelopeBytes);
        if(string.IsNullOrEmpty(envelope.Payload)||string.IsNullOrEmpty(envelope.Signature))throw new InvalidDataException("更新签名文件不完整。");
        byte[] payload=Convert.FromBase64String(envelope.Payload),signature=Convert.FromBase64String(envelope.Signature);
        using var rsa=RSA.Create();rsa.ImportFromPem(config.PublicKeyPem);
        if(!rsa.VerifyData(payload,signature,HashAlgorithmName.SHA256,RSASignaturePadding.Pkcs1))throw new InvalidDataException("更新签名不正确，已停止更新。");
        var release=JsonData.Read<Release>(payload);
        if(release.Schema!=1||release.BundleId!=config.BundleId)throw new InvalidDataException("服务器需要新版启动器，请获取新的单文件启动器。");
        if(release.Sequence<previous||release.Sequence<1)throw new InvalidDataException("服务器提供了过期版本，已停止更新。");
        if(string.IsNullOrWhiteSpace(release.Version)||release.Version.Length>80||release.Files==null||release.Files.Length>40)throw new InvalidDataException("更新清单无效。");
        GameAddress(release.GameAddress);
        var names=new HashSet<string>(StringComparer.OrdinalIgnoreCase);long total=0;
        foreach(var row in release.Files)
        {
            if(row==null||string.IsNullOrEmpty(row.Path)||!config.CorePaths.Contains(row.Path,StringComparer.OrdinalIgnoreCase)||!row.Path.EndsWith(".dll",StringComparison.OrdinalIgnoreCase)||
                !names.Add(row.Path)||row.Bytes<=0||row.Bytes>16*1024*1024-1||!Regex.IsMatch(row.Sha256??"",@"^[0-9a-f]{64}$"))throw new InvalidDataException("更新清单包含不允许的文件。");
            total+=row.Bytes;
        }
        if(total>256*1024*1024||!names.SetEquals(config.CorePaths))throw new InvalidDataException("核心文件配套不完整。");
        return release;
    }
    public async Task<Release> Apply(string game,string work,Bundle bundle,IProgress<string> progress,CancellationToken cancel)
    {
        SafeFiles.GameStopped(game);
        using var gate=Transactions.Lock(game);Transactions.Recover(game);
        var installed=bundle.Installed(game)??throw new IOException("请先安装或修复 MetaHook。");
        progress.Report("正在检查核心插件更新…");
        var release=VerifyRelease(await Small("manifest.json",1024*1024,cancel),installed.Sequence);
        if(release.Sequence==installed.Sequence && release.Files.Any(x=>!installed.CoreFiles.Any(y=>x.Path==y.Path&&x.Sha256==y.Sha256)))throw new InvalidDataException("相同版本的更新内容发生变化。");
        var wanted=release.Files.Where(x=>SafeFiles.Hash(SafeFiles.Under(game,x.Path))!=x.Sha256).ToArray();
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
            var state=new InstallState(config.BundleId,release.Sequence,release.Version,release.Files);
            string stateFile=SafeFiles.Under(stage,"installed.json");File.WriteAllBytes(stateFile,JsonData.Write(state));
            if(changes.Count>0||installed.Sequence!=release.Sequence)changes.Add(new(".goldcraft-launcher/installed.json",stateFile));
            cancel.ThrowIfCancellationRequested();SafeFiles.GameStopped(game);
            if(changes.Count>0){progress.Report("正在应用更新…");Transactions.Apply(game,changes);}
            progress.Report("核心插件已是最新版本。");return release;
        }
        finally{SafeFiles.RemoveTree(work,stage);}
    }
    public void Dispose()=>http.Dispose();
}
