<#
01_archive.ps1 -- Stage 1: COPY-ONLY archive of the repo cleanup plan (2026-10-19).

Source plan  : build_out\docs\目录清理方案_20261019.md  (section 3 = the B/C lists)
Location     : <repo>\vmctl\cleanup\01_archive.ps1

WHAT IT DOES
  * Copies (never moves, never deletes) the B1/B2/C items to E:\<backup>\...
  * Computes an MD5 for every single copied file (source hash AND target hash)
    and writes one CSV row per file into  <backup>\_manifests\manifest_<stamp>.csv
  * Aborts before touching anything if a destination already exists.
  * Exits non-zero on any mismatch; the source file is always kept.

WHAT IT DOES NOT DO
  * It does NOT delete a single source file.  Deletion is exclusively done by
    02_verify_then_remove.ps1, and only after every MD5 in the manifest passes.

ENC O D I N G NOTE
  This script source is deliberately ASCII-only.  PowerShell 5.1 decodes a
  BOM-less script as ANSI/GBK, which corrupts Chinese literals and then eats
  quotes/braces (that exact bug is documented in
  Linux车载系统\tools\ota_server.ps1 lines 28-38).  All CJK path components
  are therefore built from code points by the U() helper below, so the script
  behaves identically under powershell.exe 5.1 and pwsh 7.

USAGE (read first, then run yourself -- this file was delivered NOT executed)
  # 0) dry run, prints the full task list and totals, writes nothing:
  pwsh -File vmctl\cleanup\01_archive.ps1 -DryRun

  # 1) batch 1 only (smallest, safest: 4 ui_backup dirs, 2 book backups,
  #    _wallpaper_backup, all __pycache__)
  pwsh -File vmctl\cleanup\01_archive.ps1 -Batch 1

  # 2) batch 2 (the five tmp_* working dirs)
  pwsh -File vmctl\cleanup\01_archive.ps1 -Batch 2

  # 3) batch 3 (vmctl archive set 1,164 files + old-feature items)
  pwsh -File vmctl\cleanup\01_archive.ps1 -Batch 3

  # everything in one go (B1+B2+C):
  pwsh -File vmctl\cleanup\01_archive.ps1

  # opt-in extras (off by default -- see decision items D1/D2/D5/D8/D9):
  pwsh -File vmctl\cleanup\01_archive.ps1 -IncludeChromeProfile
#>
[CmdletBinding()]
param(
    [string]$Repo,
    [string]$TargetRoot,
    [int]$Batch = 0,
    [switch]$DryRun,
    [switch]$IncludeChromeProfile,
    [switch]$IncludeEvidence,
    [switch]$IncludeBookSnapshots,
    [switch]$IncludeArchive,
    [string]$Stamp = (Get-Date -Format 'yyyyMMdd_HHmmss')
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0

# ---------------------------------------------------------------- CJK strings
function U { param([int[]]$Codes) -join ($Codes | ForEach-Object { [char]$_ }) }
$sBackupName = U 0x8F66, 0x8F7D, 0x5907, 0x4EFD            # 车载备份
$sOldFeature = U 0x65E7, 0x529F, 0x80FD                    # 旧功能
$sOneShot    = U 0x4E00, 0x6B21, 0x6027, 0x811A, 0x672C    # 一次性脚本
$sNonProdB   = U 0x975E, 0x4EA7, 0x54C1                    # 非产品
$sInnerRepo  = 'Linux' + (U 0x8F66, 0x8F7D, 0x7CFB, 0x7EDF) # Linux车载系统
$sGongFen    = U 0x5206, 0x5DE5, 0x7248                    # 分工版
$sOldScripts = U 0x65E7, 0x9879, 0x76EE, 0x811A, 0x672C    # 旧项目脚本

if (-not $Repo) { $Repo = (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)) }
if (-not $TargetRoot) { $TargetRoot = 'E:\' + $sBackupName }
$Repo = (Resolve-Path -LiteralPath $Repo).Path.TrimEnd('\')

$tRoot = Join-Path $TargetRoot 'vm_tools'
$vmArc = Join-Path $tRoot ('vmctl_' + (Get-Date -Format 'yyyyMMdd'))
$tBkp  = Join-Path $TargetRoot ('backups_' + (Get-Date -Format 'yyyyMMdd'))
$tOld  = Join-Path $TargetRoot ($sOldFeature + '_' + (Get-Date -Format 'yyyyMMdd'))
$tOne  = Join-Path $TargetRoot ($sOneShot + '_' + (Get-Date -Format 'yyyyMMdd'))
$tNon  = Join-Path $TargetRoot ($sNonProdB + '_' + (Get-Date -Format 'yyyyMMdd'))
$tMan  = Join-Path $TargetRoot '_manifests'

# The 24 vmctl scripts that STAY in the repo (plan section 2.3).
$Keep = @(
    'board_common.py', 'board_paths.py', 'board_health.py', 'board_inventory.py',
    'board_restore.py', 'board_restore_selftest.py',
    'vm.py', 'vm_config.json', 'vm_compile.py', 'upload_tree.py', 'build_app.sh',
    'pf_shot.py', 'kmscap.c', 'grab_real_screen.py',
    'ota_deploy_tool.py', 'ota_syntax_check.py', 'flash_mtd6_rtthread.py',
    'check_board_artifact_paths.py', 'install_smartcar_fresh.py', 'deploy_app.py',
    'm0_quick_health.py', 'final_check.py', 'board_ssh.py', 'ota_server.py'
)

# The 9 old-project one-off scripts -> category C (plan section 3.3).
$OldScripts = @(
    'disable_old_project.py', 'really_disable_old_project.py',
    'find_old_project_launcher.py', 'kill_leftover_old_ui.py',
    'disable_other_uis.py', 'inspect_boot_competition.py',
    'inspect_other_init_scripts.py', 'ipvid_04_community.py',
    'bt_systemui_check.py'
)

# ---------------------------------------------------------------- task builder
$Tasks = New-Object System.Collections.ArrayList

function Add-One {
    param([string]$Category, [int]$BatchNo, [string]$Src, [string]$Dst, [string]$Note)
    if (-not (Test-Path -LiteralPath $Src)) { Write-Warning ("missing, skipped: " + $Src); return }
    [void]$Tasks.Add([pscustomobject]@{
            Category = $Category; BatchNo = $BatchNo
            Source   = $Src; Target = $Dst; Note = $Note
        })
}
function Add-Tree {
    param([string]$Category, [int]$BatchNo, [string]$SrcDir, [string]$DstDir, [string]$Note)
    if (-not (Test-Path -LiteralPath $SrcDir)) { Write-Warning ("missing, skipped: " + $SrcDir); return }
    $parent = $SrcDir.TrimEnd('\')
    Get-ChildItem -LiteralPath $parent -Recurse -File -Force -ErrorAction SilentlyContinue | ForEach-Object {
        $rel = $_.FullName.Substring($parent.Length).TrimStart('\')
        Add-One $Category $BatchNo $_.FullName (Join-Path $DstDir $rel) $Note
    }
}
function Want([int]$b) { return ($Batch -eq 0) -or ($Batch -eq $b) }

# --- batch 1: tiny, zero-reference backups + every __pycache__ outside vmctl
if (Want 1) {
    foreach ($n in @('ui_backup_incar_ok', 'ui_backup_step1_ok', 'ui_backup_tesla_partial', 'ui_backup_v2_ok')) {
        Add-Tree 'B2' 1 (Join-Path $Repo ("build_out\" + $n)) (Join-Path $tBkp ("build_out__" + $n)) 'UI stage source backup'
    }
    Add-Tree 'B2' 1 (Join-Path $Repo 'build_out\docs\_wallpaper_backup') (Join-Path $tBkp 'docs__wallpaper_backup') 'old wallpaper + old app bin'
    Add-Tree 'B2' 1 (Join-Path $Repo 'build_out\docs\_book_backup_20261017') (Join-Path $tBkp 'docs__book_backup_20261017') 'book PDF old copy'
    foreach ($n in @(('build_out\docs\_book_backup_20261018_' + $sGongFen + '.docx'), ('build_out\docs\_book_backup_20261018_' + $sGongFen + '.pdf'))) {
        $src = Join-Path $Repo $n
        Add-One 'B2' 1 $src (Join-Path $tBkp (Split-Path $src -Leaf)) 'book draft old copy'
    }
    $pc = Get-ChildItem -LiteralPath $Repo -Recurse -Directory -Force -Filter '__pycache__' -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -notlike (Join-Path $Repo 'vmctl\*') }
    foreach ($d in $pc) {
        $rel = $d.FullName.Substring($Repo.Length).TrimStart('\')
        $flat = $rel.Replace('\', '__')
        Add-Tree 'B2' 1 $d.FullName (Join-Path (Join-Path $tBkp 'pycache') $flat) 'regenerable bytecode'
    }
}

# --- batch 2: the five tmp_* one-off working dirs
if (Want 2) {
    foreach ($n in @('tmp_adc_scan', 'tmp_bt_off', 'tmp_ota_150', 'tmp_ota_160', 'tmp_switch')) {
        Add-Tree 'B2' 2 (Join-Path $Repo $n) (Join-Path $tBkp $n) 'one-off evidence dir'
    }
}

# --- batch 3: vmctl archive set (B1) + old-feature items (C)
if (Want 3) {
    $vmDir = Join-Path $Repo 'vmctl'
    $selfDir = Join-Path $vmDir 'cleanup'
    Get-ChildItem -LiteralPath $vmDir -Recurse -File -Force -ErrorAction SilentlyContinue | ForEach-Object {
        $name = $_.Name
        $isRoot = ($_.DirectoryName -eq $vmDir)
        # never archive the cleanup kit itself (this script lives in vmctl\cleanup)
        if ($_.FullName -like ($selfDir + '\*')) { return }
        if ($isRoot -and ($Keep -contains $name)) { return }
        if ($isRoot -and ($OldScripts -contains $name)) { return }
        $rel = $_.FullName.Substring($vmDir.Length).TrimStart('\')
        Add-One 'B1' 3 $_.FullName (Join-Path $vmArc $rel) 'one-off campaign script'
    }
    foreach ($n in $OldScripts) {
        $src = Join-Path $vmDir $n
        Add-One 'C' 3 $src (Join-Path (Join-Path $tOld $sOldScripts) $n) 'old-project one-off'
    }
    Add-Tree 'C' 3 (Join-Path $Repo 'build_out\m0diag') (Join-Path $tOld 'build_out__m0diag') 'M0 diag output, zero refs'
}

# --- opt-in extras (all OFF by default)
if ($IncludeChromeProfile) {
    foreach ($n in @('_chrome_profile', '_chrome_profile_filetest')) {
        Add-Tree 'B2' 4 (Join-Path $Repo ($sInnerRepo + '\tools\can_web\' + $n)) (Join-Path (Join-Path $tBkp 'can_web_chrome_profile') $n) 'rebuildable chrome profile'
    }
}
if ($IncludeArchive -and (Want 4)) {
    Add-Tree 'B2' 4 (Join-Path $Repo 'archive') (Join-Path $tBkp 'archive') 'previous cleanup archive'
}
if ($IncludeEvidence -and (Want 4)) {
    $ev = @('_audit_raw', '_coverfix', '_incprobe', '_interview_book', '_ipvideo', '_ipvideofix',
        '_mp4tx', '_mq135breath', '_mq135conf', '_mq135fix', '_music', '_outline',
        '_rpmsgdup', '_switch', '_vinyl', '_wallpaper', '_wallpaper_160')
    foreach ($n in $ev) { Add-Tree 'B3' 4 (Join-Path $Repo ('build_out\docs\' + $n)) (Join-Path (Join-Path $tBkp 'docs_evidence') $n) 'delivery-report evidence (BREAKS doc links!)' }
}
if ($IncludeBookSnapshots -and (Want 4)) {
    $bs = @('pdf_v12', 'pdf_v13', 'pdf_v14', 'qa_v11', 'qa_v11b', 'qa_v11c', 'qa_v12', 'qa_v13',
        'qa_v14', 'qa_v15', 'qa_v16', 'qa_v17', 'r1', 'r2', 'r3', 'r4', 'r5', 'r6', 'r7', 'r8',
        'render', 'render2', 'render3', 'render4', 'render5', '_stats')
    foreach ($n in $bs) { Add-Tree 'B4' 4 (Join-Path $Repo ('build_out\docs\book\' + $n)) (Join-Path (Join-Path $tBkp 'book_snapshots') $n) 'book version snapshot' }
    foreach ($n in @('book_v11.pdf', 'book_v11b.pdf')) {
        $src = Join-Path $Repo ('build_out\docs\book\' + $n)
        Add-One 'B4' 4 $src (Join-Path (Join-Path $tBkp 'book_snapshots') $n) 'book version snapshot'
    }
}

# ---------------------------------------------------------------- report
$totalFiles = $Tasks.Count
$totalBytes = 0
$catN = @{}
$catB = @{}
foreach ($t in $Tasks) {
    $len = (Get-Item -LiteralPath $t.Source).Length
    $totalBytes += $len
    if (-not $catN.ContainsKey($t.Category)) { $catN[$t.Category] = 0; $catB[$t.Category] = 0 }
    $catN[$t.Category] = $catN[$t.Category] + 1
    $catB[$t.Category] = $catB[$t.Category] + $len
}
Write-Host ''
Write-Host '================ 01_archive.ps1 : task list ================'
Write-Host ("repo        : " + $Repo)
Write-Host ("target root : " + $TargetRoot)
Write-Host ("manifest    : " + (Join-Path $tMan ('manifest_' + $Stamp + '.csv')))
Write-Host ''
Write-Host 'per category:'
foreach ($k in ($catN.Keys | Sort-Object)) {
    Write-Host ("  {0,-3} {1,6} files  {2,12:N0} bytes" -f $k, $catN[$k], $catB[$k])
}
Write-Host ''
Write-Host 'per batch:'
$Tasks | Group-Object BatchNo | Sort-Object Name | ForEach-Object {
    Write-Host ("  batch {0} : {1} files" -f $_.Name, $_.Count)
}
Write-Host ''
Write-Host ("TOTAL       : {0} files / {1:N0} bytes / {2:N2} MiB" -f $totalFiles, $totalBytes, ($totalBytes / 1MB))
Write-Host ''

if ($DryRun) {
    Write-Host 'DRY RUN -- nothing was written, nothing was deleted.' -ForegroundColor Yellow
    exit 0
}

# ---------------------------------------------------------------- preflight
$driveLetter = ([System.IO.Path]::GetPathRoot([System.IO.Path]::GetFullPath($TargetRoot))).Substring(0, 1)
$drv = Get-PSDrive -Name $driveLetter -ErrorAction SilentlyContinue
if (-not $drv) { throw ("target drive not found: " + $driveLetter) }
$free = $drv.Free
Write-Host ("free space on {0}: {1:N0} bytes ({2:N2} GiB)" -f $driveLetter, $free, ($free / 1GB))
if ($free -lt ($totalBytes * 1.2)) { throw 'not enough free space on target drive (need 1.2x payload)' }

if (-not (Test-Path -LiteralPath $TargetRoot)) {
    New-Item -ItemType Directory -Force -Path $TargetRoot | Out-Null
    Write-Host ("created: " + $TargetRoot)
}
$probe = Join-Path $TargetRoot ('_writetest_' + $Stamp + '.tmp')
try {
    Set-Content -LiteralPath $probe -Value 'ok' -Encoding ASCII
    Remove-Item -LiteralPath $probe -Force
    Write-Host 'write test: OK'
}
catch { throw ("target is not writable: " + $TargetRoot + " -> " + $_.Exception.Message) }

# PATCH 2026-10-20 (execution round only, safety unchanged):
#   The original check threw whenever ANY of the four category dirs was already
#   non-empty.  That makes it impossible to run the plan's batches as separate
#   copy -> verify -> remove cycles (batch 3 creates vm_tools, which then blocks
#   batch 1/2, and batch 1/2 share backups_<date>).  The check is therefore
#   narrowed to the per-item directories (two path levels) that THIS batch is
#   about to write.  The authoritative no-overwrite guarantee stays the per-file
#   target check right below: no existing file is ever overwritten.
$ItemDirs = @{}
foreach ($t in $Tasks) {
    $rel = $t.Target.Substring($TargetRoot.Length).TrimStart('\')
    $parts = $rel -split '\\'
    $dirCount = $parts.Length - 1
    if ($dirCount -lt 1) { continue }
    $take = [Math]::Min(2, $dirCount)
    $ItemDirs[(($parts[0..($take - 1)]) -join '\')] = $true
}
$busy = @()
foreach ($k in $ItemDirs.Keys) {
    $full = Join-Path $TargetRoot $k
    if (Test-Path -LiteralPath $full) {
        $existing = Get-ChildItem -LiteralPath $full -Force -ErrorAction SilentlyContinue
        if ($existing) { $busy += $full }
    }
}
if ($busy.Count -gt 0) {
    throw ("destination already exists and is not empty: " + ($busy -join '; ') + "  (use -Stamp to pick a new run)")
}
foreach ($t in $Tasks) {
    if (Test-Path -LiteralPath $t.Target) { throw ("target file already exists, refusing to overwrite: " + $t.Target) }
}
Write-Host 'preflight: no destination conflicts'

# ---------------------------------------------------------------- copy loop
New-Item -ItemType Directory -Force -Path $tMan | Out-Null
$rows = New-Object System.Collections.ArrayList
$i = 0; $fail = 0
foreach ($t in $Tasks) {
    $i++
    if (($i % 200) -eq 0) { Write-Host ("  ... {0}/{1}" -f $i, $totalFiles) }
    $parent = Split-Path -Parent $t.Target
    if (-not (Test-Path -LiteralPath $parent)) { New-Item -ItemType Directory -Force -Path $parent | Out-Null }

    $srcHash = (Get-FileHash -LiteralPath $t.Source -Algorithm MD5).Hash.ToLower()
    $size = (Get-Item -LiteralPath $t.Source).Length
    $status = 'COPIED'
    try {
        Copy-Item -LiteralPath $t.Source -Destination $t.Target
        $dstHash = (Get-FileHash -LiteralPath $t.Target -Algorithm MD5).Hash.ToLower()
        if ($dstHash -ne $srcHash) { $status = 'FAIL_MD5' }
        elseif ((Get-Item -LiteralPath $t.Target).Length -ne $size) { $status = 'FAIL_SIZE' }
        else { $status = 'COPIED' }
    }
    catch {
        $status = 'FAIL_COPY'
        Write-Warning ("copy failed: " + $t.Source + " -> " + $_.Exception.Message)
    }
    if ($status -ne 'COPIED') { $fail++ }
    [void]$rows.Add([pscustomobject]@{
            seq         = $i
            batch       = $t.BatchNo
            category    = $t.Category
            source_path = $t.Source
            size_bytes  = $size
            md5         = $srcHash
            target_path = $t.Target
            status      = $status
            note        = $t.Note
        })
}

$manifestPath = Join-Path $tMan ('manifest_' + $Stamp + '.csv')
$rows | Export-Csv -LiteralPath $manifestPath -NoTypeInformation -Encoding UTF8
Write-Host ''
Write-Host ("manifest written: " + $manifestPath)
Write-Host ("files: {0}   failures: {1}" -f $rows.Count, $fail)
if ($fail -gt 0) {
    Write-Host 'AT LEAST ONE FILE FAILED -- do NOT run 02_verify_then_remove.ps1 for this manifest.' -ForegroundColor Red
    exit 1
}
Write-Host 'all copies verified by MD5. No source file was deleted.' -ForegroundColor Green
Write-Host ('next step (when you are ready): pwsh -File vmctl\cleanup\02_verify_then_remove.ps1 -Manifest "' + $manifestPath + '"')
exit 0
