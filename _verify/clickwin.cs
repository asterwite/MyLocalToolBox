// Foreground the ToolBoxQt main window and click at (dx,dy) relative to its top-left.
// C# 5 compatible. Build: csc -out:clickwin.exe clickwin.cs
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Runtime.InteropServices;

class ClickWin {
    [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc cb, IntPtr lp);
    [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("user32.dll")] static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] static extern void mouse_event(uint f, uint dx, uint dy, uint d, int e);
    delegate bool EnumProc(IntPtr h, IntPtr lp);
    static EnumProc s_proc;
    struct RECT { public int L, T, R, B; }

    static void Main(string[] args) {
        if (args.Length < 2) { Console.WriteLine("usage: clickwin dx dy"); return; }
        int dx = int.Parse(args[0]), dy = int.Parse(args[1]);
        var procs = new HashSet<uint>();
        foreach (var p in Process.GetProcessesByName("ToolBoxQt")) procs.Add((uint)p.Id);
        if (procs.Count == 0) { Console.WriteLine("NO-PROCESS"); return; }
        IntPtr best = IntPtr.Zero; RECT br = new RECT(); int bestArea = 0;
        s_proc = delegate(IntPtr hWnd, IntPtr lp) {
            if (!IsWindowVisible(hWnd)) return true;
            uint pid; GetWindowThreadProcessId(hWnd, out pid);
            if (!procs.Contains(pid)) return true;
            RECT r; GetWindowRect(hWnd, out r);
            int area = (r.R - r.L) * (r.B - r.T);
            if (area > bestArea) { bestArea = area; br = r; best = hWnd; }
            return true;
        };
        EnumWindows(s_proc, IntPtr.Zero);
        if (best == IntPtr.Zero || bestArea < 200000) { Console.WriteLine("NO-MAIN"); return; }
        SetForegroundWindow(best);
        System.Threading.Thread.Sleep(500);
        int sx = br.L + dx, sy = br.T + dy;
        SetCursorPos(sx, sy);
        System.Threading.Thread.Sleep(150);
        mouse_event(2, 0, 0, 0, 0);
        System.Threading.Thread.Sleep(60);
        mouse_event(4, 0, 0, 0, 0);
        Console.WriteLine("CLICKED rel " + dx + "," + dy + " abs " + sx + "," + sy);
    }
}
