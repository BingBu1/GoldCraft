using System.Text.Json;

namespace GoldCraft.Launcher;

public record Asset(string Path, long Bytes, string Sha256);
public sealed record BundleConfig(int Schema, string BundleId, string Version, string UpdateBase,
    string GameAddress, string PublicKeyPem, string EngineSha256, string ClientSha256,
    string[] Plugins, string[] DllPaths, string[] CorePaths, Asset[] Files);
public sealed record SignedEnvelope(string Payload, string Signature);
public sealed record Release(int Schema, string BundleId, long Sequence, string Version,
    string GameAddress, Asset[] Files);
public sealed record ServerStatus(int Schema, bool Online, string Name, string Map,
    int Players, int MaxPlayers, string GameAddress);
public sealed record InstallState(string BundleId, long Sequence, string Version, Asset[] CoreFiles);
public sealed record Preferences(string GameDirectory = "");
public static class JsonData
{
    public static readonly JsonSerializerOptions Options = new() { PropertyNamingPolicy = JsonNamingPolicy.CamelCase, WriteIndented = true };
    public static T Read<T>(byte[] data) => JsonSerializer.Deserialize<T>(data, Options) ?? throw new InvalidDataException("文件内容为空。");
    public static byte[] Write<T>(T value) => JsonSerializer.SerializeToUtf8Bytes(value, Options);
}
