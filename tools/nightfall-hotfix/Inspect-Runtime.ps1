param([string]$TargetDirectory='E:\cs_MineCraft\GoldCraft-Servers', [int]$ProcessId=0)
$ErrorActionPreference='Stop'
# Queries and ReadProcessMemory only: no commands, process stops or file writes.
Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Collections.Generic;
using System.IO;
using System.Text;
public static class GoldCraftReadOnlyImage {
    [DllImport("kernel32.dll", SetLastError=true)] static extern IntPtr OpenProcess(uint access, bool inherit, int pid);
    [DllImport("kernel32.dll", SetLastError=true)] static extern bool ReadProcessMemory(IntPtr process, IntPtr address, byte[] data, UIntPtr size, out UIntPtr received);
    [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr value);
    [DllImport("psapi.dll", SetLastError=true)] static extern bool EnumProcessModulesEx(IntPtr process, IntPtr[] modules, int size, out int needed, int filter);
    [DllImport("psapi.dll", CharSet=CharSet.Unicode, SetLastError=true)] static extern int GetModuleFileNameEx(IntPtr process, IntPtr module, StringBuilder path, int size);
    public sealed class Module { public string Name; public string Path; public IntPtr Base; }
    public static Module[] Modules(int pid) {
        var process=OpenProcess(0x410,false,pid);
        if(process==IntPtr.Zero) throw new Win32Exception();
        try {
            var modules=new IntPtr[1024];int needed;
            if(!EnumProcessModulesEx(process,modules,modules.Length*IntPtr.Size,out needed,3))throw new Win32Exception();
            if(needed>modules.Length*IntPtr.Size)throw new InvalidOperationException("Module bound exceeded");
            var result=new List<Module>();
            for(int i=0;i<needed/IntPtr.Size;i++){
                var path=new StringBuilder(32768);
                if(GetModuleFileNameEx(process,modules[i],path,path.Capacity)==0)throw new Win32Exception();
                result.Add(new Module {Name=System.IO.Path.GetFileName(path.ToString()),Path=path.ToString(),Base=modules[i]});
            }
            return result.ToArray();
        }finally{CloseHandle(process);}
    }
    public static byte[] Read(int pid, long address, int count) {
        if (address <= 0 || count <= 0 || count > 1048576) throw new ArgumentOutOfRangeException();
        var process=OpenProcess(0x410, false, pid);
        if (process==IntPtr.Zero) throw new Win32Exception();
        try {
            var data=new byte[count]; UIntPtr received;
            if (!ReadProcessMemory(process,new IntPtr(address),data,(UIntPtr)count,out received) || received.ToUInt64()!=(ulong)count)
                throw new Win32Exception();
            return data;
        } finally { CloseHandle(process); }
    }
}
'@
function U32([byte[]]$Bytes,[int]$Offset){[BitConverter]::ToUInt32($Bytes,$Offset)}
function Loaded-Identity([int]$Id,[long]$Base){
    $dos=[GoldCraftReadOnlyImage]::Read($Id,$Base,64)
    if($dos[0]-ne 77 -or $dos[1]-ne 90){throw 'Loaded image has no DOS header'}
    $pe=[GoldCraftReadOnlyImage]::Read($Id,$Base+(U32 $dos 60),248)
    if((U32 $pe 0)-ne 17744 -or [BitConverter]::ToUInt16($pe,24)-ne 267){throw 'Expected PE32 image'}
    $directory=U32 $pe 168;$size=U32 $pe 172
    if(!$directory -or $size-gt 4096 -or $size%28){throw 'Unexpected debug directory'}
    $debug=[GoldCraftReadOnlyImage]::Read($Id,$Base+$directory,$size)
    for($i=0;$i-lt $size;$i+=28){
        if((U32 $debug ($i+12))-ne 2){continue}
        $record=[GoldCraftReadOnlyImage]::Read($Id,$Base+(U32 $debug ($i+20)),24)
        if((U32 $record 0)-ne 1396986706){throw 'Expected RSDS debug record'}
        return ([BitConverter]::ToString($record)).Replace('-','').ToLowerInvariant()
    }
    throw 'Loaded module has no CodeView identity'
}
$root=[IO.Path]::GetFullPath($TargetDirectory).TrimEnd('\')
$image=Join-Path $root 'cs\cstrike\dlls\mp.dll'
$expected='52534453baf25f6c749b443c4c4c44205044422e01000000'
$report=[ordered]@{readOnly=$true;expectedMP='60ba4f3f929a4afc725fde721577a08ebb4ef086f8248fdf53dce73515dd891f';expectedLoadedIdentity=$expected;diskMP=$null;damageFile=$null;cofLog=@();processes=@()}
if(Test-Path -LiteralPath $image -PathType Leaf){$report.diskMP=(Get-FileHash -LiteralPath $image -Algorithm SHA256).Hash.ToLowerInvariant()}
$damage=Join-Path $root 'cs\cstrike\configs\guest\cof\scenario_damage.json'
if(Test-Path -LiteralPath $damage -PathType Leaf){
    $file=Get-Item -LiteralPath $damage
    $report.damageFile=[ordered]@{bytes=$file.Length;sha256=(Get-FileHash -LiteralPath $damage -Algorithm SHA256).Hash.ToLowerInvariant()}
}
$console=Join-Path $root 'cs\qconsole.log'
if(Test-Path -LiteralPath $console -PathType Leaf){
    $report.cofLog=@(Get-Content -LiteralPath $console -Tail 5000 | Where-Object {$_ -match '\[CoF\]'} | Select-Object -Last 20)
}
$targets=if($ProcessId){@(Get-Process -Id $ProcessId)}else{@(Get-Process -Name hlds -ErrorAction SilentlyContinue)}
foreach($process in $targets){
    $row=[ordered]@{pid=$process.Id;executable=$null;modules=@();error=$null}
    try {
        $row.executable=$process.MainModule.FileName
        foreach($module in [GoldCraftReadOnlyImage]::Modules($process.Id)){
            if($module.Name -ine 'mp.dll' -and $module.Name -ine 'swds.dll'){continue}
            $identity=Loaded-Identity $process.Id $module.Base.ToInt64()
            $row.modules+=@([ordered]@{name=$module.Name;path=$module.Path;loadedIdentity=$identity;hotfixMPMatches=($module.Name -ieq 'mp.dll' -and $identity -ceq $expected)})
        }
    } catch {$row.error=$_.Exception.Message}
    $report.processes+=@($row)
}
$report|ConvertTo-Json -Depth 6
