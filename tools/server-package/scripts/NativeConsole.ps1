# ReHLDS needs real console input handles. Process.Start under a redirected
# PowerShell may inherit pipe handles even though hlds allocates a console.
Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.Diagnostics;
using System.Runtime.InteropServices;
public static class GoldCraftNativeConsole {
    [StructLayout(LayoutKind.Sequential, CharSet=CharSet.Unicode)]
    struct Startup {
        public int cb; public string reserved, desktop, title;
        public int x,y,width,height,xchars,ychars,fill,flags;
        public short show,reservedSize;
        public IntPtr reservedBytes,input,output,error;
    }
    [StructLayout(LayoutKind.Sequential)]
    struct Child { public IntPtr process,thread; public int pid,tid; }
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    static extern bool CreateProcessW(string app, System.Text.StringBuilder command,
        IntPtr processSecurity,IntPtr threadSecurity,bool inherit,int flags,
        IntPtr environment,string cwd,ref Startup startup,out Child child);
    [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr handle);
    [DllImport("user32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    static extern IntPtr CreateDesktopW(string name,IntPtr device,IntPtr mode,int flags,uint access,IntPtr security);
    [DllImport("user32.dll", SetLastError=true)] static extern bool CloseDesktop(IntPtr desktop);
    static IntPtr testDesktop;
    public static Process Start(string executable,string arguments,string directory,bool hidden) {
        var si=new Startup {cb=Marshal.SizeOf(typeof(Startup)),flags=1,show=(short)(hidden?0:1)};
        if(hidden) {
            string name="GoldCraftPackage_"+Guid.NewGuid().ToString("N");
            testDesktop=CreateDesktopW(name,IntPtr.Zero,IntPtr.Zero,0,0x10000000,IntPtr.Zero);
            if(testDesktop==IntPtr.Zero) throw new Win32Exception(Marshal.GetLastWin32Error());
            si.desktop=name; // Never switch the operator's active desktop or focus.
        }
        Child child;
        // CREATE_NEW_CONSOLE, no handle inheritance and no STARTF_USESTDHANDLES.
        if(!CreateProcessW(executable,new System.Text.StringBuilder("\""+executable+"\" "+arguments),
            IntPtr.Zero,IntPtr.Zero,false,0x10,IntPtr.Zero,directory,ref si,out child)) {
            int code=Marshal.GetLastWin32Error(); Release(); throw new Win32Exception(code);
        }
        try {
            var process=Process.GetProcessById(child.pid);
            var held=process.Handle; // Retain identity/exit status before releasing CreateProcess's handle.
            return process;
        }
        finally { CloseHandle(child.thread);CloseHandle(child.process); }
    }
    public static void Release() {
        if(testDesktop!=IntPtr.Zero) {CloseDesktop(testDesktop);testDesktop=IntPtr.Zero;}
    }
}
'@
