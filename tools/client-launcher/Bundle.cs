using System.IO.Compression;
using System.Reflection;
using System.Text;

namespace GoldCraft.Launcher;

public sealed class Bundle
{
    public BundleConfig Config { get; }
    private readonly Func<Stream> payload;
    public Bundle(BundleConfig config, Func<Stream> payload) { Config = config; this.payload = payload; }
    public static Bundle Embedded()
    {
        var asm = Assembly.GetExecutingAssembly();
        using var data = asm.GetManifestResourceStream("bundle.json") ?? throw new IOException("缺少内置安装清单。");
        using var copy = new MemoryStream(); data.CopyTo(copy);
        return new(JsonData.Read<BundleConfig>(copy.ToArray()), () => asm.GetManifestResourceStream("payload.zip") ?? throw new IOException("缺少内置安装文件。"));
    }
    public void ValidateGame(string game)
    {
        SafeFiles.Plain(game);
        if (!File.Exists(SafeFiles.Under(game,"hl.exe")) || !Directory.Exists(SafeFiles.Under(game,"cstrike")))
            throw new IOException("请选择包含 hl.exe 和 cstrike 文件夹的 CS 1.6 目录。");
        if (SafeFiles.Hash(SafeFiles.Under(game,"hw.dll")) != Config.EngineSha256 ||
            SafeFiles.Hash(SafeFiles.Under(game,"cstrike/cl_dlls/client.dll")) != Config.ClientSha256)
            throw new IOException("这个 CS 版本与插件不兼容。请选择受支持的 Steam CS 1.6 安装；游戏文件未改动。");
    }
    public InstallState? Installed(string game)
    {
        if (string.IsNullOrWhiteSpace(game)) return null;
        try
        {
            ValidateGame(game);
            var path = SafeFiles.Under(game,".goldcraft-launcher/installed.json");
            if (!File.Exists(path)) return null;
            var state = JsonData.Read<InstallState>(File.ReadAllBytes(path));
            if (state.BundleId != Config.BundleId || state.CoreFiles == null || state.CoreFiles.Any(x=>x==null||string.IsNullOrEmpty(x.Path)) ||
                !state.CoreFiles.Select(x=>x.Path).ToHashSet(StringComparer.OrdinalIgnoreCase).SetEquals(Config.CorePaths)) return null;
            var files = Config.Files.Where(x => !Config.CorePaths.Contains(x.Path, StringComparer.OrdinalIgnoreCase)).Concat(state.CoreFiles);
            foreach (var row in files) SafeFiles.Verify(SafeFiles.Under(game,row.Path),row);
            string[] plugins = ActiveLines(File.ReadAllText(SafeFiles.Under(game,"cstrike/metahook/configs/plugins.lst")));
            int last = -1;
            foreach (var plugin in Config.Plugins)
            {
                int index = Array.FindIndex(plugins, x => x.Equals(plugin,StringComparison.OrdinalIgnoreCase));
                if (index <= last) return null;
                last = index;
            }
            var paths=ActiveLines(File.ReadAllText(SafeFiles.Under(game,"cstrike/metahook/configs/dllpaths.lst"))).ToHashSet(StringComparer.OrdinalIgnoreCase);
            if(!Config.DllPaths.All(paths.Contains))return null;
            if(ObsoleteAliases(game).Any())return null;
            return state;
        }
        catch (Exception ex) when (ex is IOException or InvalidDataException or UnauthorizedAccessException or System.Text.Json.JsonException or ArgumentException) { return null; }
    }
    private IEnumerable<string> ObsoleteAliases(string game)
    {
        var supplied=Config.Files.Select(x=>x.Path).ToHashSet(StringComparer.OrdinalIgnoreCase);
        var candidates=new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach(var plugin in Config.Plugins)
        foreach(var stem in new[]{PluginKey(plugin)[..^4],plugin[..^4]}.Distinct(StringComparer.OrdinalIgnoreCase))
        foreach(var suffix in new[]{".dll","_AVX2.dll","_AVX.dll","_SSE2.dll","_SSE.dll"})
        {
            string rel="cstrike/metahook/plugins/"+stem+suffix;
            if(!supplied.Contains(rel)&&candidates.Add(rel)&&File.Exists(SafeFiles.Under(game,rel)))yield return rel;
        }
    }
    internal static string[] ActiveLines(string text) => text.Split('\n').Select(x=>x.Split("//")[0].Trim())
        .Where(x=>x.Length>0 && !x.StartsWith(';') && !x.StartsWith('#')).ToArray();
    private static string PluginKey(string name) => System.Text.RegularExpressions.Regex.Replace(name, @"(?:_(?:AVX2|AVX|SSE2|SSE))+(?=\.dll$)", "", System.Text.RegularExpressions.RegexOptions.IgnoreCase);
    public static string MergePlugins(string text, string[] required)
    {
        var keys = required.Select(PluginKey).ToHashSet(StringComparer.OrdinalIgnoreCase);
        var lines = text.Split('\n').Select(x=>x.TrimEnd('\r')).ToList();
        int insertion = lines.FindIndex(x => keys.Contains(PluginKey(x.Split("//")[0].Trim())));
        if (insertion < 0) insertion = lines.Count;
        var kept = new List<string>(); int where = 0;
        for (int i=0;i<lines.Count;i++)
        {
            if (i==insertion) where=kept.Count;
            if (!keys.Contains(PluginKey(lines[i].Split("//")[0].Trim()))) kept.Add(lines[i]);
        }
        if (insertion==lines.Count) where=kept.Count;
        // MetaAudio must initialize before CaptionMod, as in the existing installer.
        var audio=kept.Where(x=>PluginKey(x.Split("//")[0].Trim()).Equals("MetaAudio.dll",StringComparison.OrdinalIgnoreCase)).ToArray();
        foreach(var item in audio) { int i=kept.IndexOf(item);kept.RemoveAt(i);if(i<where)where--; }
        kept.InsertRange(where,audio.Concat(required));
        return string.Join("\r\n",kept).TrimEnd('\r','\n')+"\r\n";
    }
    public void Install(string game, string work, IProgress<string> progress, CancellationToken cancel)
    {
        ValidateGame(game); SafeFiles.GameStopped(game);
        using var gate=Transactions.Lock(game); Transactions.Recover(game);
        if (Installed(game) != null) { progress.Report("MetaHook 已安装，无需重复安装。"); return; }
        string stage=SafeFiles.Under(work,"install-"+Guid.NewGuid().ToString("N"));Directory.CreateDirectory(stage);
        try
        {
            using(var stream=payload())
            using(var zip=new ZipArchive(stream,ZipArchiveMode.Read))
            {
                var expected=Config.Files.ToDictionary(x=>x.Path,StringComparer.OrdinalIgnoreCase);
                var seen=new HashSet<string>(StringComparer.OrdinalIgnoreCase);
                foreach(var entry in zip.Entries)
                {
                    cancel.ThrowIfCancellationRequested();
                    if(!expected.TryGetValue(entry.FullName,out var asset) || !seen.Add(entry.FullName) || entry.Length!=asset.Bytes)
                        throw new InvalidDataException("内置安装包清单不一致。");
                    string file=SafeFiles.Under(stage,entry.FullName); Directory.CreateDirectory(Path.GetDirectoryName(file)!);
                    using(var input=entry.Open())using(var output=new FileStream(file,FileMode.CreateNew))input.CopyTo(output);
                    SafeFiles.Verify(file,asset);
                    progress.Report($"正在准备安装文件 {seen.Count}/{expected.Count}");
                }
                if(seen.Count!=expected.Count)throw new InvalidDataException("内置安装包缺少文件。");
            }
            var changes=Config.Files.Select(x=>new Change(x.Path,SafeFiles.Under(stage,x.Path))).ToList();
            string plugins="cstrike/metahook/configs/plugins.lst", paths="cstrike/metahook/configs/dllpaths.lst";
            string ReadExisting(string rel)=>File.Exists(SafeFiles.Under(game,rel))?File.ReadAllText(SafeFiles.Under(game,rel)):"";
            File.WriteAllText(SafeFiles.Under(stage,"plugins.lst"),MergePlugins(ReadExisting(plugins),Config.Plugins),new UTF8Encoding(false));
            var existing=ReadExisting(paths);var active=ActiveLines(existing).ToHashSet(StringComparer.OrdinalIgnoreCase);
            File.WriteAllText(SafeFiles.Under(stage,"dllpaths.lst"),existing.TrimEnd()+"\r\n"+string.Join("\r\n",Config.DllPaths.Where(x=>!active.Contains(x)))+"\r\n",new UTF8Encoding(false));
            changes.Add(new(plugins,SafeFiles.Under(stage,"plugins.lst")));changes.Add(new(paths,SafeFiles.Under(stage,"dllpaths.lst")));
            // Retire only CPU aliases of our plugins; do not edit unrelated addons or CS bindings.
            foreach(string rel in ObsoleteAliases(game))changes.Add(new(rel,null));
            var state=new InstallState(Config.BundleId,0,Config.Version,Config.Files.Where(x=>Config.CorePaths.Contains(x.Path,StringComparer.OrdinalIgnoreCase)).ToArray());
            string stateFile=SafeFiles.Under(stage,"installed.json");File.WriteAllBytes(stateFile,JsonData.Write(state));
            changes.Add(new(".goldcraft-launcher/installed.json",stateFile));
            cancel.ThrowIfCancellationRequested();progress.Report("正在安装 MetaHook，请稍候…");
            Transactions.Apply(game,changes);
        }
        finally {SafeFiles.RemoveTree(work,stage);}
    }
}
