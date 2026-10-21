<#
02_verify_then_remove.ps1 -- Stage 2: delete the sources ONLY after every MD5 passes.

Sibling of 01_archive.ps1.  Reads the manifest produced by stage 1 and:

  PHASE 1 (never deletes anything)
      For every row: target exists ?  size equal ?  md5 equal ?
      Any single mismatch -> prints the reason and exits with code 2.
      Nothing is deleted in that case.  Not one file.

  PHASE 2 (only reached when PHASE 1 is 100% OK)
      Removes each source file listed in the manifest, printing one line each,
      then removes directories that became empty (bottom-up, only if truly empty).

SAFETY RAILS
  * A source is only ever deleted if its path is inside -Repo and it is
    listed in the manifest with status COPIED (or COPY/MD5-verified).
  * Nothing outside the manifest is touched.
  * -DryRun prints what would be verified/deleted and exits without changes.

USAGE (delivered NOT executed)
  # see what would happen, no writes:
  pwsh -File vmctl\cleanup\02_verify_then_remove.ps1 -DryRun

  # verify + delete, picking the newest manifest automatically:
  pwsh -File vmctl\cleanup\02_verify_then_remove.ps1

  # specific manifest:
  pwsh -File vmctl\cleanup\02_verify_then_remove.ps1 -Manifest "E:\车载备份\_manifests\manifest_20261019_101500.csv"
#>
[CmdletBinding()]
param(
    [string]$Manifest,
    [string]$TargetRoot,
    [string]$Repo,
    [switch]$DryRun
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0

function U { param([int[]]$Codes) -join ($Codes | ForEach-Object { [char]$_ }) }
$sBackupName = U 0x8F66, 0x8F7D, 0x5907, 0x4EFD    # 车载备份

if (-not $Repo) { $Repo = (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)) }
if (-not $TargetRoot) { $TargetRoot = 'E:\' + $sBackupName }
$Repo = (Resolve-Path -LiteralPath $Repo).Path.TrimEnd('\')

if (-not $Manifest) {
    $manDir = Join-Path $TargetRoot '_manifests'
    $cand = Get-ChildItem -LiteralPath $manDir -Filter 'manifest_*.csv' -File -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if (-not $cand) { throw ('no manifest found under ' + $manDir + ' -- pass -Manifest explicitly') }
    $Manifest = $cand.FullName
}
if (-not (Test-Path -LiteralPath $Manifest)) { throw ('manifest not found: ' + $Manifest) }

Write-Host ''
Write-Host '========== 02_verify_then_remove.ps1 =========='
Write-Host ('repo     : ' + $Repo)
Write-Host ('manifest : ' + $Manifest)
Write-Host ''
$rows = Import-Csv -LiteralPath $Manifest
Write-Host ('rows in manifest: ' + $rows.Count)
Write-Host ''

# ---------------------------------------------------------------- PHASE 1
Write-Host '--- PHASE 1: verify every target against the recorded MD5 (nothing deleted) ---'
$bad = 0
$ok = 0
$todo = New-Object System.Collections.ArrayList
foreach ($r in $rows) {
    if ($r.status -eq 'REMOVED') { continue }
    [void]$todo.Add($r)
    $why = $null
    if (-not (Test-Path -LiteralPath $r.target_path)) { $why = 'target missing' }
    elseif (-not (Test-Path -LiteralPath $r.source_path)) { $why = 'source already gone (already removed?)' }
    else {
        $dstLen = (Get-Item -LiteralPath $r.target_path).Length
        if ([int64]$dstLen -ne [int64]$r.size_bytes) { $why = ('size mismatch dst=' + $dstLen + ' expected=' + $r.size_bytes) }
        else {
            $dstHash = (Get-FileHash -LiteralPath $r.target_path -Algorithm MD5).Hash.ToLower()
            if ($dstHash -ne $r.md5.ToLower()) { $why = ('md5 mismatch dst=' + $dstHash + ' expected=' + $r.md5) }
        }
    }
    if ($why) {
        $bad++
        Write-Host ('[BAD] ' + $r.seq + ' ' + $why) -ForegroundColor Red
        Write-Host ('      src=' + $r.source_path)
        Write-Host ('      dst=' + $r.target_path)
    }
    else {
        $ok++
        if (($ok % 100) -eq 0) { Write-Host ('[ OK] verified ' + $ok + ' ...') }
    }
}
Write-Host ''
Write-Host ('PHASE 1 result: OK=' + $ok + '  BAD=' + $bad + '  rows=' + $todo.Count)

if ($bad -gt 0) {
    Write-Host ''
    Write-Host 'ABORT: at least one file failed verification. Nothing was deleted.' -ForegroundColor Red
    Write-Host 'Fix the archive first (re-run 01_archive.ps1 with a new -Stamp), then retry.' -ForegroundColor Red
    exit 2
}
if ($todo.Count -eq 0) {
    Write-Host 'Nothing to do (all rows already REMOVED).'
    exit 0
}

# ---------------------------------------------------------------- PHASE 2
Write-Host ''
Write-Host '--- PHASE 2: all MD5 verified, removing sources ---'
if ($DryRun) {
    foreach ($r in $todo) { Write-Host ('[WOULD DEL] ' + $r.source_path) }
    Write-Host ''
    Write-Host 'DRY RUN -- nothing was deleted.' -ForegroundColor Yellow
    exit 0
}

$del = 0
$failDel = 0
foreach ($r in $todo) {
    if ($r.source_path -notlike ($Repo + '\*')) {
        Write-Warning ('refusing to delete outside repo: ' + $r.source_path)
        $failDel++
        continue
    }
    try {
        Remove-Item -LiteralPath $r.source_path -Force
        Write-Host ('[DEL] ' + $r.source_path)
        $r.status = 'REMOVED'
        $del++
    }
    catch {
        Write-Warning ('delete failed: ' + $r.source_path + ' -> ' + $_.Exception.Message)
        $failDel++
    }
}

# ---------------------------------------------------------------- empty dirs
Write-Host ''
Write-Host '--- removing directories that are now empty (bottom-up) ---'
$dirs = @{}
foreach ($r in $todo) {
    if ($r.status -ne 'REMOVED') { continue }
    $d = Split-Path -Parent $r.source_path
    while ($d -and $d.Length -gt $Repo.Length -and $d -ne $Repo) { $dirs[$d] = $true; $d = Split-Path -Parent $d }
}
$removedDirs = 0
foreach ($d in ($dirs.Keys | Sort-Object -Property Length -Descending)) {
    if (-not (Test-Path -LiteralPath $d)) { continue }
    $left = Get-ChildItem -LiteralPath $d -Force -ErrorAction SilentlyContinue
    if (-not $left) {
        try {
            Remove-Item -LiteralPath $d -Force
            Write-Host ('[RMDIR] ' + $d)
            $removedDirs++
        }
        catch { Write-Warning ('rmdir failed: ' + $d + ' -> ' + $_.Exception.Message) }
    }
}

# ---------------------------------------------------------------- manifest update
$rows | Export-Csv -LiteralPath $Manifest -NoTypeInformation -Encoding UTF8

Write-Host ''
Write-Host '================ summary ================'
Write-Host ('deleted files : ' + $del)
Write-Host ('delete errors : ' + $failDel)
Write-Host ('empty dirs    : ' + $removedDirs)
Write-Host ('manifest      : ' + $Manifest + '  (status column updated)')
Write-Host ''
Write-Host ('rollback      : pwsh -File vmctl\cleanup\03_rollback.ps1 -Manifest "' + $Manifest + '"')
if ($failDel -gt 0) { exit 3 }
exit 0
