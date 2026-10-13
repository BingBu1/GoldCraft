param([Parameter(Mandatory)][int]$ProcessId,
      [Parameter(Mandatory)][string]$StartTicks,
      [Parameter(Mandatory)][string]$CommandFile)
$ErrorActionPreference = 'Stop'
try {
    $target = Get-Process -Id $ProcessId -ErrorAction Stop
    if ($target.StartTime.ToUniversalTime().Ticks.ToString() -ne $StartTicks) { throw 'Server process identity changed.' }
    $text = [IO.File]::ReadAllText($CommandFile)
    if ($text.Length -gt 512 -or $text -match '[\r\n\x00]') { throw 'Invalid console command.' }
    Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;
public static class GoldCraftConsole {
    [StructLayout(LayoutKind.Explicit, CharSet=CharSet.Unicode, Size=20)]
    public struct Input {
        [FieldOffset(0)] public short Type;
        [FieldOffset(4)] public int Down;
        [FieldOffset(8)] public short Repeat;
        [FieldOffset(10)] public short VirtualKey;
        [FieldOffset(12)] public short ScanCode;
        [FieldOffset(14)] public char Character;
        [FieldOffset(16)] public int Control;
    }
    [DllImport("kernel32.dll")] static extern bool FreeConsole();
    [DllImport("kernel32.dll", SetLastError=true)] static extern bool AttachConsole(uint pid);
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    static extern SafeFileHandle CreateFileW(string name, uint access, uint share, IntPtr security, uint mode, uint flags, IntPtr template);
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    static extern bool WriteConsoleInputW(SafeFileHandle handle, Input[] input, int count, out int written);
    public static void Send(uint pid, string text) {
        // ReHLDS is a GUI-subsystem executable that allocates its own console.
        // This short-lived helper must attach to the verified child, not its PS parent.
        FreeConsole();
        if (!AttachConsole(pid)) throw new Win32Exception(Marshal.GetLastWin32Error());
        try {
            using (var input=CreateFileW("CONIN$", 0x40000000, 3, IntPtr.Zero, 3, 0, IntPtr.Zero)) {
                if (input.IsInvalid) throw new Win32Exception(Marshal.GetLastWin32Error());
                var line=text+"\r";
                var data=new Input[line.Length*2];
                for(int i=0;i<line.Length;++i) {
                    var key=new Input {Type=1,Down=1,Repeat=1,Character=line[i],VirtualKey=(short)(line[i]=='\r'?13:0)};
                    data[i*2]=key;key.Down=0;data[i*2+1]=key;
                }
                int written;
                if (!WriteConsoleInputW(input,data,data.Length,out written) || written!=data.Length)
                    throw new Win32Exception(Marshal.GetLastWin32Error());
            }
        } finally { FreeConsole(); }
    }
}
'@
    [GoldCraftConsole]::Send([uint32]$ProcessId, $text)
} catch {
    [IO.File]::WriteAllText("$CommandFile.error", $_.Exception.Message)
    exit 1
}
