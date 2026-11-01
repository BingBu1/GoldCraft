namespace GoldCraft.Launcher;

internal static class Program
{
    [STAThread]
    private static void Main(string[] args)
    {
        ApplicationConfiguration.Initialize();
        try
        {
            string? Option(string name){int i=Array.IndexOf(args,name);return i>=0&&i+1<args.Length?args[i+1]:null;}
            string data=Option("--data-root")??Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),"GoldCraftLauncher");
            SafeFiles.Plain(data);Directory.CreateDirectory(data);
            using var single=new Mutex(true,@"Local\GoldCraftLauncher-"+Convert.ToHexString(System.Security.Cryptography.SHA256.HashData(System.Text.Encoding.UTF8.GetBytes(data.ToUpperInvariant())))[..16],out bool first);
            if(!first){MessageBox.Show("启动器已经打开。","GoldCraft",MessageBoxButtons.OK,MessageBoxIcon.Information);return;}
            var form=new LauncherForm(Bundle.Embedded(),data,Option("--game-directory"),Option("--server-url"));
            // Developer layout check of this app only; does not capture the desktop or operate other windows.
            if(Option("--render-preview") is string preview)
                form.Shown+=async(_,_)=>
                {
                    await Task.Delay(7000);
                    using var bitmap=new Bitmap(form.Width,form.Height);
                    form.DrawToBitmap(bitmap,new Rectangle(0,0,form.Width,form.Height));
                    bitmap.Save(SafeFiles.Plain(preview),System.Drawing.Imaging.ImageFormat.Png);
                    form.Close();
                };
            Application.Run(form);
        }
        catch(Exception ex){MessageBox.Show(ex.Message,"无法打开 GoldCraft 启动器",MessageBoxButtons.OK,MessageBoxIcon.Error);}
    }
}
