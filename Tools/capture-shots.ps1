# Captures Docs/menu.png, Docs/gameplay.png, Docs/gameover.png from the
# PACKAGED Shipping build -- so the README shows the artifact people download.
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File capture-shots.ps1
param(
  [string]$GameDir = "D:\Projects\UNREAL-game\Releases\verify",
  [string]$OutDir  = "D:\Projects\UNREAL-game\Docs"
)
$ErrorActionPreference = "Stop"

Add-Type @"
using System;
using System.Runtime.InteropServices;
public class FW {
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool BringWindowToTop(IntPtr h);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int c);
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern void mouse_event(uint f, uint dx, uint dy, uint d, IntPtr e);
}
"@
Add-Type -AssemblyName System.Windows.Forms,System.Drawing

function Focus-Game([IntPtr]$h) {
  # A real click is the only thing Windows reliably grants foreground to.
  [void][FW]::SetCursorPos(200, 900)
  Start-Sleep -Milliseconds 250
  [FW]::mouse_event(0x0002,0,0,0,[IntPtr]::Zero)
  [FW]::mouse_event(0x0004,0,0,0,[IntPtr]::Zero)
  Start-Sleep -Milliseconds 400
  if ([FW]::GetForegroundWindow() -ne $h) {
    [void][FW]::ShowWindow($h,3)
    [void][FW]::BringWindowToTop($h)
    [void][FW]::SetForegroundWindow($h)
    Start-Sleep -Milliseconds 500
  }
  return ([FW]::GetForegroundWindow() -eq $h)
}

function Snap([string]$path) {
  Start-Sleep -Milliseconds 800
  $b = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
  $bmp = New-Object System.Drawing.Bitmap $b.Width, $b.Height
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $g.CopyFromScreen($b.Location, [System.Drawing.Point]::Empty, $b.Size)
  $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
  $g.Dispose(); $bmp.Dispose()
  "  -> $path"
}

# Red "GAME OVER" detector.
#
# The overlay draws in this order: full-screen dim (alpha .90), then the menu
# items, then the banner band (alpha .45) and the "GAME OVER" text. After the
# dim, red board pieces drop to ~10% brightness so they can't trigger this --
# only the opaque (255,77,77) banner text still reads as saturated red.
# Text sits at y = ScreenH*0.08 (~86) at scale 2.6, so it spans ~y 86..154.
function Test-GameOver {
  $b = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
  $bmp = New-Object System.Drawing.Bitmap $b.Width, $b.Height
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $g.CopyFromScreen($b.Location, [System.Drawing.Point]::Empty, $b.Size)
  $g.Dispose()
  $red = 0
  for ($y=60; $y -lt 200; $y += 3) {
    for ($x=650; $x -lt 1500; $x += 3) {
      $p = $bmp.GetPixel($x,$y)
      if ($p.R -gt 140 -and $p.G -lt 90 -and $p.B -lt 90) { $red++ }
    }
  }
  $bmp.Dispose()
  return $red -gt 40
}

# --- launch ------------------------------------------------------------------
$exe = Join-Path $GameDir "UNREAL-game.exe"
if (-not (Test-Path $exe)) { $exe = Join-Path $GameDir "MyUEProject.exe" }
if (-not (Test-Path $exe)) { throw "no launcher found in $GameDir" }

Get-Process -Name "MyUEProject*" -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Seconds 2
Start-Process -FilePath $exe
"waiting 60s for boot..."
Start-Sleep -Seconds 60

$game = Get-Process -Name "MyUEProject-Win64-Shipping" -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $game) { throw "packaged game did not start" }
"game pid=$($game.Id)"
$h = $game.MainWindowHandle

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$wsh = New-Object -ComObject WScript.Shell

if (-not (Focus-Game $h)) { throw "could not focus the game window" }
Start-Sleep -Seconds 3

# 1 --- main menu -------------------------------------------------------------
Snap (Join-Path $OutDir "menu.png")

# 2 --- gameplay: enter and build a stack ------------------------------------
[void]$wsh.AppActivate($game.Id)
$wsh.SendKeys("{ENTER}")
Start-Sleep -Seconds 2

for ($i=1; $i -le 15; $i++) {
  if (-not (Focus-Game $h)) { throw "lost focus mid-game" }
  $moves = Get-Random -Minimum 1 -Maximum 4
  foreach ($m in 1..$moves) { $wsh.SendKeys("{LEFT}"); Start-Sleep -Milliseconds 70 }
  $wsh.SendKeys(" ")
  Start-Sleep -Milliseconds 300
}
Snap (Join-Path $OutDir "gameplay.png")

# 3 --- game over: keep hard-dropping until the board tops out ----------------
$over = $false
for ($i=1; $i -le 90 -and -not $over; $i++) {
  if ($i % 10 -eq 0 -and -not (Focus-Game $h)) { throw "lost focus" }
  $wsh.SendKeys(" ")
  Start-Sleep -Milliseconds 220
  if ($i -ge 30 -and ($i % 6 -eq 0)) { $over = Test-GameOver }
}
if (-not $over) { "WARNING: game-over banner not detected" } else { "game over reached" }
Snap (Join-Path $OutDir "gameover.png")

Get-Process -Name "MyUEProject*" -ErrorAction SilentlyContinue | Stop-Process -Force
"`ncaptured 3 screenshots -> $OutDir"
