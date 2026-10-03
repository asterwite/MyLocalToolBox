// List all visible windows of ToolBoxQt process with rects. Build: csc -out:petrect.exe petrect.cs
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Runtime.InteropServices;

class PetRect {
    [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc cb, IntPtr lp);
    [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("user32.dll")] static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern int GetWindowTextW(IntPtr h, System.Text.StringBuilder sb, int max);
    delegate bool EnumProc(IntPtr h, IntPtr lp);
    static EnumProc s_proc;
    struct RECT { public int L, T, R, B; }

    static void Main() {
        var procs = new HashSet<uint>();
        foreach (var p in Process.GetProcessesByName("ToolBoxQt")) procs.Add((uint)p.Id);
        if (procs.Count == 0) { Console.WriteLine("NO-PROCESS"); return; }
        s_proc = delegate(IntPtr hWnd, IntPtr lp) {
            if (!IsWindowVisible(hWnd)) return true;
            uint pid; GetWindowThreadProcessId(hWnd, out pid);
            if (!procs.Contains(pid)) return true;
            RECT r; GetWindowRect(hWnd, out r);
            int w = r.R - r.L, h2 = r.B - r.T;
            var sb = new System.Text.StringBuilder(128);
            GetWindowTextW(hWnd, sb, 128);
            string title = sb.ToString();
            if (title.Length > 20) title = title.Substring(0, 20) + "...";
            Console.WriteLine("HWND " + hWnd + " rect " + r.L + "," + r.T + " " + w + "x" + h2 + " area " + (w * h2) + " title [" + title + "]");
            return true;
        };
        EnumWindows(s_proc, IntPtr.Zero);
    }
}
