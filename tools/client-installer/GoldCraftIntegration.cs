using System;
using System.Diagnostics;
using System.IO;
using System.Text;
using System.Threading.Tasks;
using MetahookInstaller;
using ReactiveUI;

namespace MetahookInstallerAvalonia.ViewModels;

// Local integration for the pinned official Avalonia UI. Steam discovery and
// file deployment still belong to the official installer; the PS entry adds
// GoldCraft's matched payload, plugin ordering and reversible transactions.
public partial class MainViewModel
{
    private bool _isBusy;
    public bool IsBusy { get => _isBusy; private set => this.RaiseAndSetIfChanged(ref _isBusy, value); }
    private string _operationStatus = "GoldCraft — 选择 CS 文件夹后点击安装 / Select CS and install";
    public string OperationStatus { get => _operationStatus; private set => this.RaiseAndSetIfChanged(ref _operationStatus, value); }
    private string _operationLog = "";
    public string OperationLog { get => _operationLog; private set => this.RaiseAndSetIfChanged(ref _operationLog, value); }
    public int? LastOperationExitCode { get; private set; }

    private static string Literal(string text) => "'" + text.Replace("'", "''") + "'";

    public async Task RunGoldCraftAsync(bool restore)
    {
        if (IsBusy) return;
        IsBusy = true;
        LastOperationExitCode = null;
        OperationLog = "";
        try
        {
            // Snapshot before awaiting; the UI also disables target/editor changes.
            var target = Selected;
            if (target?.GamePath is not string game ||
                !string.Equals(target.Directory, "cstrike", StringComparison.OrdinalIgnoreCase) ||
                !MetahookSetup.IsValidInstallTarget(game, "cstrike"))
                throw new InvalidOperationException("请选择有效的 CS 1.6 根目录 / Select a valid CS 1.6 game root.");
            var script = Path.Combine(AppContext.BaseDirectory, "Install-GoldCraft.ps1");
            if (!File.Exists(script) || !File.Exists(Path.Combine(AppContext.BaseDirectory, "package.json")))
                throw new FileNotFoundException("请完整解压 GoldCraft 安装包 / Extract the complete GoldCraft package.");
            OperationStatus = restore ? "正在恢复 / Restoring…" : "正在安装 / Installing…";
            var command = "[Console]::OutputEncoding = [Text.UTF8Encoding]::new($false); " +
                "$ProgressPreference = 'SilentlyContinue'; $global:LASTEXITCODE = 1; & " + Literal(script) +
                " -GameDirectory " + Literal(Path.GetFullPath(game)) + (restore ? " -Restore" : "") +
                "; exit $LASTEXITCODE";
            var info = new ProcessStartInfo
            {
                FileName = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.Windows),
                    @"System32\WindowsPowerShell\v1.0\powershell.exe"),
                WorkingDirectory = AppContext.BaseDirectory,
                UseShellExecute = false,
                CreateNoWindow = true,
                RedirectStandardOutput = true,
                RedirectStandardError = true,
                StandardOutputEncoding = Encoding.UTF8,
                StandardErrorEncoding = Encoding.UTF8
            };
            foreach (var arg in new[] { "-NoLogo", "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass",
                "-OutputFormat", "Text", "-Command", command })
                info.ArgumentList.Add(arg);
            info.Environment["PSModulePath"] = "";
            using var process = Process.Start(info) ?? throw new IOException("Cannot start installation.");
            var output = process.StandardOutput.ReadToEndAsync();
            var error = process.StandardError.ReadToEndAsync();
            await process.WaitForExitAsync();
            OperationLog = ((await output) + Environment.NewLine + (await error)).Trim();
            LastOperationExitCode = process.ExitCode;
            OperationStatus = process.ExitCode == 0
                ? (restore ? "已恢复上次安装前的文件 / Previous files restored"
                           : "安装完成 — 使用游戏目录中的 MetaHook for Counter-Strike 快捷方式启动 / Installed")
                : "操作失败，请查看日志 / Operation failed; see the log";
            _pluginListInitialized = false;
            this.RaisePropertyChanged(nameof(EditorUsable));
        }
        catch (Exception ex)
        {
            LastOperationExitCode = -1;
            OperationStatus = "操作失败，请查看日志 / Operation failed; see the log";
            OperationLog = ex.Message;
        }
        finally { IsBusy = false; }
    }
}
