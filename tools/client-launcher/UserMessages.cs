using System.Text.RegularExpressions;

namespace GoldCraft.Launcher;

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
    public static string Error(Exception error,string endpoint)=>error switch
    {
        HttpRequestException { StatusCode:System.Net.HttpStatusCode.NotFound }=>"更新服务暂未开放，请联系服主。",
        HttpRequestException=>"暂时无法连接更新服务，请稍后重试。",
        UnauthorizedAccessException=>"游戏目录不可写，请更换目录或以管理员身份运行。",
        InvalidDataException or IOException=>Clean(error.Message,endpoint),
        _=>"操作未完成，请重试；若仍失败，请联系服主。"
    };
    [GeneratedRegex(@"https?://[^\s<>""']+",RegexOptions.IgnoreCase)] private static partial Regex Url();
}
