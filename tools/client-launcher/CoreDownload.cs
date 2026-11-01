using System.Net;
using System.Net.Http.Headers;

namespace GoldCraft.Launcher;

public static class CoreDownload
{
    private const int ChunkBytes=512*1024;

    public static async Task Fetch(HttpClient http,Uri uri,string file,Asset asset,
        Action<long> progress,Action retry,CancellationToken cancel)
    {
        using var deadline=CancellationTokenSource.CreateLinkedTokenSource(cancel);
        deadline.CancelAfter(TimeSpan.FromMinutes(3));
        try
        {
            await Full(http,uri,file,asset,progress,deadline.Token);
            SafeFiles.Verify(file,asset);
            return;
        }
        catch(InvalidDataException)
        {
            // Some public TCP relays return a full-length response with a damaged
            // tail. Retry once with bounded ranges; never accept a different hash.
            retry();
        }
        await Ranges(http,uri,file,asset,progress,deadline.Token);
        SafeFiles.Verify(file,asset);
    }

    private static async Task Full(HttpClient http,Uri uri,string file,Asset asset,Action<long> progress,CancellationToken cancel)
    {
        using var request=new HttpRequestMessage(HttpMethod.Get,uri);
        request.Headers.CacheControl=new CacheControlHeaderValue{NoCache=true};
        using var response=await http.SendAsync(request,HttpCompletionOption.ResponseHeadersRead,cancel);
        response.EnsureSuccessStatusCode();
        if(response.StatusCode!=HttpStatusCode.OK)throw new InvalidDataException("下载响应不完整。");
        await using var output=new FileStream(file,FileMode.CreateNew,FileAccess.Write,FileShare.None);
        await Copy(response,output,asset.Bytes,progress,cancel);
    }

    private static async Task Ranges(HttpClient http,Uri uri,string file,Asset asset,Action<long> progress,CancellationToken cancel)
    {
        await using var output=new FileStream(file,FileMode.Create,FileAccess.Write,FileShare.None);
        for(long start=0;start<asset.Bytes;start+=ChunkBytes)
        {
            long first=start,last=Math.Min(start+ChunkBytes,asset.Bytes)-1;
            using var request=new HttpRequestMessage(HttpMethod.Get,uri);
            request.Headers.Range=new RangeHeaderValue(first,last);
            request.Headers.CacheControl=new CacheControlHeaderValue{NoCache=true};
            using var response=await http.SendAsync(request,HttpCompletionOption.ResponseHeadersRead,cancel);
            response.EnsureSuccessStatusCode();
            var range=response.Content.Headers.ContentRange;
            if(response.StatusCode!=HttpStatusCode.PartialContent || range==null || range.Unit!="bytes" ||
                range.From!=first || range.To!=last || range.Length!=asset.Bytes)
                throw new InvalidDataException("更新服务返回了不正确的文件分段，请联系服主。");
            await Copy(response,output,last-first+1,n=>progress(first+n),cancel);
        }
    }

    private static async Task Copy(HttpResponseMessage response,Stream output,long expected,Action<long> progress,CancellationToken cancel)
    {
        if(response.Content.Headers.ContentLength is long length && length!=expected)
            throw new InvalidDataException("下载长度不匹配。");
        await using var input=await response.Content.ReadAsStreamAsync(cancel);
        byte[] buffer=new byte[65536];long size=0;int count;
        while((count=await input.ReadAsync(buffer,cancel))>0)
        {
            size+=count;if(size>expected)throw new InvalidDataException("下载超过清单长度。");
            await output.WriteAsync(buffer.AsMemory(0,count),cancel);progress(size);
        }
        if(size!=expected)throw new InvalidDataException("下载文件不完整。");
    }
}
