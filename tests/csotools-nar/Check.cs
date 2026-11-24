using System;
using System.IO;
using System.Text;
using Nexon.CSO;

static class Check
{
    static void Require(bool condition, string message)
    {
        if (!condition)
            throw new InvalidDataException(message);
    }

    static void CheckEntry(NexonArchive archive, string path, string expected)
    {
        foreach (NexonArchiveFileEntry entry in archive.FileEntries)
        {
            if (entry.Path != path)
                continue;
            using (MemoryStream destination = new MemoryStream())
            {
                Require(entry.Verify() && entry.Size == expected.Length, path);
                Require(entry.Extract(destination) == expected.Length, path);
                Require(Encoding.ASCII.GetString(destination.ToArray()) == expected, path);
            }
            return;
        }
        throw new FileNotFoundException(path);
    }

    static void CheckRejected(NexonArchive reader, string path)
    {
        try
        {
            reader.Load(path, false);
            throw new Exception("Unexpectedly opened " + path);
        }
        catch (InvalidDataException) { }

        using (Stream caller = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.None))
        {
            try
            {
                reader.Load(caller, false);
                throw new Exception("Unexpectedly opened stream for " + path);
            }
            catch (InvalidDataException) { }
            Require(caller.CanRead, "Failed load closed caller stream");
        }
        reader.Load("standard.nar", false);
        reader.Close();
    }

    static void Main()
    {
        using (NexonArchive reader = new NexonArchive())
        {
            foreach (string name in new string[] { "standard-zero", "repacked-nar", "unknown", "short", "truncated" })
                CheckRejected(reader, name + ".nar");
            reader.Load("repacked.nar", false);
            Require(reader.FileEntries.Count == 4, "Duplicate license");
            CheckEntry(reader, "LICENSE", "license");
            CheckEntry(reader, "dir/q%3F.txt", "question");
            CheckEntry(reader, "dir/q%253F.txt", "percent");
            CheckEntry(reader, "dir/\uC0F7\uAC74.bin", "bitmap");
            reader.Close();
            reader.Load("distinct.nar", false);
            Require(reader.FileEntries.Count == 2, "Distinct root files");
            CheckEntry(reader, "LICENSE", "license");
            CheckEntry(reader, "%2FLICENSE", "another");
            reader.Close();
            reader.Load("standard.nar", false);
            CheckEntry(reader, "/dir/ref.bin", new string('A', 8198));
            reader.Close();
        }
        Console.WriteLine("NAR compatibility checks passed.");
    }
}
