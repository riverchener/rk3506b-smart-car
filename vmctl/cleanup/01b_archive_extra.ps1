<#
01b_archive_extra.ps1 -- Stage 1b: COPY-ONLY archive of the ADDITIONAL items that
01_archive.ps1 deliberately left out (plan section 4.1 item 7 / decisions D1, D2,
D5, D6, D9).

WHY THIS FILE EXISTS
  The user approved a stricter rule for this execution round:
      "keep whatever the product needs to build / burn / run / accept,
       move everything else to E:\<backup>".
  That adds these scopes on top of 01_archive.ps1 (which covers B1 vmctl set,
  B2 backups, C old-feature items only):
      * build_out\flash\update_a7_rollback.img + update_amp.img   (brick-recovery)
      * build_out\docs\_*            evidence dirs  (minus the 3 that 01 owns)
      * every build_out evidence dir + every loose file at build_out top level
      * docs top-level one-off artifacts (.sh .txt .dtb .img _askpass.*)
      * build_out\board_backup_*     board-file snapshots
      * can_web\_chrome_profile*     rebuildable browser profile
      * archive\                     previous cleanup archive
      * generate_resume.py + RK3506B*.docx   (non-product)
  01_archive.ps1 is NOT modified (its copy/verify logic stays exactly as
  delivered); this is a sibling that writes the same CSV schema, so the
  unmodified 02_verify_then_remove.ps1 and 03_rollback.ps1 read it as-is.

WHAT IT DOES
  * Copies (never moves, never deletes) each item to E:\<backup>\<category>_20261019
  * MD5 of source AND target for every single file; one CSV row per file in
    E:\<backup>\_manifests\manifest_extra_<stamp>.csv
  * Refuses to overwrite any existing target file
  * Prints per category / per batch totals first; -DryRun writes nothing
  * Exit code 1 if any file failed -> do NOT run 02 for that manifest

WHAT IT DOES NOT DO
  * No deletion of any source file (only 02_verify_then_remove.ps1 deletes,
    and only after every MD5 in the manifest passes).
  * No board / rootfs / M0 / git operation.

ENCODING NOTE
  Source is deliberately ASCII-only; all CJK path components are built from
  code points by U() so it behaves identically under Windows PowerShell 5.1
  and PowerShell 7 (see the note in 01_archive.ps1 lines 18-24).

USAGE
  pwsh -File vmctl\cleanup\01b_archive_extra.ps1 -DryRun
  pwsh -File vmctl\cleanup\01b_archive_extra.ps1 -Batch 1   # evidence
  pwsh -File vmctl\cleanup\01b_archive_extra.ps1 -Batch 2   # flash rollback imgs
  pwsh -File vmctl\cleanup\01b_archive_extra.ps1 -Batch 3   # misc / backups / non-product
  pwsh -File vmctl\cleanup\01b_archive_extra.ps1            # all of the above
#>
[CmdletBinding()]
param(
    [string]$Repo,
    [string]$TargetRoot,
    [int]$Batch = 0,
    [string]$DateTag = '20261019',
    [switch]$DryRun,
    [string]$Stamp = (Get-Date -Format 'yyyyMMdd_HHmmss')
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0

# ---------------------------------------------------------------- CJK strings
function U { param([int[]]$Codes) -join ($Codes | ForEach-Object { [char]$_ }) }
$sBackupName = U 0x8F66, 0x8F7D, 0x5907, 0x4EFD              # car-backup
$sMidProd    = U 0x4E2D, 0x95F4, 0x4EA7, 0x7269              # intermediate products
$sNonProd    = U 0x975E, 0x4EA7, 0x54C1                      # non-product
$sArchive    = U 0x5F52, 0x6863                              # archive
$sInnerRepo  = 'Linux' + (U 0x8F66, 0x8F7D, 0x7CFB, 0x7EDF)  # inner repo "Linux car system"

if (-not $Repo) { $Repo = (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)) }
if (-not $TargetRoot) { $TargetRoot = 'E:\' + $sBackupName }
$Repo = (Resolve-Path -LiteralPath $Repo).Path.TrimEnd('\')

$tRoot = $TargetRoot
$tEv   = Join-Path $tRoot ('evidence_' + $DateTag)
$tFl   = Join-Path $tRoot ('flash_rollback_' + $DateTag)
$tMid  = Join-Path $tRoot ($sMidProd + '_' + $DateTag)
$tBkp  = Join-Path $tRoot ('backups_' + $DateTag)
$tNon  = Join-Path $tRoot ($sNonProd + '_' + $DateTag)
$tArc  = Join-Path $tRoot ($sArchive + '_' + $DateTag)
$tMan  = Join-Path $tRoot '_manifests'

$bo = Join-Path $Repo 'build_out'
$dd = Join-Path $bo 'docs'

# ------------------------------------------------- keep zones (internal guard)
# Nothing inside these prefixes may ever be archived by this script.  The two
# brick-recovery images are the single allowed exception inside build_out\flash.
$KeepPrefixes = @(
    (Join-Path $Repo 'vmctl'),
    (Join-Path $Repo 'docs'),
    (Join-Path $Repo ($sInnerRepo + '\Smart_car')),
    (Join-Path $Repo ($sInnerRepo + '\docs')),
    (Join-Path $Repo ($sInnerRepo + '\build_out')),
    (Join-Path $Repo ($sInnerRepo + '\tools\ota_release')),
    (Join-Path $bo 'docs\book'),
    (Join-Path $bo 'docs\blocks'),
    (Join-Path $bo ('docs\' + (U 0x6559, 0x7A0B))),
    (Join-Path $bo 'docs\acc20261013'),
    (Join-Path $bo 'flash'),
    (Join-Path $bo 'pfshot'),
    (Join-Path $bo 'm0diag')
)
$AllowExact = @(
    (Join-Path $bo 'flash\update_a7_rollback.img'),
    (Join-Path $bo 'flash\update_amp.img')
)

# ---------------------------------------------------------------- task builder
$Tasks = New-Object System.Collections.ArrayList

function Add-One {
    param([string]$Category, [int]$BatchNo, [string]$Src, [string]$Dst, [string]$Note)
    if (-not (Test-Path -LiteralPath $Src)) { Write-Warning ('missing, skipped: ' + $Src); return }
    foreach ($kp in $KeepPrefixes) {
        if ($Src -like ($kp + '\*')) {
            if ($AllowExact -notcontains $Src) { throw ('internal guard: refuse to archive keep-zone file: ' + $Src) }
        }
    }
    # defense in depth: a file sitting directly in build_out\docs may only be an
    # explicit one-off artifact; never a deliverable (.md/.pdf/.docx/.py/.json).
    if ((Split-Path -Parent $Src) -eq $dd) {
        $okExt = @('.sh', '.txt', '.dtb', '.img', '.cmd', '.exe')
        $ext = [System.IO.Path]::GetExtension($Src).ToLower()
        if ($okExt -notcontains $ext) { throw ('internal guard: refuse to archive a docs top-level deliverable: ' + $Src) }
    }
    [void]$Tasks.Add([pscustomobject]@{
            Category = $Category; BatchNo = $BatchNo
            Source   = $Src; Target = $Dst; Note = $Note
        })
}
function Add-Tree {
    param([string]$Category, [int]$BatchNo, [string]$SrcDir, [string]$DstDir, [string]$Note)
    if (-not (Test-Path -LiteralPath $SrcDir)) { Write-Warning ('missing, skipped: ' + $SrcDir); return }
    $parent = $SrcDir.TrimEnd('\')
    Get-ChildItem -LiteralPath $parent -Recurse -File -Force -ErrorAction SilentlyContinue | ForEach-Object {
        $rel = $_.FullName.Substring($parent.Length).TrimStart('\')
        Add-One $Category $BatchNo $_.FullName (Join-Path $DstDir $rel) $Note
    }
}
function Want([int]$b) { return ($Batch -eq 0) -or ($Batch -eq $b) }

# --- batch 1: evidence (docs _* dirs + build_out evidence dirs)
if (Want 1) {
    # 01_archive.ps1 already owns these three under build_out\docs, skip them here
    $docsEvSkip = @('_book_backup_20261017', '_wallpaper_backup', '_cleanup_run', '__pycache__')
    Get-ChildItem -LiteralPath $dd -Directory -Force -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -like '_*' -and ($docsEvSkip -notcontains $_.Name) } | ForEach-Object {
            Add-Tree 'E1' 1 $_.FullName (Join-Path $tEv ('docs__' + $_.Name)) 'delivery evidence dir (doc links break)'
        }
    $boEv = @('appshot', 'boxtool', 'btobs', 'btshot', 'canshot', 'canshot_new', 'metashot',
        'ota_evidence', 'ota_srv_20261018', 'pages', 'rc20261013', 'realscreen',
        'realscreen_after', 'realshot', 'srdiag', 'uifix', 'wsflash', '_m0fix_ae',
        '_silence_20261017', '_txvideo_20261018')
    foreach ($n in $boEv) {
        Add-Tree 'E2' 1 (Join-Path $bo $n) (Join-Path $tEv ('build_out__' + $n)) 'build_out evidence dir'
    }
}

# --- batch 2: brick-recovery images
if (Want 2) {
    foreach ($n in @('update_a7_rollback.img', 'update_amp.img')) {
        Add-One 'R1' 2 (Join-Path $bo ('flash\' + $n)) (Join-Path $tFl $n) 'brick-recovery image'
    }
}

# --- batch 3: loose artifacts, board snapshots, chrome profile, archive, non-product
if (Want 3) {
    Get-ChildItem -LiteralPath $bo -File -Force -ErrorAction SilentlyContinue | ForEach-Object {
        Add-One 'M1' 3 $_.FullName (Join-Path (Join-Path $tMid 'build_out_root') $_.Name) 'loose artifact at build_out top level'
    }
    $artExt = @('.sh', '.txt', '.dtb', '.img', '.cmd', '.exe')
    Get-ChildItem -LiteralPath $dd -File -Force -ErrorAction SilentlyContinue |
        Where-Object { $artExt -contains $_.Extension.ToLower() } | ForEach-Object {
            Add-One 'M1' 3 $_.FullName (Join-Path (Join-Path $tMid 'docs_root') $_.Name) 'one-off artifact at docs top level'
        }
    foreach ($n in @('can_docs', 'board_toolbox')) {
        Add-Tree 'M1' 3 (Join-Path $bo $n) (Join-Path $tMid ('build_out__' + $n)) 'text drafts / board toolbox binaries'
    }
    foreach ($n in @('board_backup_original', 'board_backup_f1_20261013', 'board_backup_f2_20261013')) {
        Add-Tree 'B2' 3 (Join-Path $bo $n) (Join-Path $tBkp $n) 'board-file snapshot'
    }
    foreach ($n in @('_chrome_profile', '_chrome_profile_filetest')) {
        Add-Tree 'B2' 3 (Join-Path $Repo ($sInnerRepo + '\tools\can_web\' + $n)) (Join-Path (Join-Path $tBkp 'can_web_chrome_profile') $n) 'rebuildable browser profile'
    }
    Add-Tree 'A1' 3 (Join-Path $Repo 'archive') $tArc 'previous cleanup archive'
    Add-One 'N1' 3 (Join-Path $Repo 'generate_resume.py') (Join-Path $tNon 'generate_resume.py') 'resume generator (non-product)'
    Get-ChildItem -LiteralPath $Repo -File -Force -Filter 'RK3506B*.docx' -ErrorAction SilentlyContinue | ForEach-Object {
        Add-One 'N1' 3 $_.FullName (Join-Path $tNon $_.Name) 'resume docx (non-product)'
    }
}

# ---------------------------------------------------------------- unclassified
# Anything left in build_out that this run neither archives nor keeps as product.
$KeepTop = @('docs', 'flash', 'pfshot', 'm0diag')
$OwnedBy01 = @('ui_backup_incar_ok', 'ui_backup_step1_ok', 'ui_backup_tesla_partial', 'ui_backup_v2_ok')
$movedTop = @{}
foreach ($t in $Tasks) {
    if ($t.Source -like ($bo + '\*')) {
        $rel = $t.Source.Substring($bo.Length).TrimStart('\')
        $movedTop[($rel -split '\\')[0]] = $true
    }
}
$leftover = @()
foreach ($e in (Get-ChildItem -LiteralPath $bo -Force -ErrorAction SilentlyContinue)) {
    if (($KeepTop -contains $e.Name) -or ($OwnedBy01 -contains $e.Name) -or $movedTop.ContainsKey($e.Name)) { continue }
    $leftover += $e.Name
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
Write-Host '============ 01b_archive_extra.ps1 : task list ============'
Write-Host ('repo        : ' + $Repo)
Write-Host ('target root : ' + $TargetRoot)
Write-Host ('date tag    : ' + $DateTag)
Write-Host ('manifest    : ' + (Join-Path $tMan ('manifest_extra_' + $Stamp + '.csv')))
Write-Host ''
Write-Host 'per category:'
foreach ($k in ($catN.Keys | Sort-Object)) {
    Write-Host ("  {0,-3} {1,6} files  {2,14:N0} bytes  {3,9:N2} MiB" -f $k, $catN[$k], $catB[$k], ($catB[$k] / 1MB))
}
Write-Host ''
Write-Host 'per batch:'
$Tasks | Group-Object BatchNo | Sort-Object Name | ForEach-Object {
    $b = 0
    $_.Group | ForEach-Object { $b += (Get-Item -LiteralPath $_.Source).Length }
    Write-Host ("  batch {0} : {1,6} files  {2,9:N2} MiB" -f $_.Name, $_.Count, ($b / 1MB))
}
Write-Host ''
Write-Host ("TOTAL       : {0} files / {1:N0} bytes / {2:N2} MiB" -f $totalFiles, $totalBytes, ($totalBytes / 1MB))
Write-Host ''
if ($leftover.Count -gt 0) {
    Write-Host ('NOT classified in build_out (left in place, review manually): ' + ($leftover -join ', ')) -ForegroundColor Yellow
} else {
    Write-Host 'build_out: every entry is either archived by 01/01b or kept as product.'
}
Write-Host ''

if ($DryRun) {
    Write-Host 'DRY RUN -- nothing was written, nothing was deleted.' -ForegroundColor Yellow
    exit 0
}

# ---------------------------------------------------------------- preflight
$driveLetter = ([System.IO.Path]::GetPathRoot([System.IO.Path]::GetFullPath($TargetRoot))).Substring(0, 1)
$drv = Get-PSDrive -Name $driveLetter -ErrorAction SilentlyContinue
if (-not $drv) { throw ('target drive not found: ' + $driveLetter) }
$free = $drv.Free
Write-Host ("free space on {0}: {1:N0} bytes ({2:N2} GiB)" -f $driveLetter, $free, ($free / 1GB))
if ($free -lt ($totalBytes * 1.2)) { throw 'not enough free space on target drive (need 1.2x payload)' }

if (-not (Test-Path -LiteralPath $TargetRoot)) {
    New-Item -ItemType Directory -Force -Path $TargetRoot | Out-Null
    Write-Host ('created: ' + $TargetRoot)
}
$probe = Join-Path $TargetRoot ('_writetest_' + $Stamp + '.tmp')
try {
    Set-Content -LiteralPath $probe -Value 'ok' -Encoding ASCII
    Remove-Item -LiteralPath $probe -Force
    Write-Host 'write test: OK'
}
catch { throw ("target is not writable: " + $TargetRoot + " -> " + $_.Exception.Message) }

# per-file check is the authoritative no-overwrite guarantee
foreach ($t in $Tasks) {
    if (Test-Path -LiteralPath $t.Target) { throw ("target file already exists, refusing to overwrite: " + $t.Target) }
}
Write-Host 'preflight: no target-file conflicts'

# ---------------------------------------------------------------- copy loop
New-Item -ItemType Directory -Force -Path $tMan | Out-Null
$rows = New-Object System.Collections.ArrayList
$i = 0; $fail = 0
foreach ($t in $Tasks) {
    $i++
    if (($i % 100) -eq 0) { Write-Host ("  ... {0}/{1}" -f $i, $totalFiles) }
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
        Write-Warning ('copy failed: ' + $t.Source + ' -> ' + $_.Exception.Message)
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

$manifestPath = Join-Path $tMan ('manifest_extra_' + $Stamp + '.csv')
$rows | Export-Csv -LiteralPath $manifestPath -NoTypeInformation -Encoding UTF8
Write-Host ''
Write-Host ('manifest written: ' + $manifestPath)
Write-Host ("files: {0}   failures: {1}" -f $rows.Count, $fail)
if ($fail -gt 0) {
    Write-Host 'AT LEAST ONE FILE FAILED -- do NOT run 02_verify_then_remove.ps1 for this manifest.' -ForegroundColor Red
    exit 1
}
Write-Host 'all copies verified by MD5. No source file was deleted.' -ForegroundColor Green
Write-Host ('next step (when you are ready): pwsh -File vmctl\cleanup\02_verify_then_remove.ps1 -Manifest "' + $manifestPath + '"')
exit 0
