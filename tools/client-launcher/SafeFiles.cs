using System.Diagnostics;
using System.Security.Cryptography;

namespace GoldCraft.Launcher;

public static class SafeFiles
{
    public static string Plain(string path)
    {
        string full = Path.GetFullPath(path);
        for (string? p = full; p != null; p = Path.GetDirectoryName(p))
            if ((File.Exists(p) || Directory.Exists(p)) && (File.GetAttributes(p) & FileAttributes.ReparsePoint) != 0)
                throw new IOException("目录包含链接，请选择普通游戏文件夹：" + p);
        return full;
    }
    public static string Under(string root, string name)
    {
        if (string.IsNullOrWhiteSpace(name) || name.Contains('\\') || name.Contains(':') ||
            name.Split('/').Any(p => p.Length == 0 || p is "." or ".." || p.EndsWith('.') || p.EndsWith(' ') ||
                p.IndexOfAny(Path.GetInvalidFileNameChars()) >= 0 ||
                System.Text.RegularExpressions.Regex.IsMatch(p, @"^(con|prn|aux|nul|com[0-9]|lpt[0-9])(?:\.|$)", System.Text.RegularExpressions.RegexOptions.IgnoreCase)))
            throw new InvalidDataException("文件路径无效。");
        string parent = Plain(root).TrimEnd(Path.DirectorySeparatorChar) + Path.DirectorySeparatorChar;
        string full = Plain(Path.Combine(parent, name.Replace('/', Path.DirectorySeparatorChar)));
        if (!full.StartsWith(parent, StringComparison.OrdinalIgnoreCase)) throw new InvalidDataException("文件超出安装目录。");
        return full;
    }
    public static string Hash(string path)
    {
        using var stream = File.OpenRead(Plain(path));
        return Convert.ToHexStringLower(SHA256.HashData(stream));
    }
    public static void Verify(string path, Asset asset)
    {
        if (new FileInfo(Plain(path)).Length != asset.Bytes || Hash(path) != asset.Sha256)
            throw new InvalidDataException("文件校验失败：" + asset.Path);
    }
    public static void Atomic(string source, string target)
    {
        Plain(source); Plain(target);
        Directory.CreateDirectory(Path.GetDirectoryName(target)!);
        string temp = target + "." + Guid.NewGuid().ToString("N") + ".tmp";
        try
        {
            File.Copy(source, temp, false); // independent inode, never overwrite a shared hard link
            if (Hash(temp) != Hash(source)) throw new IOException("临时文件校验失败。");
            if (File.Exists(target)) ReplaceVerified(temp, target);
            else File.Move(temp, target);
        }
        finally { if (File.Exists(temp)) File.Delete(temp); }
    }
    public static void WriteAtomic(string target, byte[] bytes)
    {
        Plain(target); Directory.CreateDirectory(Path.GetDirectoryName(target)!);
        string temp = target + "." + Guid.NewGuid().ToString("N") + ".tmp";
        try
        {
            File.WriteAllBytes(temp, bytes);
            if (File.Exists(target)) ReplaceVerified(temp, target); else File.Move(temp, target);
        }
        finally { if (File.Exists(temp)) File.Delete(temp); }
    }
    private static void ReplaceVerified(string temp, string target)
    {
        string before = Hash(target), after = Hash(temp);
        for (int attempt = 0; ; attempt++)
        {
            if (attempt > 0 && (Hash(target) != before || Hash(temp) != after))
                throw new IOException("重试期间文件被其他程序修改，已停止替换。");
            try { File.Replace(temp, target, null); return; }
            catch (IOException ex) when (attempt < 4 && (ex.HResult & 0xffff) is 32 or 33 or 1175)
            {
                // Windows scanners may briefly retain a sharing lock. Retry only
                // an unchanged transaction, retaining atomic hard-link isolation.
                Thread.Sleep(100 << attempt);
            }
        }
    }
    public static void RemoveTree(string root, string directory)
    {
        string dir = Plain(directory), parent = Plain(root).TrimEnd('\\') + "\\";
        if (!dir.StartsWith(parent, StringComparison.OrdinalIgnoreCase)) throw new IOException("拒绝清理目录外的文件。");
        foreach (var entry in Directory.EnumerateFileSystemEntries(dir))
        {
            Plain(entry);
            if (Directory.Exists(entry)) RemoveTree(root, entry); else File.Delete(entry);
        }
        Directory.Delete(dir);
    }
    public static void GameStopped(string game)
    {
        foreach (var name in new[] { "hl", "MetaHook" })
        foreach (var process in Process.GetProcessesByName(name))
        using (process)
        {
            try
            {
                var exe = process.MainModule?.FileName;
                if (exe != null && string.Equals(Path.GetDirectoryName(exe), game, StringComparison.OrdinalIgnoreCase))
                    throw new GameRunningException();
            }
            catch (System.ComponentModel.Win32Exception) { throw new IOException("请先退出 CS，再安装或更新。"); }
            catch (InvalidOperationException) { /* process exited during enumeration */ }
        }
    }
    private sealed class GameRunningException : IOException { public GameRunningException() : base("请先退出这个目录中的 CS，再安装或更新。") { } }
}

public sealed record Change(string Path, string? Source);
public sealed record JournalEntry(string Path, bool Existed, string? Before, string? After);
public sealed record Journal(string Status, JournalEntry[] Files);

public static class Transactions
{
    public static FileStream Lock(string game)
    {
        var state = SafeFiles.Under(game, ".goldcraft-launcher"); Directory.CreateDirectory(state);
        return new FileStream(SafeFiles.Under(state, "operation.lock"), FileMode.OpenOrCreate, FileAccess.ReadWrite, FileShare.None);
    }
    public static void Recover(string game)
    {
        var backups = SafeFiles.Under(game, ".goldcraft-launcher/backups");
        if (!Directory.Exists(backups)) return;
        foreach (var dir in Directory.GetDirectories(backups))
        {
            SafeFiles.Plain(dir);
            string file = SafeFiles.Under(dir, "transaction.json");
            if (!File.Exists(file)) continue;
            var j = JsonData.Read<Journal>(File.ReadAllBytes(file));
            if (j.Status == "prepared") Rollback(game, dir, j);
        }
    }
    public static void Apply(string game, IReadOnlyList<Change> changes, Action<int>? beforeWrite = null)
    {
        if (changes.Count == 0) return;
        SafeFiles.GameStopped(game);
        var names = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach (var c in changes)
        {
            if (!names.Add(c.Path)) throw new InvalidDataException("安装路径重复。");
            SafeFiles.Under(game, c.Path);
            if (c.Source != null) SafeFiles.Plain(c.Source);
        }
        string backups = SafeFiles.Under(game, ".goldcraft-launcher/backups");
        string backup = SafeFiles.Under(backups, DateTime.UtcNow.ToString("yyyyMMdd-HHmmss") + "-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(backup);
        var rows = new List<JournalEntry>();
        foreach (var c in changes)
        {
            string target = SafeFiles.Under(game, c.Path);
            bool existed = File.Exists(target);
            string? old = existed ? SafeFiles.Hash(target) : null;
            if (existed)
            {
                string saved = SafeFiles.Under(backup, "files/" + c.Path);
                Directory.CreateDirectory(Path.GetDirectoryName(saved)!); File.Copy(target, saved, false);
                if (SafeFiles.Hash(saved) != old) throw new IOException("备份校验失败。");
            }
            rows.Add(new(c.Path, existed, old, c.Source == null ? null : SafeFiles.Hash(c.Source)));
        }
        var journal = new Journal("prepared", rows.ToArray());
        string record = SafeFiles.Under(backup, "transaction.json");
        SafeFiles.WriteAtomic(record, JsonData.Write(journal));
        try
        {
            for (int i = 0; i < changes.Count; i++)
            {
                beforeWrite?.Invoke(i);
                var c = changes[i]; string target = SafeFiles.Under(game, c.Path);
                string? now = File.Exists(target) ? SafeFiles.Hash(target) : null;
                if (now != rows[i].Before) throw new IOException("更新时文件被其他程序修改：" + c.Path);
                if (c.Source == null) { if (File.Exists(target)) File.Delete(target); }
                else SafeFiles.Atomic(c.Source, target);
                if ((File.Exists(target) ? SafeFiles.Hash(target) : null) != rows[i].After) throw new IOException("安装后校验失败。");
            }
            SafeFiles.WriteAtomic(record, JsonData.Write(journal with { Status = "installed" }));
        }
        catch
        {
            Rollback(game, backup, journal);
            throw;
        }
        // Retain two complete independent backups. Pending/failed recovery is never pruned.
        var completed = Directory.GetDirectories(backups).Where(d => File.Exists(SafeFiles.Under(d,"transaction.json")))
            .Where(d => JsonData.Read<Journal>(File.ReadAllBytes(SafeFiles.Under(d,"transaction.json"))).Status != "prepared")
            .OrderByDescending(d => d, StringComparer.Ordinal).Skip(2).ToArray();
        foreach (var dir in completed) SafeFiles.RemoveTree(backups, dir);
    }
    private static void Rollback(string game, string backup, Journal journal)
    {
        foreach (var row in journal.Files.Reverse())
        {
            string target = SafeFiles.Under(game, row.Path);
            string? current = File.Exists(target) ? SafeFiles.Hash(target) : null;
            if (current == row.Before) continue;
            if (current != row.After) throw new IOException("无法自动恢复已被其他程序修改的文件，备份保留于：" + backup);
            if (row.Existed)
            {
                string saved = SafeFiles.Under(backup, "files/" + row.Path);
                if (SafeFiles.Hash(saved) != row.Before) throw new IOException("备份损坏，停止恢复。");
                SafeFiles.Atomic(saved, target);
            }
            else if (File.Exists(target)) File.Delete(target);
        }
        SafeFiles.WriteAtomic(SafeFiles.Under(backup, "transaction.json"), JsonData.Write(journal with { Status = "rolled-back" }));
    }
}
