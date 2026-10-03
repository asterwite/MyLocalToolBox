// Hover-mode verify: drag pet up 150px, release gently, sample window pos 2.1s.
// C# 5 compatible. Build: csc /out:test_hover.exe test_hover.cs
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Runtime.InteropServices;

class HoverTest {
    [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc cb, IntPtr lp);
    [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("user32.dll")] static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] static extern void mouse_event(uint f, uint dx, uint dy, uint d, UIntPtr e);
    [DllImport("user32.dll")] static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] static extern IntPtr WindowFromPoint(POINT p);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern int GetWindowText(IntPtr h, System.Text.StringBuilder t, int n);
    struct POINT { public int X, Y; public POINT(int x, int y) { X = x; Y = y; } }
    delegate bool EnumProc(IntPtr h, IntPtr lp);
    static EnumProc s_proc;
    struct RECT { public int L, T, R, B; }

    static IntPtr s_petH = IntPtr.Zero;
    static RECT s_petR;

    static void Main() {
        var procs = new HashSet<uint>();
        foreach (var p in Process.GetProcessesByName("ToolBoxQt")) procs.Add((uint)p.Id);
        if (procs.Count == 0) { Console.WriteLine("NO-PROCESS"); return; }
        s_proc = delegate(IntPtr hWnd, IntPtr lp) {
            if (!IsWindowVisible(hWnd)) return true;
            uint pid; GetWindowThreadProcessId(hWnd, out pid);
            if (!procs.Contains(pid)) return true;
            RECT r; GetWindowRect(hWnd, out r);
            int ww = r.R - r.L, hh = r.B - r.T;
            if (ww >= 180 && ww <= 520 && hh >= 100 && hh <= 320) { s_petH = hWnd; s_petR = r; }
            return true;
        };
        EnumWindows(s_proc, IntPtr.Zero);
        if (s_petH == IntPtr.Zero) { Console.WriteLine("NO-PET-WINDOW"); return; }
        Console.WriteLine("pet rect " + s_petR.L + "," + s_petR.T + " " + (s_petR.R - s_petR.L) + "x" + (s_petR.B - s_petR.T));

        int cx = (s_petR.L + s_petR.R) / 2, cy = (s_petR.T + s_petR.B) / 2;
        var wpt = new POINT(cx, cy);
        IntPtr topH = WindowFromPoint(wpt);
        var tb = new System.Text.StringBuilder(128);
        GetWindowText(topH, tb, 128);
        Console.WriteLine("window under click point: [" + tb + "] h=" + topH + " (pet h=" + s_petH + ")");
        SetForegroundWindow(s_petH);
        System.Threading.Thread.Sleep(300);
        SetCursorPos(cx, cy); System.Threading.Thread.Sleep(150);
        mouse_event(0x0002, 0, 0, 0, UIntPtr.Zero); // down
        for (int i = 1; i <= 6; ++i) {
            SetCursorPos(cx, cy - i * 30);
            System.Threading.Thread.Sleep(30);
        }
        mouse_event(0x0004, 0, 0, 0, UIntPtr.Zero); // up 放置在半空
        System.Threading.Thread.Sleep(800);
        RECT rp; GetWindowRect(s_petH, out rp);
        Console.WriteLine("placed: T=" + rp.T);

        // 单击（按下即抬起，不移动）：悬空放置的桌宠不应被按回地面
        int ccx = (rp.L + rp.R) / 2, ccy = (rp.T + rp.B) / 2;
        SetCursorPos(ccx, ccy); System.Threading.Thread.Sleep(120);
        mouse_event(0x0002, 0, 0, 0, UIntPtr.Zero);
        System.Threading.Thread.Sleep(80);
        mouse_event(0x0004, 0, 0, 0, UIntPtr.Zero);
        Console.WriteLine("clicked at (" + ccx + "," + ccy + ")");
        for (int k = 0; k < 4; ++k) {
            System.Threading.Thread.Sleep(700);
            RECT r2; GetWindowRect(s_petH, out r2);
            Console.WriteLine("t+" + (k * 0.7).ToString("0.0") + "s  T=" + r2.T + "  B=" + r2.B);
        }
        Console.WriteLine("released at y~" + (cy - 150));
        for (int k = 0; k < 4; ++k) {
            System.Threading.Thread.Sleep(700);
            RECT r2; GetWindowRect(s_petH, out r2);
            Console.WriteLine("t+" + (k * 0.7).ToString("0.0") + "s  T=" + r2.T + "  B=" + r2.B);
        }
    }
}
