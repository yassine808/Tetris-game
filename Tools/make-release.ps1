# Builds a GitHub-release-ready archive of the packaged game.
#   - drops debug symbols (the .pdb is 229 MB and nothing needs it to play)
#   - zips the remainder
#   - splits the zip into chunks under GitHub's 100 MB per-file limit
$ErrorActionPreference = "Stop"

$src   = "D:\Projects\UNREAL-game\Packaged\Windows"
$stage = "D:\Projects\UNREAL-game\Releases\stage"
$rel   = "D:\Projects\UNREAL-game\Releases"

Remove-Item $rel -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $stage | Out-Null

# --- copy, skipping debug symbols -------------------------------------------
robocopy $src $stage /E /XF *.pdb /NFL /NDL /NJH /NJS /NP | Out-Null
if ($LASTEXITCODE -ge 8) { throw "robocopy failed: $LASTEXITCODE" }

$staged = (Get-ChildItem $stage -Recurse -File | Measure-Object Length -Sum).Sum
"staged: {0} MB in {1} files (debug symbols excluded)" -f [math]::Round($staged/1MB,1), (Get-ChildItem $stage -Recurse -File).Count

# --- give the launcher the name the README/REASSEMBLE.bat tell people to run --
# The root exe is a thin bootstrap that spawns
#   MyUEProject\Binaries\Win64\MyUEProject-Win64-Shipping.exe
# by directory-relative path, so its own filename is free to change.
$oldExe = Join-Path $stage "MyUEProject.exe"
$newExe = Join-Path $stage "UNREAL-game.exe"
if (Test-Path $oldExe) {
    Rename-Item $oldExe "UNREAL-game.exe"
    "launcher renamed -> UNREAL-game.exe"
} else {
    throw "launcher MyUEProject.exe not found in staged build -- aborting"
}

# --- zip --------------------------------------------------------------------
$zip = Join-Path $rel "UNREAL-game.zip"
Compress-Archive -Path (Join-Path $stage "*") -DestinationPath $zip -CompressionLevel Optimal
$zipSize = (Get-Item $zip).Length
"zip: {0} MB" -f [math]::Round($zipSize/1MB,1)

# --- split under 100 MB (use 90 MB for margin) ------------------------------
$chunk = 90MB
$parts = [math]::Ceiling($zipSize / $chunk)
"splitting into $parts part(s) of ~90 MB"

$buffer = New-Object byte[] $chunk
$in  = [System.IO.File]::OpenRead($zip)
try {
  for ($i = 1; $i -le $parts; $i++) {
    $outPath = "$zip.part$i"
    $out = [System.IO.File]::Create($outPath)
    try {
      $total = 0
      while ($total -lt $chunk) {
        $toRead = [math]::Min($chunk - $total, $in.Length - $in.Position)
        if ($toRead -le 0) { break }
        $read = $in.Read($buffer, 0, $toRead)
        if ($read -le 0) { break }
        $out.Write($buffer, 0, $read)
        $total += $read
      }
    } finally { $out.Dispose() }
    "  {0,-28} {1,8:N2} MB" -f (Split-Path $outPath -Leaf), ((Get-Item $outPath).Length/1MB)
  }
} finally { $in.Dispose() }

# The parts fully replace the zip, so drop it -- it is also over the 100 MB limit.
Remove-Item $zip -Force

# --- safety check: no single uploaded file may exceed 100 MB -----------------
$tooBig = Get-ChildItem $rel -File | Where-Object { $_.Length -gt 95MB }
if ($tooBig) { throw "Files still over GitHub limit: $($tooBig.Name -join ', ')" }

Copy-Item "D:\Projects\UNREAL-game\REASSEMBLE.bat" $rel -Force
"`nAll release files ready in $rel"
Get-ChildItem $rel -File | Select-Object Name,@{n='MB';e={[math]::Round($_.Length/1MB,2)}} | Format-Table -AutoSize
