using Microsoft.Win32;
using System.Text.RegularExpressions;

namespace GoldCraft.Launcher;

public static partial class SteamDiscovery
{
    public static IReadOnlyList<string> Find()
    {
        var libraries=new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach(var view in new[]{RegistryView.Registry32,RegistryView.Registry64})
        foreach(var hive in new[]{RegistryHive.CurrentUser,RegistryHive.LocalMachine})
        {
            using var root=RegistryKey.OpenBaseKey(hive,view);
            using var key=root.OpenSubKey(@"Software\Valve\Steam");
            foreach(var name in new[]{"SteamPath","InstallPath"})
                if(key?.GetValue(name) is string path && Directory.Exists(path))libraries.Add(path);
        }
        libraries.Add(Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86),"Steam"));
        foreach(var steam in libraries.ToArray())
        {
            string file=Path.Combine(steam,"steamapps","libraryfolders.vdf");
            if(!File.Exists(file))continue;
            foreach(Match m in LibraryPath().Matches(File.ReadAllText(file)))
                libraries.Add(m.Groups[1].Value.Replace(@"\\",@"\"));
        }
        var found=new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach(var library in libraries)
        {
            string common=Path.Combine(library,"steamapps","common");
            string manifest=Path.Combine(library,"steamapps","appmanifest_10.acf");
            if(File.Exists(manifest))
            {
                var match=InstallDir().Match(File.ReadAllText(manifest));
                if(match.Success)Add(Path.Combine(common,match.Groups[1].Value));
            }
            Add(Path.Combine(common,"Half-Life"));
        }
        return found.OrderBy(x=>x,StringComparer.OrdinalIgnoreCase).ToArray();
        void Add(string p)
        {
            if(File.Exists(Path.Combine(p,"hl.exe"))&&File.Exists(Path.Combine(p,"hw.dll"))&&Directory.Exists(Path.Combine(p,"cstrike")))
                try{found.Add(SafeFiles.Plain(p));}catch(IOException){}
        }
    }
    [GeneratedRegex("\"path\"\\s*\"([^\"]+)\"",RegexOptions.IgnoreCase)] private static partial Regex LibraryPath();
    [GeneratedRegex("\"installdir\"\\s*\"([^\"]+)\"",RegexOptions.IgnoreCase)] private static partial Regex InstallDir();
}
