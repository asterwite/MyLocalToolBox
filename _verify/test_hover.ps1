# 悬停模式拖拽验证：把桌宠拖到半空松手，观察是否悬停
Add-Type @'
using System;
using System.Runtime.InteropServices;
public class D {
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern void mouse_event(uint f, uint dx, uint dy, uint d, UIntPtr e);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  public struct RECT { public int L, T, R, B; }
}
'@
Add-Type -AssemblyName System.Windows.Forms
$p = Get-Process ToolBoxQt | Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1
# 枚举本进程所有可见顶层窗口，找高度 150-320 的桌宠窗口
Add-Type @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public class E {
  public delegate bool CB(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(CB c, IntPtr l);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT2 r);
  public struct RECT2 { public int L, T, R, B; }
}
'@
$petH = [IntPtr]::Zero
$cb = [E+CB]{ param($h, $l)
  $pid2 = 0
  [E]::GetWindowThreadProcessId($h, [ref]$pid2) | Out-Null
  $pr = Get-Process -Id $pid2 -ErrorAction SilentlyContinue
  if ($pr -and $pr.Id -eq $p.Id -and [E]::IsWindowVisible($h)) {
    $r = New-Object E+RECT2
    [E]::GetWindowRect($h, [ref]$r) | Out-Null
    $hgt = $r.B - $r.T
    if ($hgt -gt 150 -and $hgt -lt 330) { $script:petH = $h }
  }
  return $true
}
[E]::EnumWindows($cb, [IntPtr]::Zero) | Out-Null
if ($petH -eq [IntPtr]::Zero) { Write-Host "pet window not found"; exit 1 }
$r = New-Object E+RECT2
[D]::GetWindowRect($petH, [ref]$r) | Out-Null
$cx = [int](($r.L + $r.R) / 2); $cy = [int](($r.T + $r.B) / 2)
Write-Host ("pet rect: L=" + $r.L + " T=" + $r.T + " R=" + $r.R + " B=" + $r.B)
[D]::SetForegroundWindow($petH) | Out-Null
Start-Sleep -Milliseconds 300
[D]::SetCursorPos($cx, $cy) | Out-Null
Start-Sleep -Milliseconds 150
[D]::mouse_event(0x0002, 0, 0, 0, [UIntPtr]::Zero)  # down
Start-Sleep -Milliseconds 120
for ($i = 1; $i -le 10; $i++) {
    [D]::SetCursorPos($cx, $cy - $i * 15) | Out-Null
    Start-Sleep -Milliseconds 40
}
Start-Sleep -Milliseconds 200
[D]::mouse_event(0x0004, 0, 0, 0, [UIntPtr]::Zero)  # up（慢速松手=轻放）
Write-Host "released at ~$($cy - 150)"
for ($k = 0; $k -lt 4; $k++) {
    Start-Sleep -Milliseconds 700
    $r2 = New-Object E+RECT2
    [D]::GetWindowRect($petH, [ref]$r2) | Out-Null
    Write-Host ("t+" + ($k * 0.7) + "s  T=" + $r2.T + "  B=" + $r2.B)
}
