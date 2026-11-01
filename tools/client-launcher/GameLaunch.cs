using System.Diagnostics;
using System.Net;
using System.Net.Sockets;

namespace GoldCraft.Launcher;

public static class GameLaunch
{
    public static Task<string> ResolveAddress(string address,CancellationToken cancel) =>
        ResolveAddress(address,(host,token)=>Dns.GetHostAddressesAsync(host,AddressFamily.InterNetwork,token),cancel);

    internal static async Task<string> ResolveAddress(string address,
        Func<string,CancellationToken,Task<IPAddress[]>> resolve,CancellationToken cancel)
    {
        Updates.GameAddress(address);
        cancel.ThrowIfCancellationRequested();
        int separator=address.LastIndexOf(':');
        string host=address[..separator],port=address[(separator+1)..];
        if(IPAddress.TryParse(host,out var numeric)&&numeric.AddressFamily==AddressFamily.InterNetwork)
            return numeric+":"+port;
        using var timeout=CancellationTokenSource.CreateLinkedTokenSource(cancel);
        timeout.CancelAfter(TimeSpan.FromSeconds(8));
        try
        {
            var addresses=await resolve(host,timeout.Token).WaitAsync(timeout.Token);
            var ipv4=addresses.FirstOrDefault(ip=>ip.AddressFamily==AddressFamily.InterNetwork);
            if(ipv4==null)throw new IOException("游戏服务器没有可用的 IPv4 地址，请稍后重试。");
            return ipv4+":"+port;
        }
        catch(SocketException)
        {
            throw new IOException("暂时无法解析游戏服务器地址，请检查网络后重试。");
        }
    }

    public static ProcessStartInfo StartInfo(string game,string resolvedAddress)
    {
        Updates.GameAddress(resolvedAddress);
        string host=resolvedAddress[..resolvedAddress.LastIndexOf(':')];
        if(!IPAddress.TryParse(host,out var ip)||ip.AddressFamily!=AddressFamily.InterNetwork)
            throw new InvalidDataException("启动前需要解析游戏服务器地址。");
        var info=new ProcessStartInfo(SafeFiles.Under(game,"MetaHook.exe")){WorkingDirectory=game,UseShellExecute=false};
        // Feed GoldSrc a numeric endpoint, matching a server-list connection.
        // Resolve every launch so tunnel DNS changes do not pin an obsolete IP.
        foreach(string arg in new[]{"-game","cstrike","-insecure","+connect",resolvedAddress})info.ArgumentList.Add(arg);
        return info;
    }
}
