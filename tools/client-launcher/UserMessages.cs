using System.ComponentModel;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.Text.RegularExpressions;

namespace GoldCraft.Launcher;

public enum LauncherStep { Install, Update, Steam, Resolve, Start }

public static partial class UserMessages
{
    public static string Clean(string? text,string endpoint)
    {
        string result=Url().Replace(text??"","服务地址");
        if(Uri.TryCreate(endpoint,UriKind.Absolute,out var uri))
        {
            result=result.Replace(uri.Authority,"服务地址",StringComparison.OrdinalIgnoreCase);
            result=result.Replace(uri.Host,"服务地址",StringComparison.OrdinalIgnoreCase);
        }
        return result;
    }
    public static string StepName(LauncherStep step) => step switch
    {
        LauncherStep.Install => "安装组件",
        LauncherStep.Update => "检查更新",
        LauncherStep.Steam => "检查 Steam",
        LauncherStep.Resolve => "解析游戏服务器",
        LauncherStep.Start => "启动 MetaHook",
        _ => throw new ArgumentOutOfRangeException(nameof(step))
    };

    private static string Code(Exception error) => error switch
    {
        Win32Exception native => $"Windows {native.NativeErrorCode}",
        HttpRequestException { StatusCode: { } status } => $"HTTP {(int)status}",
        IOException io when ((uint)io.HResult >> 16) == 0x8007 => $"Windows {io.HResult & 0xffff}",
        _ => $"{error.GetType().Name} 0x{error.HResult:X8}"
    };

    public static string Error(Exception error, string endpoint, LauncherStep step)
    {
        string action = error switch
        {
            Win32Exception native when step == LauncherStep.Start => native.NativeErrorCode switch
            {
                2 or 3 => "找不到 MetaHook.exe 或其游戏目录。请点击“检查组件”修复。",
                5 or 740 => "Windows 拒绝启动 MetaHook.exe。请检查该文件的运行权限和兼容性设置。",
                1223 => "已取消 Windows 的启动确认。再次点击“启动游戏”可重新确认。",
                32 or 33 => "MetaHook.exe 正被占用。请退出该目录中的游戏后重试。",
                193 or 216 => "Windows 无法运行此版本的 MetaHook.exe。请检查组件和系统兼容性。",
                126 or 14001 => "Windows 无法加载游戏所需的运行库。请把此错误码发给服主。",
                225 or 226 or 577 or 1260 => "Windows 安全策略阻止了启动。请查看系统保护记录。",
                _ => "Windows 未能启动游戏。请把此错误码发给服主。"
            },
            HttpRequestException { StatusCode: System.Net.HttpStatusCode.NotFound } => "更新服务缺少所需文件，请联系服主。",
            HttpRequestException => "暂时无法连接更新服务，请稍后重试。",
            OperationCanceledException => "连接超时，请重试。",
            JsonException or FormatException when step == LauncherStep.Update => "更新数据格式不完整，请重试。如果持续失败，请将此提示发给服主。",
            CryptographicException => "无法验证组件签名，请将此提示发给服主。",
            UnauthorizedAccessException => "Windows 拒绝访问所需文件，请检查游戏目录和启动器数据目录的权限。",
            InvalidDataException or IOException => Clean(error.Message, endpoint),
            _ => "请将这条提示发给服主。"
        };
        return $"{StepName(step)}（{Code(error)}）\n{action}";
    }

    public static void SaveDiagnostic(string data, string version, LauncherStep step, Exception error)
    {
        var report = new StringBuilder().AppendLine($"冰桑启动器 {version}  {DateTimeOffset.Now:O}").AppendLine(StepName(step));
        // Only record error codes and method names. Exception messages can contain private URLs and paths.
        for (Exception? cause = error; cause != null; cause = cause.InnerException)
        {
            var site = cause.TargetSite;
            report.AppendLine($"{Code(cause)}  {site?.DeclaringType?.Name}.{site?.Name}");
        }
        try { SafeFiles.WriteAtomic(SafeFiles.Under(data, "last-error.txt"), Encoding.UTF8.GetBytes(report.ToString())); }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or System.Security.SecurityException) { }
    }
    [GeneratedRegex(@"https?://[^\s<>""']+",RegexOptions.IgnoreCase)] private static partial Regex Url();
}
