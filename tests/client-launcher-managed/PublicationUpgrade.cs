using GoldCraft.Launcher;

static class PublicationUpgrade
{
    public static async Task Run(string output,string exe,string runtime,string priorPublication,string publication,int expectedDownloads)
    {
        string root=SafeFiles.Plain(output);if(Directory.Exists(root))throw new IOException("Use a fresh fixture");Directory.CreateDirectory(root);
        string beforeExe=SafeFiles.Hash(exe);var shipped=ShippedBundle.Read(exe);
        var checks=new List<string>();void Check(bool value,string name){if(!value)throw new Exception(name);checks.Add(name);}
        string game=Path.Combine(root,"game"),work=Path.Combine(root,"work");Directory.CreateDirectory(Path.Combine(game,"cstrike/cl_dlls"));Directory.CreateDirectory(work);
        foreach(string rel in new[]{"hl.exe","hw.dll","cstrike/cl_dlls/client.dll"})File.Copy(SafeFiles.Under(runtime,rel),SafeFiles.Under(game,rel));
        File.WriteAllText(Path.Combine(game,"cstrike/config.cfg"),"keep original bindings");
        var quiet=new Quiet();shipped.bundle.Install(game,work,quiet,CancellationToken.None);
        using var server=new Endpoint();using var updates=new Updates(shipped.bundle.Config,server.Url);
        Release Serve(string folder){
            byte[] signed=File.ReadAllBytes(Path.Combine(folder,"manifest.json"));var release=updates.VerifyRelease(signed,0);server.Files["manifest.json"]=signed;
            foreach(var row in release.Files)server.Files["blobs/"+row.Sha256+"/"+Path.GetFileName(row.Path)]=File.ReadAllBytes(Path.Combine(folder,"blobs",row.Sha256,Path.GetFileName(row.Path)));
            return release;
        }
        var old=Serve(priorPublication);await updates.Apply(game,work,shipped.bundle,quiet,CancellationToken.None);
        Check(shipped.bundle.Installed(game)?.Sequence==old.Sequence,"real preceding signed publication installed");
        int priorReads=server.BlobReads;var next=Serve(publication);await updates.Apply(game,work,shipped.bundle,quiet,CancellationToken.None);
        Check(shipped.bundle.Installed(game)?.Sequence==next.Sequence,"unchanged launcher accepts new real signed plugin release");
        Check(server.BlobReads-priorReads==expectedDownloads,"only changed DLLs downloaded over HTTP");
        foreach(var row in next.Files)SafeFiles.Verify(SafeFiles.Under(game,row.Path),row);
        Check(true,"every released DLL installed with exact SHA and length");
        foreach(var row in next.Resources!)SafeFiles.Verify(SafeFiles.Under(game,row.Path),ReleasePolicy.Asset(row));
        Check(true,"every signed catalog and shader installed with exact SHA and length");
        var plugins=Bundle.ActiveLines(File.ReadAllText(SafeFiles.Under(game,"cstrike/metahook/configs/plugins.lst")));
        Check(plugins.SequenceEqual(next.Plugins!),"signed plugin order installed");
        int once=server.BlobReads;await updates.Apply(game,work,shipped.bundle,quiet,CancellationToken.None);
        Check(server.BlobReads==once,"second launch check does not download unchanged plugin again");
        Check(File.ReadAllText(Path.Combine(game,"cstrike/config.cfg"))=="keep original bindings","player bindings preserved");
        Check(SafeFiles.Hash(exe)==beforeExe,"same delivered launcher EXE remains byte-identical");
        File.WriteAllBytes(Path.Combine(root,"receipt.json"),System.Text.Json.JsonSerializer.SerializeToUtf8Bytes(new{
            passed=true,checks,exeSha256=beforeExe,previousSequence=old.Sequence,sequence=next.Sequence,downloadedDLLs=expectedDownloads,
            coreFiles=next.Files.Length,resources=next.Resources.Length,manifestSha256=SafeFiles.Hash(Path.Combine(publication,"manifest.json")),gameExecuted=false},JsonData.Options));
        Console.WriteLine($"{checks.Count} actual publication upgrade checks passed; unchanged EXE, {expectedDownloads} DLL downloads.");
    }
}
