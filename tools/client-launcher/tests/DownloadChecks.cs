using GoldCraft.Launcher;
using System.Net;
using System.Net.Http.Headers;
using System.Security.Cryptography;

static class DownloadChecks
{
    public static async Task Run(Action<bool,string> check,string root)
    {
        byte[] bytes=new byte[1100*1024];new Random(412).NextBytes(bytes);
        var asset=new Asset("cstrike/metahook/plugins/CSOLSupport.dll",bytes.Length,Convert.ToHexStringLower(SHA256.HashData(bytes)));
        var uri=new Uri("http://fixture.invalid/core.dll");
        foreach(string scenario in new[]{"valid","damaged-full","wrong-range","ignored-range","damaged-range"})
        {
            using var handler=new RangeFixture(bytes,scenario);using var http=new HttpClient(handler);
            string file=Path.Combine(root,scenario+".dll");bool retried=false,rejected=false;
            try{await CoreDownload.Fetch(http,uri,file,asset,_=>{},()=>retried=true,CancellationToken.None);}
            catch(InvalidDataException){rejected=true;}
            if(scenario=="valid")check(!retried&&!rejected&&handler.Ranges.Count==0,"valid full body uses one request");
            else if(scenario=="damaged-full")
            {
                check(retried&&!rejected&&SafeFiles.Hash(file)==asset.Sha256,"full-length corrupted body recovered with final SHA-256 intact");
                check(handler.Ranges.SequenceEqual(new[]{(0L,524287L),(524288L,1048575L),(1048576L,(long)bytes.Length-1)}),"ranges cover the whole file exactly once in bounded chunks");
            }
            else check(retried&&rejected,"reject "+scenario+" without accepting bytes");
        }
        using var cancelled=new CancellationTokenSource();cancelled.Cancel();
        using var cancellationHttp=new HttpClient(new RangeFixture(bytes,"valid"));
        bool stopped=false;
        try{await CoreDownload.Fetch(cancellationHttp,uri,Path.Combine(root,"cancelled.dll"),asset,_=>{},()=>{},cancelled.Token);}
        catch(OperationCanceledException){stopped=true;}
        check(stopped,"cancelled download never turns into range retry");
    }

    public static async Task Live(string configFile,string manifestFile,string output)
    {
        var config=JsonData.Read<BundleConfig>(File.ReadAllBytes(configFile));
        using var updates=new Updates(config,config.UpdateBase);
        var release=updates.VerifyRelease(File.ReadAllBytes(manifestFile),0);
        var asset=release.Files.Single(x=>x.Path.EndsWith("/CSOLSupport.dll",StringComparison.Ordinal));
        using var http=new HttpClient(new HttpClientHandler{AllowAutoRedirect=false,AutomaticDecompression=DecompressionMethods.None});
        var uri=new Uri(Updates.BaseUri(config.UpdateBase),"blobs/"+asset.Sha256+"/CSOLSupport.dll");
        bool retried=false;
        await CoreDownload.Fetch(http,uri,output,asset,_=>{},()=>retried=true,CancellationToken.None);
        Console.WriteLine(System.Text.Json.JsonSerializer.Serialize(new{sequence=release.Sequence,bytes=new FileInfo(output).Length,sha256=SafeFiles.Hash(output),rangedRetry=retried,signatureVerified=true}));
    }

    private sealed class RangeFixture(byte[] bytes,string scenario):HttpMessageHandler
    {
        public readonly List<(long,long)> Ranges=new();
        protected override Task<HttpResponseMessage> SendAsync(HttpRequestMessage request,CancellationToken cancel)
        {
            cancel.ThrowIfCancellationRequested();
            var range=request.Headers.Range?.Ranges.Single();
            if(range==null || scenario=="ignored-range")
            {
                var body=bytes.ToArray();if(scenario!="valid")body[^1]^=0x42;
                return Task.FromResult(new HttpResponseMessage(HttpStatusCode.OK){Content=new ByteArrayContent(body)});
            }
            long first=range.From!.Value,last=range.To!.Value;Ranges.Add((first,last));
            var part=bytes[(int)first..((int)last+1)];if(scenario=="damaged-range")part[^1]^=0x42;
            var content=new ByteArrayContent(part);
            content.Headers.ContentRange=new ContentRangeHeaderValue(first+(scenario=="wrong-range"?1:0),last,bytes.Length);
            return Task.FromResult(new HttpResponseMessage(HttpStatusCode.PartialContent){Content=content});
        }
    }
}
