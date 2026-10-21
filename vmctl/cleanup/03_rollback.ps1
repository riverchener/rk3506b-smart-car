<#
03_rollback.ps1 -- restore files from the archive back into the repo, per manifest.

This is the reverse of 01_archive.ps1 and is what you run if 02 removed something
you want back.  It copies target_path -> source_path again and verifies the MD5.

  * never deletes anything
  * refuses to overwrite an existing source file (reports the conflict instead)
  * -DryRun lists the planned copies and exits

USAGE (delivered NOT executed)
  pwsh -File vmctl\cleanup\03_rollback.ps1 -DryRun
  pwsh -File vmctl\cleanup\03_rollback.ps1 -Manifest "E:\车载备份\_manifests\manifest_20261019_101500.csv"
  # subset: only one category (B1 / B2 / C):
  pwsh -File vmctl\cleanup\03_rollback.ps1 -Category B1
#>
[CmdletBinding()]
param(
    [string]$Manifest,
    [string]$TargetRoot,
    [string]$Repo,
    [string]$Category,
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

$rows = Import-Csv -LiteralPath $Manifest
if ($Category) { $rows = $rows | Where-Object { $_.category -eq $Category } }

Write-Host ''
Write-Host '========== 03_rollback.ps1 =========='
Write-Host ('manifest : ' + $Manifest)
Write-Host ('rows     : ' + $rows.Count + '   category filter: ' + ($(if ($Category) { $Category } else { '(none)' })))
Write-Host ''

$done = 0; $skip = 0; $fail = 0
foreach ($r in $rows) {
    if (-not (Test-Path -LiteralPath $r.target_path)) {
        Write-Warning ('archived copy missing, cannot restore: ' + $r.target_path)
        $fail++
        continue
    }
    if (Test-Path -LiteralPath $r.source_path) {
        $cur = (Get-FileHash -LiteralPath $r.source_path -Algorithm MD5).Hash.ToLower()
        if ($cur -eq $r.md5.ToLower()) { Write-Host ('[SKIP same] ' + $r.source_path); $skip++ }
        else { Write-Warning ('source exists with different content, not overwriting: ' + $r.source_path); $fail++ }
        continue
    }
    if ($DryRun) { Write-Host ('[WOULD COPY] ' + $r.target_path + ' -> ' + $r.source_path); continue }

    $parent = Split-Path -Parent $r.source_path
    if (-not (Test-Path -LiteralPath $parent)) { New-Item -ItemType Directory -Force -Path $parent | Out-Null }
    try {
        Copy-Item -LiteralPath $r.target_path -Destination $r.source_path
        $h = (Get-FileHash -LiteralPath $r.source_path -Algorithm MD5).Hash.ToLower()
        if ($h -ne $r.md5.ToLower()) { throw ('md5 mismatch after restore: ' + $h) }
        Write-Host ('[RESTORED] ' + $r.source_path)
        $done++
    }
    catch {
        Write-Warning ('restore failed: ' + $r.source_path + ' -> ' + $_.Exception.Message)
        $fail++
    }
}

Write-Host ''
Write-Host ('restored=' + $done + '  skipped_same=' + $skip + '  failed=' + $fail)
if ($DryRun) { Write-Host 'DRY RUN -- nothing was copied.' -ForegroundColor Yellow }
if ($fail -gt 0) { exit 1 }
exit 0
