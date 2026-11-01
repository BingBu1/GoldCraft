using System.Diagnostics;
using System.Runtime.InteropServices;

namespace GoldCraft.Launcher;

public sealed class LauncherForm : Form
{
    private readonly Bundle bundle;
    private readonly string data,updateBase;
    private readonly TextBox game=new(){Dock=DockStyle.Fill,ReadOnly=true,BorderStyle=BorderStyle.None,TabStop=false,BackColor=LauncherTheme.Surface,ForeColor=LauncherTheme.Muted,Margin=new(0,9,6,0),AccessibleName="CS 游戏目录"};
    private readonly Label serverName=Label("游戏服务器",18,FontStyle.Bold);
    private readonly Label serverState=Label("正在获取服务器状态…",10);
    private readonly Label map=Label("—",13,FontStyle.Bold),players=Label("— / —",13,FontStyle.Bold);
    private readonly Label installState=Label("需要安装游戏组件",10);
    private readonly Label operation=Label("安装组件后，即可开始游戏。",10);
    private readonly LauncherButton install=Button("安装 MetaHook"),browse=Button("更换",ButtonTone.Quiet),refresh=Button("刷新",ButtonTone.Quiet);
    private readonly LauncherButton launch=Button("启动游戏  →",ButtonTone.Primary),cancel=Button("取消",ButtonTone.Quiet);
    private readonly System.Windows.Forms.Timer timer=new(){Interval=20000};
    private readonly CancellationTokenSource lifetime=new();
    private CancellationTokenSource? work;
    private InstallState? installed;
    private bool busy,refreshing,closingRequested;
    private int installCheck;

    public LauncherForm(Bundle bundle,string data,string? initialGame)
    {
        this.bundle=bundle;this.data=data;updateBase=bundle.Config.UpdateBase;
        var preferences=LoadPreferences();
        Text="冰桑启动器";ClientSize=new(970,640);MinimumSize=new(970,640);StartPosition=FormStartPosition.CenterScreen;
        FormBorderStyle=FormBorderStyle.None;MaximizeBox=false;DoubleBuffered=true;KeyPreview=true;
        BackColor=LauncherTheme.Background;ForeColor=LauncherTheme.Text;Font=LauncherTheme.Font(10);AutoScaleDimensions=new(96,96);AutoScaleMode=AutoScaleMode.Dpi;
        var root=new TableLayoutPanel{Dock=DockStyle.Fill,ColumnCount=1,RowCount=3,Padding=new(1)};
        root.RowStyles.Add(new(SizeType.Absolute,54));root.RowStyles.Add(new(SizeType.Percent,100));root.RowStyles.Add(new(SizeType.Absolute,30));Controls.Add(root);
        root.Controls.Add(TitleBar(),0,0);
        var body=new TableLayoutPanel{Dock=DockStyle.Fill,ColumnCount=2,RowCount=1,Padding=new(10,0,28,6),Margin=new(0)};
        body.ColumnStyles.Add(new(SizeType.Percent,49));body.ColumnStyles.Add(new(SizeType.Percent,51));root.Controls.Add(body,0,1);
        var cover=new TableLayoutPanel{Dock=DockStyle.Fill,ColumnCount=1,RowCount=3,Padding=new(24,28,10,4),Margin=new(0)};
        cover.RowStyles.Add(new(SizeType.Absolute,76));cover.RowStyles.Add(new(SizeType.Absolute,35));cover.RowStyles.Add(new(SizeType.Percent,100));
        var heading=Label("冰桑启动器",32,FontStyle.Bold);heading.ForeColor=LauncherTheme.Text;
        cover.Controls.Add(heading,0,0);cover.Controls.Add(Label("Counter-Strike  ×  Minecraft",11),0,1);cover.Controls.Add(new IceWorld{Dock=DockStyle.Fill},0,2);body.Controls.Add(cover,0,0);
        var right=new TableLayoutPanel{Dock=DockStyle.Fill,ColumnCount=1,RowCount=5,Padding=new(28,22,0,0),Margin=new(0)};
        foreach(int h in new[]{184,100,80,68})right.RowStyles.Add(new(SizeType.Absolute,h));right.RowStyles.Add(new(SizeType.Percent,100));body.Controls.Add(right,1,0);
        right.Controls.Add(ServerPanel(),0,0);
        var directory=new TableLayoutPanel{Dock=DockStyle.Fill,ColumnCount=2,RowCount=2,BackColor=LauncherTheme.Surface,Padding=new(16,12,12,12),Margin=new(0,0,0,10)};
        directory.ColumnStyles.Add(new(SizeType.Percent,100));directory.ColumnStyles.Add(new(SizeType.Absolute,64));directory.RowStyles.Add(new(SizeType.Absolute,24));directory.RowStyles.Add(new(SizeType.Percent,100));
        var directoryTitle=Label("游戏位置",10);directoryTitle.BackColor=LauncherTheme.Surface;directory.Controls.Add(directoryTitle,0,0);directory.SetColumnSpan(directoryTitle,2);
        browse.BackColor=LauncherTheme.Surface;directory.Controls.Add(game,0,1);directory.Controls.Add(browse,1,1);right.Controls.Add(directory,0,1);
        var components=new TableLayoutPanel{Dock=DockStyle.Fill,ColumnCount=2,RowCount=2,Padding=new(0,8,0,16),Margin=new(0)};
        components.ColumnStyles.Add(new(SizeType.Percent,100));components.ColumnStyles.Add(new(SizeType.Absolute,134));components.RowStyles.Add(new(SizeType.Absolute,26));components.RowStyles.Add(new(SizeType.Percent,100));
        components.Controls.Add(Label("MetaHook 组件",11,FontStyle.Bold),0,0);components.Controls.Add(installState,0,1);components.Controls.Add(install,1,0);components.SetRowSpan(install,2);right.Controls.Add(components,0,2);
        launch.Font=LauncherTheme.Font(16,FontStyle.Bold);launch.Enabled=false;launch.Margin=new(0,0,0,12);right.Controls.Add(launch,0,3);
        var feedback=new TableLayoutPanel{Dock=DockStyle.Fill,ColumnCount=1,RowCount=2,Margin=new(0)};
        feedback.RowStyles.Add(new(SizeType.Percent,100));feedback.RowStyles.Add(new(SizeType.Absolute,30));feedback.Controls.Add(operation,0,0);cancel.Dock=DockStyle.Right;cancel.Width=70;cancel.Visible=false;feedback.Controls.Add(cancel,0,1);right.Controls.Add(feedback,0,4);
        var footer=new TableLayoutPanel{Dock=DockStyle.Fill,ColumnCount=2,Padding=new(34,0,32,0),Margin=new(0)};
        footer.ColumnStyles.Add(new(SizeType.Percent,50));footer.ColumnStyles.Add(new(SizeType.Percent,50));footer.Controls.Add(Label("客户端 "+UserMessages.Clean(bundle.Config.Version,updateBase),8),0,0);
        var automatic=Label("启动前自动检查更新",8);automatic.TextAlign=ContentAlignment.TopRight;footer.Controls.Add(automatic,1,0);root.Controls.Add(footer,0,2);
        game.Text=initialGame??preferences.GameDirectory;game.Font=LauncherTheme.Font(9);
        browse.TabIndex=0;install.TabIndex=1;launch.TabIndex=2;refresh.TabIndex=3;cancel.TabIndex=4;
        browse.Click+=async(_,_)=>{using var dialog=new FolderBrowserDialog{Description="选择 CS 1.6 游戏文件夹",UseDescriptionForTitle=true,SelectedPath=game.Text};if(dialog.ShowDialog(this)==DialogResult.OK){game.Text=dialog.SelectedPath;await CheckInstalled();}};
        install.Click+=async(_,_)=>await Run("正在准备组件…",async token=>{string path=NormalizeGame();var report=new Progress<string>(SetStatus);await Task.Run(()=>bundle.Install(path,data,report,token),token);SetStatus("组件已就绪，可以启动游戏。");});
        launch.Click+=async(_,_)=>await Run("正在检查更新…",async token=>
        {
            string path=NormalizeGame();using var updates=new Updates(bundle.Config,updateBase);var report=new Progress<string>(SetStatus);
            var release=await Task.Run(()=>updates.Apply(path,data,bundle,report,token),token);token.ThrowIfCancellationRequested();
            if(Process.GetProcessesByName("steam").Length==0)throw new IOException("请先打开 Steam 并登录，再启动游戏。");
            SafeFiles.GameStopped(path);var info=new ProcessStartInfo(SafeFiles.Under(path,"MetaHook.exe")){WorkingDirectory=path,UseShellExecute=false};
            foreach(string arg in new[]{"-game","cstrike","-insecure","+connect",Updates.GameAddress(release.GameAddress)})info.ArgumentList.Add(arg);
            Process.Start(info);SetStatus("游戏已启动，正在进入服务器。");
        });
        cancel.Click+=(_,_)=>{work?.Cancel();SetStatus("正在取消，请稍候…");};refresh.Click+=async(_,_)=>await RefreshServer();timer.Tick+=async(_,_)=>await RefreshServer();
        Shown+=async(_,_)=>
        {
            try{var found=await Task.Run(SteamDiscovery.Find);if(string.IsNullOrWhiteSpace(game.Text)&&found.Count>0)game.Text=found[0];}
            catch{SetStatus("没有找到游戏，请点击“更换”选择目录。");}
            await CheckInstalled();await RefreshServer();timer.Start();
        };
        KeyDown+=(_,e)=>{if(e.KeyCode==Keys.Escape&&busy){work?.Cancel();e.Handled=true;}};
        FormClosing+=(_,e)=>{if(busy){e.Cancel=true;closingRequested=true;work?.Cancel();SetStatus("正在安全结束操作…");}else{timer.Stop();lifetime.Cancel();SavePreferences();}};
    }
    private Control TitleBar()
    {
        var bar=new Panel{Dock=DockStyle.Fill,Margin=new(0),Padding=new(24,8,10,8)};
        var brand=Label("冰桑",11,FontStyle.Bold);brand.Dock=DockStyle.Left;brand.Width=180;brand.Padding=new(10,8,0,0);bar.Controls.Add(brand);
        var windowActions=new FlowLayoutPanel{Dock=DockStyle.Right,Width=104,FlowDirection=FlowDirection.LeftToRight,WrapContents=false,Margin=new(0)};
        var minimize=Button("—",ButtonTone.Quiet);minimize.Size=new(44,34);minimize.Dock=DockStyle.None;minimize.AccessibleName="最小化";minimize.Click+=(_,_)=>WindowState=FormWindowState.Minimized;
        var close=Button("×",ButtonTone.Quiet);close.Size=new(44,34);close.Dock=DockStyle.None;close.Font=LauncherTheme.Font(15);close.AccessibleName="关闭";close.Click+=(_,_)=>Close();
        windowActions.Controls.Add(minimize);windowActions.Controls.Add(close);bar.Controls.Add(windowActions);
        void Drag(object? sender,MouseEventArgs e){if(e.Button==MouseButtons.Left){ReleaseCapture();SendMessage(Handle,0xA1,new IntPtr(2),IntPtr.Zero);}}
        bar.MouseDown+=Drag;brand.MouseDown+=Drag;return bar;
    }
    private Control ServerPanel()
    {
        var server=new TableLayoutPanel{Dock=DockStyle.Fill,ColumnCount=2,RowCount=4,Margin=new(0,0,0,18)};
        server.ColumnStyles.Add(new(SizeType.Percent,100));server.ColumnStyles.Add(new(SizeType.Absolute,64));
        foreach(int h in new[]{27,42,30,67})server.RowStyles.Add(new(SizeType.Absolute,h));
        server.Controls.Add(Label("服务器",10),0,0);server.Controls.Add(refresh,1,0);server.Controls.Add(serverName,0,1);server.SetColumnSpan(serverName,2);server.Controls.Add(serverState,0,2);server.SetColumnSpan(serverState,2);
        var stats=new TableLayoutPanel{Dock=DockStyle.Fill,ColumnCount=2,RowCount=2,Margin=new(0),Padding=new(0,8,0,0)};
        stats.ColumnStyles.Add(new(SizeType.Percent,57));stats.ColumnStyles.Add(new(SizeType.Percent,43));stats.RowStyles.Add(new(SizeType.Absolute,24));stats.RowStyles.Add(new(SizeType.Percent,100));
        stats.Controls.Add(Label("当前地图",9),0,0);stats.Controls.Add(Label("在线玩家",9),1,0);stats.Controls.Add(map,0,1);stats.Controls.Add(players,1,1);server.Controls.Add(stats,0,3);server.SetColumnSpan(stats,2);return server;
    }
    private static Label Label(string text,float size,FontStyle style=FontStyle.Regular)=>new(){Text=text,Dock=DockStyle.Fill,Font=LauncherTheme.Font(size,style),ForeColor=style==FontStyle.Regular?LauncherTheme.Muted:LauncherTheme.Text,Margin=new(0),AutoEllipsis=true};
    private static LauncherButton Button(string text,ButtonTone tone=ButtonTone.Outline)=>new(){Text=text,Tone=tone,Dock=DockStyle.Fill};
    private string NormalizeGame(){string value=game.Text.Trim().Trim('"');if(Path.GetFileName(value).Equals("cstrike",StringComparison.OrdinalIgnoreCase))value=Path.GetDirectoryName(value)!;return SafeFiles.Plain(value);}
    private void SetStatus(string text){operation.Text=UserMessages.Clean(text,updateBase);operation.ForeColor=LauncherTheme.Muted;}
    private async Task CheckInstalled()
    {
        int check=++installCheck;launch.Enabled=false;string path=game.Text.Trim();
        try{if(path.Length>0)path=NormalizeGame();}catch(Exception ex)when(ex is ArgumentException or IOException or UnauthorizedAccessException){}
        var result=await Task.Run(()=>bundle.Installed(path));if(IsDisposed||check!=installCheck)return;
        installed=result;game.Text=path;installState.Text=installed==null?"尚未安装或需要修复":"已安装 · 可以启动";installState.ForeColor=installed==null?LauncherTheme.Muted:LauncherTheme.Accent;
        install.Text=installed==null?"安装 MetaHook":"检查组件";install.Enabled=!busy&&path.Length>0;launch.Enabled=!busy&&installed!=null;
        if(installed!=null&&operation.Text=="安装组件后，即可开始游戏。")SetStatus("准备就绪。点击启动游戏，自动完成更新并连接。");SavePreferences();
    }
    private async Task Run(string message,Func<CancellationToken,Task> operationTask)
    {
        if(busy)return;busy=true;++installCheck;work=new();browse.Enabled=install.Enabled=launch.Enabled=false;launch.Text=message;cancel.Visible=true;SetStatus(message);
        try{await operationTask(work.Token);}
        catch(OperationCanceledException){SetStatus(work.IsCancellationRequested?"操作已取消。":"连接超时，请稍后重试。");}
        catch(Exception ex){SetStatus(UserMessages.Error(ex,updateBase));operation.ForeColor=LauncherTheme.Error;}
        finally{busy=false;work.Dispose();work=null;browse.Enabled=true;cancel.Visible=false;launch.Text="启动游戏  →";await CheckInstalled();if(closingRequested)Close();}
    }
    private async Task RefreshServer()
    {
        if(refreshing||IsDisposed)return;refreshing=true;refresh.Enabled=false;
        using var timeout=CancellationTokenSource.CreateLinkedTokenSource(lifetime.Token);timeout.CancelAfter(TimeSpan.FromSeconds(5));
        try
        {
            using var updates=new Updates(bundle.Config,updateBase);var state=await updates.Status(timeout.Token);if(IsDisposed)return;
            serverName.Text=string.IsNullOrWhiteSpace(state.Name)?"游戏服务器":UserMessages.Clean(state.Name,updateBase);
            serverState.Text=state.Online?"●  在线，欢迎加入":"●  暂未开放";serverState.ForeColor=state.Online?LauncherTheme.Accent:LauncherTheme.Muted;
            map.Text=state.Online?UserMessages.Clean(state.Map,updateBase):"—";players.Text=state.Online?$"{state.Players} / {state.MaxPlayers}":"— / —";
        }
        catch(Exception ex)when(ex is not OutOfMemoryException)
        {
            if(IsDisposed)return;serverState.Text="暂时无法获取状态，稍后可刷新。";serverState.ForeColor=LauncherTheme.Muted;map.Text="—";players.Text="— / —";
        }
        finally{refreshing=false;if(!IsDisposed)refresh.Enabled=true;}
    }
    private Preferences LoadPreferences(){try{return JsonData.Read<Preferences>(File.ReadAllBytes(SafeFiles.Under(data,"preferences.json")));}catch(Exception ex)when(ex is IOException or InvalidDataException or UnauthorizedAccessException or System.Text.Json.JsonException){return new();}}
    private void SavePreferences(){try{SafeFiles.WriteAtomic(SafeFiles.Under(data,"preferences.json"),JsonData.Write(new Preferences(game.Text.Trim())));}catch(IOException){}catch(UnauthorizedAccessException){}}
    protected override void OnPaint(PaintEventArgs e){base.OnPaint(e);using var pen=new Pen(LauncherTheme.Border);e.Graphics.DrawRectangle(pen,0,0,Width-1,Height-1);}
    protected override void Dispose(bool disposing){if(disposing){timer.Dispose();lifetime.Cancel();lifetime.Dispose();work?.Dispose();}base.Dispose(disposing);}
    [DllImport("user32.dll")]private static extern bool ReleaseCapture();
    [DllImport("user32.dll")]private static extern IntPtr SendMessage(IntPtr hwnd,int msg,IntPtr wparam,IntPtr lparam);
}
