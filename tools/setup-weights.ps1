# One command: download the model weights (pinned, checked) and copy them to the console over FTP.
#   powershell -ExecutionPolicy Bypass -File .\tools\setup-weights.ps1 <console-ip> [-Set q4|q8] [-Pixal3d] [-NoBirefnet] [-DownloadOnly] [-UploadOnly] [-Only <file.gguf>] [-AcceptLicenses]
# Safe to re-run: finished downloads and files already on the console (same size) are skipped.
# Writes only into models\ on this PC and /data/PS5MG/models/<set>/ on the console; never deletes anything on the console.
param(
  [Parameter(Mandatory, Position = 0)][string]$Ip,
  [string]$Set = 'q4', [switch]$Pixal3d, [switch]$NoBirefnet,
  [switch]$DownloadOnly, [switch]$UploadOnly, [string]$Only = '', [switch]$AcceptLicenses
)
$ErrorActionPreference = 'Stop'
function Die($m) { Write-Host "ERROR: $m" -ForegroundColor Red; exit 1 }
$root = Split-Path $PSScriptRoot -Parent
if ($Set -notin 'q4', 'q8') { Die "-Set must be q4 or q8 (got '$Set')" }
if ($Ip -notmatch '^[A-Za-z0-9.:-]+$') { Die "that does not look like a console address: $Ip" }
if ($DownloadOnly -and $UploadOnly) { Die '-DownloadOnly and -UploadOnly together do nothing' }
if (-not (Get-Command curl.exe -ErrorAction SilentlyContinue)) { Die 'curl.exe not found (it ships with Windows 10 and later)' }

# The file list comes only from the manifest.
$files = foreach ($l in Get-Content "$PSScriptRoot\weights.sha256") {
  $p = $l.Trim() -split '\s+'
  if ($p.Count -lt 5 -or $p[0].StartsWith('#')) { continue }
  $o = [pscustomobject]@{ Sha = $p[0]; Size = [int64]$p[1]; Repo = $p[2]; Rev = $p[3]; Path = $p[4]; Name = '' }
  if ($o.Repo -eq 'ilintar/trellis2-gguf' -and $o.Path.StartsWith("$Set/")) { $o.Name = $o.Path.Substring($Set.Length + 1) }
  elseif ($o.Repo -eq 'vegax87/Pixal3D' -and $Pixal3d) { $o.Name = $o.Path }
  if ($o.Name -and -not ($NoBirefnet -and $o.Name -eq 'birefnet.gguf')) { $o }
}
if ($Only) {
  $files = @($files | Where-Object Name -eq $Only)
  if (-not $files) { Die "'$Only' is not a file of this set (add -Pixal3d for Pixal3D files, and check the spelling)" }
}
$files = @($files); $total = $files.Count
$dir = Join-Path $root "models\$Set"
function MB($b) { [int][math]::Round($b / 1000000) }   # decimal MB, like Hugging Face and the README
function Fsize($f) { if (Test-Path $f) { (Get-Item $f).Length } else { -1 } }
function RemoteSize($url) {
  $h = & curl.exe -sI --connect-timeout 5 $url
  foreach ($x in $h) { if ($x -match '^content-length:\s*(\d+)') { return [int64]$Matches[1] } }
  -1
}

if (-not $UploadOnly) {
  if (-not $AcceptLicenses) {
    @'
These are somebody else's model weights. They are not part of PS5MG and are downloaded from their authors' pages:
  TRELLIS.2 (Microsoft, MIT) as GGUF by ilintar:  https://huggingface.co/ilintar/trellis2-gguf
  Pixal3D (MIT) by vegax87:                       https://huggingface.co/vegax87/Pixal3D
  DINOv3, inside every TRELLIS.2 set, is under Meta's own licence (not MIT):
                                                  https://ai.meta.com/resources/models-and-libraries/dinov3-license
Using them means you accept their licences. Read them first, especially before you sell anything you make with them.
'@ | Write-Host
    $a = if ([Environment]::UserInteractive -and -not [Console]::IsInputRedirected) { Read-Host 'Type "yes" to accept and download' } else { Write-Host 'Not a terminal: pass -AcceptLicenses to confirm that you have read them.'; '' }
    if ($a -ne 'yes') { Write-Host 'Nothing was downloaded.'; exit 1 }
  }
  New-Item -ItemType Directory -Force $dir | Out-Null
  $need = 0
  foreach ($f in $files) {
    $have = Fsize "$dir\$($f.Name)"
    if ($have -eq $f.Size) { continue }
    $need += if ($have -gt 0 -and $have -lt $f.Size) { $f.Size - $have } else { $f.Size }
  }
  $free = (Get-Item $dir).PSDrive.Free
  if ($free -lt $need + 100MB) { Die "not enough disk space on this PC: need about $(MB $need) MB more, only $(MB $free) MB free" }
  $n = 0
  foreach ($f in $files) {
    $n++; $dest = "$dir\$($f.Name)"; $tag = "[$n/$total] $($f.Name) ($(MB $f.Size) MB)"
    $ok = $false
    foreach ($try in 1, 2) {
      if ((Fsize $dest) -eq $f.Size) {
        Write-Host "${tag}: checking ..."
        if ((Get-FileHash $dest -Algorithm SHA256).Hash -eq $f.Sha) { Write-Host "${tag}: already there and correct"; $ok = $true; break }
        Write-Host "${tag}: damaged, downloading it again"; Remove-Item $dest
      }
      if ((Fsize $dest) -gt $f.Size) { Remove-Item $dest }
      Write-Host "${tag}: downloading (you can stop with Ctrl+C and run the same command again to continue)"
      $url = "https://huggingface.co/$($f.Repo)/resolve/$($f.Rev)/$($f.Path)"
      foreach ($k in 1..3) {
        & curl.exe -L --fail -C - -# --connect-timeout 15 -o $dest $url
        if ($LASTEXITCODE -eq 0) { break }
        if ($k -eq 3) { Die 'cannot download from Hugging Face. Is the internet connection working? Run the command again to continue.' }
        Start-Sleep 3
      }
    }
    if (-not $ok) { Die "$($f.Name) still does not match its SHA-256 after a second download - not using it" }
  }
}

if (-not $DownloadOnly) {
  $base = "ftp://${Ip}:2121/data/PS5MG/models/$Set"
  & curl.exe -s --connect-timeout 5 -o NUL "ftp://${Ip}:2121/"
  if ($LASTEXITCODE -ne 0) { Die "cannot reach the console at $Ip port 2121. Is the FTP server running on the console? Is the address right?" }
  Write-Host 'Copying to the console. A file that was only half copied is replaced when you run this again.'
  $n = 0
  foreach ($f in $files) {
    $n++; $src = "$dir\$($f.Name)"; $tag = "[$n/$total] $($f.Name) ($(MB $f.Size) MB)"
    if ((Fsize $src) -ne $f.Size) { Die "$src is missing or incomplete on this PC - run without -UploadOnly first" }
    if ((RemoteSize "$base/$($f.Name)") -eq $f.Size) { Write-Host "${tag}: already on the console with the same size, skipped"; continue }
    Write-Host "${tag}: copying to the console ..."
    & curl.exe -# --connect-timeout 5 --ftp-create-dirs -T $src "$base/$($f.Name)" -o NUL
    if ($LASTEXITCODE -ne 0) { Die "copy of $($f.Name) failed. Is the FTP server running on the console? Run the command again to continue." }
    $r = RemoteSize "$base/$($f.Name)"
    if ($r -ne $f.Size) { Die "$($f.Name) on the console has size $r, expected $($f.Size) - run the command again" }
    Write-Host "${tag}: copied"
  }
}
if ($DownloadOnly) { Write-Host "Done. The files are in $dir on this PC. Run the command again without -DownloadOnly to copy them to the console." }
else { Write-Host "Done. Open http://${Ip}:7870 in a browser and drop a picture." }
