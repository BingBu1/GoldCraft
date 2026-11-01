using System.Diagnostics;

namespace GoldCraft.Launcher;

public sealed class LauncherForm : Form
{
    private readonly Bundle bundle;
    private readonly string data;
    private string updateBase;
    private readonly ComboBox game=new(){Dock=DockStyle.Fill,DropDownStyle=ComboBoxStyle.DropDown,AccessibleName="CS 游戏目录"};
    private readonly Label serverName=TextLabel("正在连接服务器…",17,FontStyle.Bold);
    private readonly Label serverInfo=TextLabel("正在获取地图和在线人数",11);
    private readonly Label address=TextLabel("",10);
    private readonly Label installState=TextLabel("首次使用，请先安装 MetaHook",10);
    private readonly Label operation=TextLabel("选择游戏目录，安装后即可启动。",10);
    private readonly Button install=Button("安装 MetaHook"),browse=Button("选择目录"),refresh=Button("刷新"),settings=Button("服务器设置");
    private readonly Button launch=Button("启动游戏"),cancel=Button("取消");
    private readonly ProgressBar progress=new(){Dock=DockStyle.Fill,Style=ProgressBarStyle.Marquee,Visible=false,MarqueeAnimationSpeed=24};
    private readonly System.Windows.Forms.Timer timer=new(){Interval=20000};
    private bool busy,refreshing;
    private int installCheck;
    private CancellationTokenSource? work;
    private readonly CancellationTokenSource lifetime=new();
    private InstallState? installed;
    private static readonly Color Accent=Color.FromArgb(28,112,79),Ink=Color.FromArgb(28,37,33),Muted=Color.FromArgb(90,103,97);
    public LauncherForm(Bundle bundle,string data,string? initialGame,string? initialServer)
    {
        this.bundle=bundle;this.data=data;
        var preferences=LoadPreferences();updateBase=initialServer??(!string.IsNullOrWhiteSpace(preferences.UpdateBase)?preferences.UpdateBase:bundle.Config.UpdateBase);
        Text="GoldCraft 启动器";ClientSize=new(800,650);MinimumSize=new(740,689);StartPosition=FormStartPosition.CenterScreen;
        BackColor=Color.FromArgb(241,244,241);ForeColor=Ink;Font=new("Microsoft YaHei UI",10);AutoScaleMode=AutoScaleMode.Dpi;
        var root=new TableLayoutPanel{Dock=DockStyle.Fill,Padding=new(28),ColumnCount=1,RowCount=5};
        foreach(var size in new[]{88,142,178,146})root.RowStyles.Add(new(SizeType.Absolute,size));root.RowStyles.Add(new(SizeType.Percent,100));Controls.Add(root);
        var title=new TableLayoutPanel{Dock=DockStyle.Fill,ColumnCount=2};title.ColumnStyles.Add(new(SizeType.Percent,100));title.ColumnStyles.Add(new(SizeType.Absolute,120));
        var titleText=new FlowLayoutPanel{Dock=DockStyle.Fill,FlowDirection=FlowDirection.TopDown,WrapContents=false};
        settings.Dock=DockStyle.None;settings.Anchor=AnchorStyles.Top|AnchorStyles.Right;settings.Size=new(114,36);settings.Margin=new(6,12,0,0);
        titleText.Controls.Add(TextLabel("GoldCraft",25,FontStyle.Bold));titleText.Controls.Add(TextLabel("安装一次。更新、连接，一步完成。",10));title.Controls.Add(titleText,0,0);title.Controls.Add(settings,1,0);root.Controls.Add(title,0,0);
        var server=Card();server.RowCount=3;server.ColumnCount=2;server.ColumnStyles.Add(new(SizeType.Percent,100));server.ColumnStyles.Add(new(SizeType.Absolute,78));
        server.Controls.Add(serverName,0,0);server.Controls.Add(refresh,1,0);server.Controls.Add(serverInfo,0,1);server.SetColumnSpan(serverInfo,2);server.Controls.Add(address,0,2);server.SetColumnSpan(address,2);root.Controls.Add(server,0,1);
        var setup=Card();setup.RowCount=4;setup.ColumnCount=2;setup.ColumnStyles.Add(new(SizeType.Percent,100));setup.ColumnStyles.Add(new(SizeType.Absolute,140));
        setup.RowStyles.Add(new(SizeType.Absolute,30));setup.RowStyles.Add(new(SizeType.Absolute,38));setup.RowStyles.Add(new(SizeType.Absolute,32));setup.RowStyles.Add(new(SizeType.Percent,100));
        setup.Controls.Add(TextLabel("01  游戏与 MetaHook",12,FontStyle.Bold),0,0);setup.SetColumnSpan(setup.GetControlFromPosition(0,0)!,2);
        setup.Controls.Add(game,0,1);setup.Controls.Add(browse,1,1);setup.Controls.Add(installState,0,2);setup.SetColumnSpan(installState,2);
        setup.Controls.Add(TextLabel("保留原有按键、账号和游戏设置",9),0,3);setup.Controls.Add(install,1,3);root.Controls.Add(setup,0,2);
        var action=new TableLayoutPanel{Dock=DockStyle.Fill,ColumnCount=2,RowCount=3,Padding=new(0,10,0,0)};
        action.ColumnStyles.Add(new(SizeType.Percent,100));action.ColumnStyles.Add(new(SizeType.Absolute,100));
        action.RowStyles.Add(new(SizeType.Absolute,52));action.RowStyles.Add(new(SizeType.Absolute,56));action.RowStyles.Add(new(SizeType.Absolute,6));
        launch.BackColor=Accent;launch.ForeColor=Color.White;launch.FlatAppearance.BorderSize=0;launch.Font=new(Font,FontStyle.Bold);launch.Enabled=false;
        action.Controls.Add(launch,0,0);action.Controls.Add(cancel,1,0);cancel.Visible=false;action.SetColumnSpan(launch,2);action.Controls.Add(operation,0,1);action.SetColumnSpan(operation,2);action.Controls.Add(progress,0,2);action.SetColumnSpan(progress,2);root.Controls.Add(action,0,3);
        var footer=TextLabel("启动时自动检查核心插件更新 · MetaHook 安装文件已内置",9);footer.Dock=DockStyle.Fill;root.Controls.Add(footer,0,4);
        game.Text=initialGame??preferences.GameDirectory;
        game.Leave+=async(_,_)=>{if(!busy)await CheckInstalled();};
        game.SelectionChangeCommitted+=async(_,_)=>{if(!busy)await CheckInstalled();};
        browse.Click+=async(_,_)=>{using var dialog=new FolderBrowserDialog{Description="选择包含 hl.exe 和 cstrike 的游戏文件夹",UseDescriptionForTitle=true,SelectedPath=game.Text};if(dialog.ShowDialog(this)==DialogResult.OK){game.Text=dialog.SelectedPath;await CheckInstalled();}};
        install.Click+=async(_,_)=>await Run(async token=>{string path=NormalizeGame();var report=new Progress<string>(SetStatus);await Task.Run(()=>bundle.Install(path,data,report,token),token);SetStatus("安装完成。现在可以启动游戏。");});
        launch.Click+=async(_,_)=>await Run(async token=>
        {
            string path=NormalizeGame();using var updates=new Updates(bundle.Config,updateBase);
            var report=new Progress<string>(SetStatus);
            var release=await Task.Run(()=>updates.Apply(path,data,bundle,report,token),token);
            token.ThrowIfCancellationRequested();
            if(Process.GetProcessesByName("steam").Length==0)throw new IOException("请先打开 Steam 并登录，然后点击启动游戏。");
            SafeFiles.GameStopped(path);
            var info=new ProcessStartInfo(SafeFiles.Under(path,"MetaHook.exe")){WorkingDirectory=path,UseShellExecute=false};
            foreach(string arg in new[]{"-game","cstrike","-insecure","+connect",Updates.GameAddress(release.GameAddress)})info.ArgumentList.Add(arg);
            Process.Start(info);SetStatus("游戏已启动，正在连接服务器。");
        });
        cancel.Click+=(_,_)=>{work?.Cancel();SetStatus("正在取消，请稍候…");};
        refresh.Click+=async(_,_)=>await RefreshServer();settings.Click+=(_,_)=>ServerSettings();
        timer.Tick+=async(_,_)=>await RefreshServer();
        Shown+=async(_,_)=>
        {
            try{var found=await Task.Run(SteamDiscovery.Find);game.Items.AddRange(found.Cast<object>().ToArray());if(string.IsNullOrWhiteSpace(game.Text)&&found.Count>0)game.Text=found[0];}catch(Exception ex){SetStatus("未能自动识别，请手动选择目录。"+ex.Message);}
            await CheckInstalled();await RefreshServer();timer.Start();
        };
        FormClosing+=(_,e)=>{if(busy){e.Cancel=true;work?.Cancel();SetStatus("正在结束当前操作，请稍后关闭。");}else{timer.Stop();lifetime.Cancel();SavePreferences();}};
    }
    private static Label TextLabel(string text,float size,FontStyle style=FontStyle.Regular)=>new(){Text=text,AutoSize=true,MaximumSize=new(640,0),Font=new("Microsoft YaHei UI",size,style),ForeColor=style==FontStyle.Regular?Muted:Ink,Margin=new(0,2,0,3),AutoEllipsis=true};
    private static Button Button(string text)=>new(){Text=text,Dock=DockStyle.Fill,FlatStyle=FlatStyle.Flat,BackColor=Color.White,Margin=new(6,2,0,4),Height=34,Cursor=Cursors.Hand,UseVisualStyleBackColor=false};
    private static TableLayoutPanel Card()=>new(){Dock=DockStyle.Fill,BackColor=Color.White,Padding=new(18,12,18,12),Margin=new(0,0,0,12)};
    private string NormalizeGame(){string value=game.Text.Trim().Trim('"');if(Path.GetFileName(value).Equals("cstrike",StringComparison.OrdinalIgnoreCase))value=Path.GetDirectoryName(value)!;return SafeFiles.Plain(value);}
    private void SetStatus(string text){operation.Text=text;operation.ForeColor=Muted;}
    private async Task CheckInstalled()
    {
        int check=++installCheck;launch.Enabled=false;string path=game.Text.Trim();
        try{if(path.Length>0)path=NormalizeGame();}catch(ArgumentException){}catch(IOException){}
        var result=await Task.Run(()=>bundle.Installed(path));
        if(IsDisposed||check!=installCheck)return;
        installed=result;game.Text=path;
        installState.Text=installed==null?"尚未安装或文件需要修复 · 请先点击安装":"已安装 MetaHook · "+installed.Version;
        install.Text=installed==null?"安装 / 修复":"已安装";install.Enabled=!busy&&installed==null&&path.Length>0;launch.Enabled=!busy&&installed!=null;
        if(installed!=null&&operation.Text=="选择游戏目录，安装后即可启动。")SetStatus("准备就绪。点击启动游戏，自动检查更新并连接服务器。");
        SavePreferences();
    }
    private async Task Run(Func<CancellationToken,Task> operationTask)
    {
        if(busy)return;busy=true;++installCheck;work=new();game.Enabled=browse.Enabled=install.Enabled=launch.Enabled=settings.Enabled=false;((TableLayoutPanel)launch.Parent!).SetColumnSpan(launch,1);cancel.Visible=progress.Visible=true;
        try{await operationTask(work.Token);}
        catch(OperationCanceledException){SetStatus(work.IsCancellationRequested?"已取消。已安装文件保持可恢复状态。":"连接超时，请检查网络后重试。");}
        catch(HttpRequestException ex){SetStatus(ex.StatusCode==System.Net.HttpStatusCode.NotFound?"服务器尚未发布启动器更新，请联系服主启用更新服务。":"无法连接更新服务器，请检查网络或服务器下载地址。");operation.ForeColor=Color.FromArgb(170,49,39);}
        catch(Exception ex){SetStatus(ex is UnauthorizedAccessException?"目录没有写入权限，请选择可写目录或以管理员身份运行启动器。":ex.Message);operation.ForeColor=Color.FromArgb(170,49,39);}
        finally{busy=false;work.Dispose();work=null;game.Enabled=browse.Enabled=settings.Enabled=true;cancel.Visible=progress.Visible=false;((TableLayoutPanel)launch.Parent!).SetColumnSpan(launch,2);await CheckInstalled();}
    }
    private async Task RefreshServer()
    {
        if(refreshing||IsDisposed)return;refreshing=true;refresh.Enabled=false;
        using var timeout=CancellationTokenSource.CreateLinkedTokenSource(lifetime.Token);timeout.CancelAfter(TimeSpan.FromSeconds(5));
        try
        {
            using var updates=new Updates(bundle.Config,updateBase);var state=await updates.Status(timeout.Token);
            if(IsDisposed)return;
            serverName.Text=state.Online?state.Name:"服务器暂未开放";serverName.ForeColor=state.Online?Accent:Muted;
            serverInfo.Text=state.Online?$"{state.Map}    ·    在线 {state.Players} / {state.MaxPlayers}":"请稍后刷新服务器状态";
            address.Text=state.GameAddress;
        }
        catch(Exception ex) when(ex is not OutOfMemoryException)
        {
            if(IsDisposed)return;serverName.Text="暂时无法获取服务器状态";serverName.ForeColor=Muted;
            serverInfo.Text="检查网络或下载地址，然后点击刷新。";address.Text=updateBase;
        }
        finally{refreshing=false;if(!IsDisposed)refresh.Enabled=true;}
    }
    private void ServerSettings()
    {
        using var form=new Form{Text="服务器下载地址",ClientSize=new(610,158),FormBorderStyle=FormBorderStyle.FixedDialog,MaximizeBox=false,MinimizeBox=false,StartPosition=FormStartPosition.CenterParent,Font=Font};
        var label=new Label{Text="与服务器 HTTP 下载共用端口",AutoSize=true,Left=18,Top=15};
        var box=new TextBox{Text=updateBase,Left=18,Top=45,Width=570};
        var save=new Button{Text="保存",DialogResult=DialogResult.None,Left=488,Top=95,Width=100};
        save.Click+=(_,_)=>{try{Updates.BaseUri(box.Text.Trim());updateBase=box.Text.Trim();SavePreferences();form.DialogResult=DialogResult.OK;}catch(Exception ex){MessageBox.Show(form,ex.Message,"地址格式",MessageBoxButtons.OK,MessageBoxIcon.Information);}};
        form.Controls.AddRange([label,box,save]);form.AcceptButton=save;
        if(form.ShowDialog(this)==DialogResult.OK)_=RefreshServer();
    }
    private Preferences LoadPreferences(){try{return JsonData.Read<Preferences>(File.ReadAllBytes(SafeFiles.Under(data,"preferences.json")));}catch(Exception ex)when(ex is IOException or InvalidDataException or UnauthorizedAccessException or System.Text.Json.JsonException){return new();}}
    private void SavePreferences(){try{SafeFiles.WriteAtomic(SafeFiles.Under(data,"preferences.json"),JsonData.Write(new Preferences(game.Text.Trim(),updateBase)));}catch(IOException){}catch(UnauthorizedAccessException){}}
    protected override void Dispose(bool disposing){if(disposing){timer.Dispose();lifetime.Cancel();lifetime.Dispose();work?.Dispose();}base.Dispose(disposing);}
}
