param(
    [string]$ExeDir = "D:\Berton\Tank-Battle\tank-battle-cpp",
    [string]$Phase = "title",      # title | playing
    [string]$OutPng = "shot.png",
    [int]$WinW = 976,              # window outer size (default = 960x720 client + borders)
    [int]$WinH = 759
)
Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Windows.Forms

Add-Type @"
using System;
using System.Runtime.InteropServices;
public class M {
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern void mouse_event(uint f, uint dx, uint dy, uint d, UIntPtr e);
    [DllImport("user32.dll")] public static extern bool MoveWindow(IntPtr h, int x, int y, int w, int ht, bool r);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
    public struct RECT { public int L, T, R, B; }
}
"@
[M]::SetProcessDPIAware() | Out-Null

$exe = Join-Path $ExeDir "tank-battle.exe"
$p = Start-Process -FilePath $exe -WorkingDirectory $ExeDir -PassThru
Start-Sleep -Seconds 3

# move window to top-left so it is fully on screen
$p.Refresh()
$h = $p.MainWindowHandle
if ($h -ne 0) {
    [M]::MoveWindow($h, 0, 0, $WinW, $WinH, $true) | Out-Null
    [M]::SetForegroundWindow($h) | Out-Null
    Start-Sleep -Seconds 1
}

if ($Phase -eq "playing") {
    # click the window center (letterboxed view keeps stage center = window center
    # for any window size)
    $rc = New-Object M+RECT
    [void][M]::GetWindowRect($h, [ref]$rc)
    [M]::SetCursorPos([int](($rc.L + $rc.R) / 2), [int](($rc.T + $rc.B) / 2)) | Out-Null
    Start-Sleep -Milliseconds 300
    [M]::mouse_event(2, 0, 0, 0, [UIntPtr]::Zero)  # left down
    Start-Sleep -Milliseconds 120                  # hold so the 60fps poll sees it
    [M]::mouse_event(4, 0, 0, 0, [UIntPtr]::Zero)  # left up
    Start-Sleep -Seconds 7
}

# window rect for cropping
$p.Refresh()
$h = $p.MainWindowHandle
$r = New-Object M+RECT
$ok = $false
if ($h -ne 0) { $ok = [M]::GetWindowRect($h, [ref]$r) }
Write-Output "RECT=$($r.L),$($r.T),$($r.R),$($r.B) ok=$ok"

# full-screen capture then crop
$ws = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
$bmp = New-Object System.Drawing.Bitmap($ws.Width, $ws.Height)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($ws.Location, [System.Drawing.Point]::Empty, $ws.Size)
$g.Dispose()
$full = Join-Path $ExeDir ($OutPng + ".full.png")
$bmp.Save($full, [System.Drawing.Imaging.ImageFormat]::Png)
$cw = [Math]::Min($r.R, $ws.Width) - $r.L
$ch = [Math]::Min($r.B, $ws.Height) - $r.T
if ($cw -gt 0 -and $ch -gt 0) {
    $crop = $bmp.Clone([System.Drawing.Rectangle]::new($r.L, $r.T, $cw, $ch), $bmp.PixelFormat)
    $out = Join-Path $ExeDir $OutPng
    $crop.Save($out, [System.Drawing.Imaging.ImageFormat]::Png)
    $crop.Dispose()
}
$bmp.Dispose()

Write-Output "PHASE=$Phase ALIVE=$(-not $p.HasExited) OUT=$out"
Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
