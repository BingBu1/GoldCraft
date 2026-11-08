using System.IO.Compression;
using System.Security.Cryptography;
using System.Text.RegularExpressions;

namespace GoldCraft.Launcher;

// A stable launcher protocol: signed releases own DLLs and their symbol/shader
// resources. The embedded bundle remains the offline bootstrap, not the new
// resource hash authority. Existing server routes can carry these small resources
// inside the signed manifest; DLLs retain content-addressed download routes.
public static class ReleasePolicy
{
    public const int ManifestLimit = 1024 * 1024;
    public static bool ResourcePath(string path) => Regex.IsMatch(path,
        @"\Acstrike/(?:metahook/gamedata/[A-Za-z0-9_./-]+\.json|(?:metahook/shaders|renderer/shader)/[A-Za-z0-9_./-]+\.(?:glsl|fsh|vsh|h))\z");
    public static bool CorePath(string path) => Regex.IsMatch(path,
        @"\A(?:MetaHook\.dll|cstrike/metahook/(?:plugins|dlls)/[A-Za-z0-9_/-]+\.dll)\z");
    public static Asset Asset(InlineResource resource) => new(resource.Path, resource.Bytes, resource.Sha256);
    private static void ValidAsset(Asset asset, long limit, HashSet<string> names)
    {
        if (asset == null || string.IsNullOrEmpty(asset.Path) || !names.Add(asset.Path) ||
            asset.Bytes <= 0 || asset.Bytes > limit || !Regex.IsMatch(asset.Sha256 ?? "", @"\A[0-9a-f]{64}\z"))
            throw new InvalidDataException("更新文件清单无效。");
        SafeFiles.Under(Path.GetTempPath(), asset.Path); // validates segments without writing
    }
    public static Release Verify(BundleConfig config, SignedEnvelope envelope, long previous)
    {
        if (string.IsNullOrEmpty(envelope.Payload) || string.IsNullOrEmpty(envelope.Signature) ||
            envelope.Payload.Length > ManifestLimit || envelope.Signature.Length > 2048)
            throw new InvalidDataException("更新签名文件不完整或过大。");
        byte[] payload = Convert.FromBase64String(envelope.Payload), signature = Convert.FromBase64String(envelope.Signature);
        using var rsa = RSA.Create(); rsa.ImportFromPem(config.PublicKeyPem);
        if (!rsa.VerifyData(payload, signature, HashAlgorithmName.SHA256, RSASignaturePadding.Pkcs1))
            throw new InvalidDataException("更新签名不正确，已停止更新。");
        var release = JsonData.Read<Release>(payload);
        if (release.Schema is not (1 or 2) || release.BundleId != config.BundleId)
            throw new InvalidDataException("此更新与当前启动器安装基线不兼容。");
        if (release.Sequence < previous || release.Sequence < 1)
            throw new InvalidDataException("服务器提供了过期版本，已停止更新。");
        if (string.IsNullOrWhiteSpace(release.Version) || release.Version.Length > 80 ||
            release.Files == null || release.Files.Length > 128)
            throw new InvalidDataException("更新清单无效。");
        Updates.GameAddress(release.GameAddress);
        var names = new HashSet<string>(StringComparer.OrdinalIgnoreCase); long total = 0;
        foreach (var row in release.Files)
        {
            ValidAsset(row, 16 * 1024 * 1024 - 1, names); total += row.Bytes;
            if (!CorePath(row.Path)) throw new InvalidDataException("更新包含不允许的核心路径。");
        }
        if (total > 256 * 1024 * 1024 || !config.CorePaths.All(names.Contains))
            throw new InvalidDataException("核心文件配套不完整。");
        if (release.Schema == 1)
        {
            if (!names.SetEquals(config.CorePaths)) throw new InvalidDataException("旧版更新核心白名单不匹配。");
            return release;
        }
        if (release.EngineSha256 != config.EngineSha256 || release.ClientSha256 != config.ClientSha256 ||
            release.Resources == null || release.Resources.Length > 1024 || release.Plugins == null ||
            release.Plugins.Length > 64 || release.Plugins.Length == 0)
            throw new InvalidDataException("插件更新的基础游戏或配套资源合同不匹配。");
        long resources = 0;
        foreach (var row in release.Resources)
        {
            if (row == null || !ResourcePath(row.Path ?? "")) throw new InvalidDataException("更新包含不允许的配套文件。");
            ValidAsset(Asset(row), 4 * 1024 * 1024, names); resources += row.Bytes;
            if (resources > 32 * 1024 * 1024) throw new InvalidDataException("配套资源超过限制。");
            Decode(row); // bound decompression and verify SHA before any file mutation
        }
        var plugins = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach (string name in release.Plugins)
            if (string.IsNullOrEmpty(name) || !Regex.IsMatch(name, @"\A[A-Za-z0-9_-]+\.dll\z") ||
                !plugins.Add(name) || !release.Files.Any(x => x.Path == "cstrike/metahook/plugins/" + name))
                throw new InvalidDataException("插件加载顺序清单不完整。");
        if (!config.Plugins.All(plugins.Contains)) throw new InvalidDataException("更新缺少必需插件。");
        return release;
    }
    public static byte[] Decode(InlineResource resource)
    {
        if (resource.Bytes <= 0 || resource.Bytes > 4 * 1024 * 1024 || string.IsNullOrEmpty(resource.Gzip) || resource.Gzip.Length > ManifestLimit)
            throw new InvalidDataException("压缩配套文件无效。");
        using var input = new MemoryStream(Convert.FromBase64String(resource.Gzip));
        using var gzip = new GZipStream(input, CompressionMode.Decompress);
        byte[] bytes = new byte[checked((int)resource.Bytes)]; gzip.ReadExactly(bytes);
        if (gzip.ReadByte() != -1 || Convert.ToHexStringLower(SHA256.HashData(bytes)) != resource.Sha256)
            throw new InvalidDataException("配套文件长度或校验不匹配。");
        return bytes;
    }
    public static Asset[] Owned(BundleConfig config, Release? release) => release?.Schema == 2
        ? release.Files.Concat(release.Resources!.Select(Asset)).ToArray()
        : config.Files.Where(x => ResourcePath(x.Path)).Concat(release?.Files ?? config.Files.Where(x => config.CorePaths.Contains(x.Path))).ToArray();
}
