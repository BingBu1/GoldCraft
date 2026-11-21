using System;
using System.Diagnostics;
using System.IO;
using System.Threading;

// An owned fixture process, never a game. It uses the shipped console-command
// file contract and holds a real Windows sharing lock on the engine payload.
class Worker
{
    static int Main(string[] args)
    {
        string root=args[0],role=args[1],state=Path.Combine(root,".state");
        Directory.CreateDirectory(state);
        string run=Guid.NewGuid().ToString("N").Substring(0,24);
        using(var process=Process.GetCurrentProcess())
        using(var lease=new FileStream(Path.Combine(root,"cs/swds.dll"),FileMode.Open,FileAccess.Read,FileShare.Read))
        {
            string text="{\"pid\":"+process.Id+",\"startTicks\":\""+process.StartTime.ToUniversalTime().Ticks+
                "\",\"runId\":\""+run+"\",\"role\":\""+role+"\"}";
            string record = Path.Combine(state, role+".json");
            string pending = Path.Combine(state, run+".tmp");
            File.WriteAllText(pending, text);
            if (File.Exists(record)) File.Replace(pending, record, null);
            else File.Move(pending, record);
            string command=Path.Combine(state,run+".command");
            for(int i=0;i<18000;i++)
            {
                if(File.Exists(command))
                {
                    string value=File.ReadAllText(command).Trim();
                    File.Delete(command);
                    if(value=="quit"||value=="stop")return 0;
                }
                Thread.Sleep(20);
            }
        }
        return 2;
    }
}
