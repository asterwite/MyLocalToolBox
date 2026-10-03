// Pet animation verify: find ToolBoxQt pet window (small frameless), shoot 3 frames, diff pixels.
// C# 5 compatible (old csc). Build: csc /out:petshot.exe petshot.cs
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
using System.Text;

class PetShot {
    [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc cb, IntPtr lp);
    [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("user32.dll")] static extern bool GetWindowRect(IntPtr h, out RECT r);
    delegate bool EnumProc(IntPtr h, IntPtr lp);
    static EnumProc s_proc; // static to prevent GC
    struct RECT { public int L, T, R, B; }
    class PetWin { public IntPtr H; public RECT R; }

    static void Main() {
        var procs = new HashSet<uint>();
        foreach (var p in Process.GetProcessesByName("ToolBoxQt")) procs.Add((uint)p.Id);
        if (procs.Count == 0) { Console.WriteLine("NO-PROCESS"); return; }

        var pets = new List<PetWin>();
        s_proc = delegate(IntPtr hWnd, IntPtr lp) {
            if (!IsWindowVisible(hWnd)) return true;
            uint pid; GetWindowThreadProcessId(hWnd, out pid);
            if (!procs.Contains(pid)) return true;
            RECT r; GetWindowRect(hWnd, out r);
            int ww = r.R - r.L, hh = r.B - r.T;
            // pet: small frameless window ~320x180 (240/320/420 wide, 16:9)
            if (ww >= 180 && ww <= 520 && hh >= 100 && hh <= 320) {
                var pw = new PetWin(); pw.H = hWnd; pw.R = r; pets.Add(pw);
                Console.WriteLine("pet rect " + r.L + "," + r.T + " " + ww + "x" + hh);
            }
            return true;
        };
        EnumWindows(s_proc, IntPtr.Zero);
        if (pets.Count == 0) { Console.WriteLine("NO-PET-WINDOW"); return; }
        RECT rc = pets[0].R;
        int w = rc.R - rc.L, h = rc.B - rc.T;

        Bitmap[] shots = new Bitmap[3];
        for (int i = 0; i < 3; i++) {
            var bmp = new Bitmap(w, h, PixelFormat.Format32bppArgb);
            using (var g = Graphics.FromImage(bmp)) g.CopyFromScreen(rc.L, rc.T, 0, 0, new Size(w, h));
            shots[i] = bmp;
            if (i < 2) System.Threading.Thread.Sleep(800);
        }
        for (int i = 0; i < 2; i++) {
            int diff = 0;
            for (int y = 0; y < h; y += 2)
                for (int x = 0; x < w; x += 2)
                    if (shots[i].GetPixel(x, y) != shots[i + 1].GetPixel(x, y)) diff++;
            Console.WriteLine("diff shots" + i + "->" + (i + 1) + " (sampled) = " + diff);
        }
        int sw = Math.Min(400, w);
        double sc = (double)sw / w;
        int sh = (int)(h * sc);
        using (var strip = new Bitmap(sw * 3 + 20, sh + 24)) {
            using (var g = Graphics.FromImage(strip)) {
                g.Clear(Color.FromArgb(60, 62, 80));
                for (int i = 0; i < 3; i++) {
                    using (var sm = new Bitmap(shots[i], sw, sh)) g.DrawImage(sm, i * (sw + 10), 12);
                    g.DrawString("t+" + (i * 0.8).ToString("0.0") + "s", SystemFonts.DefaultFont, Brushes.White, i * (sw + 10) + 4, 0);
                }
            }
            string dir = AppDomain.CurrentDomain.BaseDirectory;
            strip.Save(dir + "pet_strip.png", ImageFormat.Png);
            Console.WriteLine("SAVED " + dir + "pet_strip.png");
        }
    }
}
