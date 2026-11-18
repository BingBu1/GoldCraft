using GoldCraft.Launcher;

static class LiveEndpoint
{
    public static async Task Run(string root,string exe,string previousExe,string runtime)
    {
        root=SafeFiles.Plain(root);
        if(Directory.Exists(root))throw new IOException("Use a fresh fixture");
        Directory.CreateDirectory(root);
        var shipped=ShippedBundle.Read(exe);
        var previous=ShippedBundle.Read(previousExe);
        var cfg=shipped.bundle.Config;
        var checks=new List<string>();
        void Check(bool value,string name){if(!value)throw new Exception(name);checks.Add(name);}
        Check(JsonData.Write(cfg with{Version=previous.bundle.Config.Version,UpdateBase=previous.bundle.Config.UpdateBase})
            .SequenceEqual(JsonData.Write(previous.bundle.Config)),"only embedded endpoint and display version changed");
        Check(shipped.payload.SequenceEqual(previous.payload),"embedded installer payload byte-identical");
        string game=Path.Combine(root,"game"),work=Path.Combine(root,"work");
        Directory.CreateDirectory(Path.Combine(game,"cstrike/cl_dlls"));Directory.CreateDirectory(work);
        foreach(string rel in new[]{"hl.exe","hw.dll","cstrike/cl_dlls/client.dll"})
            File.Copy(SafeFiles.Under(runtime,rel),SafeFiles.Under(game,rel));
        File.WriteAllText(Path.Combine(game,"cstrike/config.cfg"),"preserve player bindings");
        var quiet=new Quiet();
        // Reproduce an existing installation made by the previously issued EXE.
        previous.bundle.Install(game,work,quiet,CancellationToken.None);
        Check(shipped.bundle.Installed(game)!=null,"existing launcher installation accepted without reinstall");
        using var updates=new Updates(cfg,cfg.UpdateBase);
        using var timeout=new CancellationTokenSource(TimeSpan.FromMinutes(3));
        var status=await updates.Status(timeout.Token);
        Check(status.Online,"actual production HTTP reports server online");
        var release=await updates.Apply(game,work,shipped.bundle,quiet,timeout.Token);
        Check(shipped.bundle.Installed(game)?.Sequence==release.Sequence,"actual signed HTTP update installed and launchable");
        foreach(var file in release.Files)SafeFiles.Verify(SafeFiles.Under(game,file.Path),file);
        foreach(var file in release.Resources!)SafeFiles.Verify(SafeFiles.Under(game,file.Path),ReleasePolicy.Asset(file));
        Check(true,"all production DLLs and signed resources verified after download");
        Check(File.ReadAllText(Path.Combine(game,"cstrike/config.cfg"))=="preserve player bindings","player bindings preserved");
        string address=await GameLaunch.ResolveAddress(release.GameAddress,timeout.Token);
        Check(GameLaunch.StartInfo(game,address).ArgumentList.Contains(address),"signed game address resolves into launch arguments");
        File.WriteAllBytes(Path.Combine(root,"receipt.json"),System.Text.Json.JsonSerializer.SerializeToUtf8Bytes(new{
            passed=true,checks,version=cfg.Version,sequence=release.Sequence,server=status.Name,map=status.Map,
            coreFiles=release.Files.Length,resources=release.Resources.Length,exeSha256=SafeFiles.Hash(exe),
            gameExecuted=false,productionModified=false,protectedInstallationModified=false},JsonData.Options));
        Console.WriteLine($"{checks.Count} live HTTP endpoint checks passed; version {cfg.Version}, signed sequence {release.Sequence}.");
    }
}
