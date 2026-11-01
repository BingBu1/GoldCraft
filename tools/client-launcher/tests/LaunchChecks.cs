using GoldCraft.Launcher;
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
    }
}
