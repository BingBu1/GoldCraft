using GoldCraft.Launcher;
using System.ComponentModel;
using System.Net;
using System.Net.Sockets;

static class LaunchChecks
{
    public static async Task Run(Action<bool,string> check,string root)
    {
        int calls=0;
        Task<IPAddress[]> Resolve(string host,CancellationToken cancel)
        {
            calls++;check(host=="tunnel-node.example.test","signed game hostname reaches the resolver unchanged");
            return Task.FromResult(new[]{IPAddress.IPv6Loopback,IPAddress.Parse(calls==1?"192.0.2.1":"192.0.2.2")});
        }
        string first=await GameLaunch.ResolveAddress("tunnel-node.example.test:46344",Resolve,CancellationToken.None);
        check(first=="192.0.2.1:46344","hyphenated hostname becomes IPv4 with its game port preserved");
        var info=GameLaunch.StartInfo(root,first);
        check(info.ArgumentList.SequenceEqual(new[]{"-game","cstrike","-insecure","+connect","192.0.2.1:46344"})&&
              !info.UseShellExecute&&info.WorkingDirectory==root&&info.FileName==Path.Combine(root,"MetaHook.exe"),
              "actual process arguments pass exactly one numeric connect target without a shell");
        check(await GameLaunch.ResolveAddress("tunnel-node.example.test:46344",Resolve,CancellationToken.None)=="192.0.2.2:46344",
              "later launch resolves DNS again after the tunnel address changes");
        check(await GameLaunch.ResolveAddress("192.0.2.9:27015",Resolve,CancellationToken.None)=="192.0.2.9:27015"&&calls==2,
              "numeric addresses retain their explicit game port without DNS");
        foreach(string invalid in new[]{"host:1234\n","host:1234 +quit","host:0","host:65536"})
        {
            bool rejected=false;
            try{await GameLaunch.ResolveAddress(invalid,Resolve,CancellationToken.None);}catch(InvalidDataException){rejected=true;}
            check(rejected&&calls==2,"invalid launch target rejected before DNS");
        }
        bool hostnameRejected=false;
        try{GameLaunch.StartInfo(root,"tunnel-node.example.test:46344");}catch(InvalidDataException){hostnameRejected=true;}
        check(hostnameRejected,"unresolved hostname cannot leak into engine command-line arguments");
        bool emptyRejected=false;
        try{await GameLaunch.ResolveAddress("test:27015",(_,_)=>Task.FromResult(new[]{IPAddress.IPv6Loopback}),CancellationToken.None);}
        catch(IOException){emptyRejected=true;}
        check(emptyRejected,"IPv6-only result blocks launch instead of changing ports or connecting elsewhere");
        bool failed=false;
        try{await GameLaunch.ResolveAddress("test:27015",(_,_)=>Task.FromException<IPAddress[]>(new SocketException()),CancellationToken.None);}
        catch(IOException ex){failed=!ex.Message.Contains("test:27015");}
        check(failed,"DNS failure produces a bounded user message and no launch target");
        using var cancel=new CancellationTokenSource();
        var pending=GameLaunch.ResolveAddress("test:27015",(_,_)=>new TaskCompletionSource<IPAddress[]>().Task,cancel.Token);
        cancel.Cancel();bool cancelled=false;
        try{await pending;}catch(OperationCanceledException){cancelled=true;}
        check(cancelled,"cancellation ends a pending resolver before launch");
        WindowsStart(check, root, first);
    }

    private static void WindowsStart(Action<bool,string> check, string root, string address)
    {
        string game = Path.Combine(root, "中文 Steam Library", "Half-Life");
        Directory.CreateDirectory(game);
        string exe = Path.Combine(game, "MetaHook.exe");
        int attempts = 0;
        GameLaunch.Start(game, address, info =>
        {
            attempts++;
            check(info.FileName == exe && info.WorkingDirectory == game &&
                info.ArgumentList.SequenceEqual(new[] { "-game", "cstrike", "-insecure", "+connect", address }),
                "process target, game directory and arguments survive the Windows handoff");
            if (!info.UseShellExecute) throw new Win32Exception(740);
            check(info.Verb == "open", "Windows uses the game's ordinary open verb for consent");
        });
        check(attempts == 2, "only an elevation-required result retries the fixed executable");

        attempts = 0;
        try
        {
            GameLaunch.Start(game, address, info =>
            {
                attempts++;
                throw new Win32Exception(info.UseShellExecute ? 1223 : 740);
            });
            throw new Exception("cancelled Windows confirmation reported success");
        }
        catch (Win32Exception error) when (error.NativeErrorCode == 1223)
        {
            string message = UserMessages.Error(error, "", LauncherStep.Start);
            check(attempts == 2 && message.Contains("1223") && message.Contains("已取消"),
                "declining Windows consent reports cancellation and does not retry");
        }

        File.WriteAllText(exe, "fixture has no executable image");
        try
        {
            GameLaunch.Start(game, address);
            throw new Exception("Windows accepted an invalid image");
        }
        catch (Win32Exception error) when (error.NativeErrorCode is 193 or 216)
        {
            string message = UserMessages.Error(error, "", LauncherStep.Start);
            check(message.Contains("启动 MetaHook") && message.Contains($"Windows {error.NativeErrorCode}"),
                "actual Windows process failure at a valid game path displays its phase and native code");
        }
    }
}
