// Screenshot the ToolBoxQt MAIN window (large): find by process + biggest visible rect.
// C# 5 compatible. Build: csc /out:winshot.exe winshot.cs
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;

class WinShot {
    [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc cb, IntPtr lp);
    [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("user32.dll")] static extern bool GetWindowRect(IntPtr h, out RECT r);
    delegate bool EnumProc(IntPtr h, IntPtr lp);
    static EnumProc s_proc;
    struct RECT { public int L, T, R, B; }

    static void Main() {
        var procs = new HashSet<uint>();
        foreach (var p in Process.GetProcessesByName("ToolBoxQt")) procs.Add((uint)p.Id);
        if (procs.Count == 0) { Console.WriteLine("NO-PROCESS"); return; }
        RECT best = new RECT(); int bestArea = 0; bool found = false;
        s_proc = delegate(IntPtr hWnd, IntPtr lp) {
            if (!IsWindowVisible(hWnd)) return true;
            uint pid; GetWindowThreadProcessId(hWnd, out pid);
            if (!procs.Contains(pid)) return true;
            RECT r; GetWindowRect(hWnd, out r);
            int area = (r.R - r.L) * (r.B - r.T);
            if (area > bestArea) { bestArea = area; best = r; found = true; }
            return true;
        };
        EnumWindows(s_proc, IntPtr.Zero);
        if (!found || bestArea < 200000) { Console.WriteLine("NO-MAIN"); return; }
        int w = best.R - best.L, h = best.B - best.T;
        using (var bmp = new Bitmap(w, h, PixelFormat.Format32bppArgb))
        using (var g = Graphics.FromImage(bmp)) {
            g.CopyFromScreen(best.L, best.T, 0, 0, new Size(w, h));
            string dir = AppDomain.CurrentDomain.BaseDirectory;
            bmp.Save(dir + "main_win.png", ImageFormat.Png);
            Console.WriteLine("SAVED " + dir + "main_win.png rect " + best.L + "," + best.T + " " + w + "x" + h);
        }
    }
}
