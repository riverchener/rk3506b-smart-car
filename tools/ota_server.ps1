# =============================================================================
# ota_server.ps1 —— 起 OTA 静态 HTTP 服务 + 打通"板子 -> PC"的下载通道
# =============================================================================
# 为什么需要它：
#   本板 eth0 挂在 192.168.137.0/24（Windows 的 ICS 网段），rootfs 里也没有
#   任何指向 VM 的路由，所以"板子直接访问 PC/VM 的 IP"是不通的。
#   解决办法是用 adb 自带的隧道，**不需要网络配置、不需要动路由**：
#
#       adb reverse tcp:18080 tcp:18080
#
#   作用：板子上的 adbd 在 127.0.0.1:18080 开一个监听端口，所有连接经 USB
#   回到 PC 上 adb server 所在的 18080。于是板端 `wget http://127.0.0.1:18080/...`
#   就落到了这里起的 python http.server 上。
#
#   实测（2026-10-12）：
#     VM$ adb reverse tcp:18080 tcp:18080   ->  18080
#     板$ wget -q -O - http://127.0.0.1:18080/ota/test.txt   ->  内容正确, rc=0
#
# 用法：
#   powershell -File tools\ota_server.ps1                 # 起服务并打通隧道
#   powershell -File tools\ota_server.ps1 -Stop           # 停掉 HTTP 服务（含隧道）
#   powershell -File tools\ota_server.ps1 -Port 18081
# =============================================================================
# 2026-10-18 修改记录（为"能在 Windows PowerShell 5.1 下真正跑起来"而改）
# -----------------------------------------------------------------------------
#  1) 本文件另存为 UTF-8 **带 BOM**。
#     原文件是无 BOM 的 UTF-8，正文含中文注释/字符串；PS 5.1 对**无 BOM** 脚本
#     按 ANSI(GBK) 解码，中文字节被解成乱码后把引号/花括号"吃掉"，PSParser
#     于是报 20 条语法错误 —— 其中一条就是"标记 && 不是此版本中的有效语句分隔符"。
#     ⚠ 那条 && 报错是**连带误报**：本文件里的 `&&` 全部写在双引号字符串内部，
#       是发给**远端 bash** 的运算符，不是 PS 运算符。逐处核对过，没有一处
#       裸写 `&&`（PS 5.1 确实不支持 `&&`，但本文件从不裸写它）。
#     加 BOM 后 PSParser 报 0 错误（实测）。
#  2) 原第 50 / 66 行的 `pkill -f 'http.server <port>'` 会**匹配到自己这条
#     ssh/bash 命令行**并把自己杀掉 ⇒ 后面的命令全部不执行、服务起不来。
#     （build_out\docs\OTA使用步骤与演示记录_20261013.md 步骤 2 已明确警告过）
#     ⇒ 改成 `pgrep -f` 取 PID + 排除 `$$` 的精确 kill。
#  3) 原第 66 行 `nohup ... &` 在非交互 SSH 会话里会被收掉 ⇒ 改
#     `setsid ... > 日志 2>&1 < /dev/null &`（与 vmctl/ota_server.py 的做法一致）。
#  4) 远端 sh 代码不再内联在 python 命令行里，而是先落成本地临时文件，再用
#     vmctl/vm.py 的 `exec @<文件>` 约定（内部是 `bash -s`，脚本走 stdin）送过去。
#     原因：PS 5.1 向原生进程传参时会**吃掉参数里的双引号**（实测
#     `exec 'echo "two words"'` 到了远端变成 `echo two`），内联必然踩坑。
#  5) 健康检查用 `wget -O /tmp/_m.json` 拿字节数 + `curl -o /dev/null -w` 拿
#     HTTP 状态码（两个工具在 VM 上都实测存在）。
#  6) 原 `$vmctl = Join-Path $here "..\vmctl\vm.py"` 在本仓库的真实目录结构下
#     **找不到文件**（脚本在 <repo>\Linux车载系统\tools\，vmctl 在 <repo>\vmctl\，
#     差两级）。改成从 $here 依次往上找一/二/三级再判断存在。
#  7) 起服务加 `-u`：python3.8 的 stderr 重定向到文件时是块缓冲(8KB)，
#     不加 -u 时 /tmp/otasrv.log 会长期停在 0 字节（实测过），
#     定位问题时看不到任何请求行。
# =============================================================================
param(
    [int]$Port = 18080,
    [string]$ReleaseDir = "",
    [switch]$Stop
)

$ErrorActionPreference = "Stop"
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
if (-not $ReleaseDir) { $ReleaseDir = Join-Path $here "ota_release" }

$remoteDir = "/home/cjh/otasrv"

# 找 vmctl\vm.py：本脚本在 <repo>\Linux车载系统\tools\ 下，vmctl 实际在 <repo>\vmctl\，
# 所以要往上找两级（原文件只写死了一级 "..\vmctl\vm.py"，实测找不到 -> 见修改记录 6）
$vmctl = $null
foreach ($rel in @("..\vmctl\vm.py", "..\..\vmctl\vm.py", "..\..\..\vmctl\vm.py")) {
    $cand = [System.IO.Path]::GetFullPath((Join-Path $here $rel))
    if (Test-Path -LiteralPath $cand) { $vmctl = $cand; break }
}
if (-not $vmctl) { Write-Host "!! 找不到 vmctl\vm.py（已在 $here 往上找了三级）" -ForegroundColor Red; exit 1 }
Write-Host "vmctl: $vmctl" -ForegroundColor DarkGray

# ------------------------------------------------------------------ 远端执行器
# 把一段远端 sh 代码落成临时文件 -> python vm.py exec @file（见修改记录 4）
function Invoke-Remote {
    param([string]$Body)
    $tmp = Join-Path $env:TEMP ("ota_remote_" + [guid]::NewGuid().ToString("N") + ".sh")
    $txt = $Body.Replace("`r`n", "`n").Replace("`r", "`n")
    [System.IO.File]::WriteAllText($tmp, $txt, (New-Object System.Text.UTF8Encoding($false)))
    try {
        python $vmctl exec ("@" + $tmp)
    } finally {
        Remove-Item -LiteralPath $tmp -Force -ErrorAction SilentlyContinue
    }
}

# =========================================================== -Stop：停服务
if ($Stop) {
    Write-Host "== 停止 VM 上的 HTTP 服务 (端口 $Port) ==" -ForegroundColor Cyan
    $body = @'
set -u
PORT=@@PORT@@
ADB="$HOME/tools/platform-tools/adb"

echo "--- 停 HTTP 服务：按 PID 精确 kill（不误伤本条命令）---"
for p in $(pgrep -f "http.server $PORT"); do
  [ "$p" = "$$" ] && continue
  echo "  kill pid=$p"
  kill "$p" 2>/dev/null
done
sleep 1
if pgrep -f "http.server $PORT" > /dev/null 2>&1; then
  echo "  STILL_RUNNING（还没死，可再补一刀 kill -9）"
else
  echo "  STOPPED"
fi

echo "--- 端口 $PORT ---"
ss -ltnp 2>/dev/null | grep -E ":$PORT([^0-9]|$)" || echo "  (端口已释放)"

echo "--- 撤掉 adb reverse 隧道 ---"
$ADB reverse --remove "tcp:$PORT" </dev/null 2>&1
$ADB reverse --list 2>&1
'@
    Invoke-Remote ($body.Replace('@@PORT@@', "$Port"))
    Write-Host ""
    Write-Host "已停止。日志文件仍保留在 VM 的 /tmp/otasrv.log" -ForegroundColor Green
    exit 0
}

if (-not (Test-Path $ReleaseDir)) {
    Write-Host "!! 发布目录不存在: $ReleaseDir" -ForegroundColor Red
    Write-Host "   先跑: python tools\ota_publish.py publish --version 1.1.0 --binary <app>"
    exit 1
}
if (-not (Test-Path (Join-Path $ReleaseDir "ota\manifest.json"))) {
    Write-Host "!! $ReleaseDir\ota\manifest.json 不存在" -ForegroundColor Red
    exit 1
}

# =============================================== 1) 把发布目录同步到 VM
Write-Host "== 1) 把发布目录同步到 VM ==" -ForegroundColor Cyan
$tarLocal = Join-Path $env:TEMP ("ota_release_" + [guid]::NewGuid().ToString("N") + ".tar")
# 用 tar 打包再整体推送，比逐个文件 sftp 快，也避免中文/空格路径问题
Push-Location $ReleaseDir
try {
    tar -cf $tarLocal .
} finally {
    Pop-Location
}
python $vmctl exec ("mkdir -p " + $remoteDir)
python $vmctl put $tarLocal "$remoteDir/release.tar"

$body = @'
set -u
DIR=@@DIR@@
cd "$DIR" || exit 1
rm -rf ota VERSION
tar -xf release.tar
rm -f release.tar
echo "--- ota/ ---"
ls -l ota/
echo "--- VERSION ---"
cat VERSION 2>/dev/null || echo "(无 VERSION)"
echo "--- manifest.json ---"
cat ota/manifest.json
'@
Invoke-Remote ($body.Replace('@@DIR@@', $remoteDir))
Remove-Item -LiteralPath $tarLocal -Force -ErrorAction SilentlyContinue

# =============================================== 2) 起静态 HTTP 服务
Write-Host "== 2) 在 VM 上起静态 HTTP 服务 (端口 $Port) ==" -ForegroundColor Cyan
$body = @'
set -u
PORT=@@PORT@@
DIR=@@DIR@@

echo "--- 起服务前：端口占用检查 ---"
if ss -ltn 2>/dev/null | grep -qE ":$PORT([^0-9]|$)"; then
  echo "  端口 $PORT 原本已被占用 -> 按 PID 精确停掉旧实例"
  for p in $(pgrep -f "http.server $PORT"); do
    [ "$p" = "$$" ] && continue
    echo "    kill 旧实例 pid=$p"
    kill "$p" 2>/dev/null
  done
  sleep 1
else
  echo "  端口 $PORT 空闲"
fi

cd "$DIR" || exit 1
# setsid：脱离 SSH 会话，SSH 断开也不会被收掉（修改记录 3）
# -u   ：python3.8 的 stderr 重定向到文件时是块缓冲(8KB)，不加 -u 日志文件会长期
#        停在 0 字节、看不到请求行（修改记录 7）
setsid python3 -u -m http.server "$PORT" --bind 0.0.0.0 --directory "$DIR" \
       > /tmp/otasrv.log 2>&1 < /dev/null &
sleep 3

echo "--- 进程 ---"
for p in $(pgrep -f "http.server $PORT"); do
  printf "  pid=%s  cmd=" "$p"
  tr '\0' ' ' < "/proc/$p/cmdline"
  echo
done

echo "--- 监听 ---"
ss -ltnp 2>/dev/null | grep -E ":$PORT([^0-9]|$)" || echo "  !! 没在监听"

echo "--- VM 本地自测（manifest.json）---"
wget -q -O /tmp/_m.json "http://127.0.0.1:$PORT/ota/manifest.json"
echo "  字节数 = $(wc -c < /tmp/_m.json)"
printf "  HTTP   = "
curl -s -o /dev/null -w '%{http_code}\n' "http://127.0.0.1:$PORT/ota/manifest.json"

echo "--- 服务日志 /tmp/otasrv.log ---"
cat /tmp/otasrv.log 2>/dev/null || echo "(空)"
'@
Invoke-Remote ($body.Replace('@@PORT@@', "$Port").Replace('@@DIR@@', $remoteDir))

# =============================================== 3) 打通 adb reverse 隧道
Write-Host "== 3) 打通 adb reverse 隧道 (板子 127.0.0.1:$Port -> VM) ==" -ForegroundColor Cyan
$body = @'
set -u
PORT=@@PORT@@
ADB="$HOME/tools/platform-tools/adb"

echo "--- adb devices ---"
$ADB devices -l 2>&1
echo "--- adb reverse tcp:$PORT tcp:$PORT ---"
$ADB reverse "tcp:$PORT" "tcp:$PORT" </dev/null 2>&1
echo "--- adb reverse --list ---"
$ADB reverse --list 2>&1
'@
Invoke-Remote ($body.Replace('@@PORT@@', "$Port"))

# =============================================== 4) 板端自测
Write-Host "== 4) 板端自测：直接从板子拉 manifest ==" -ForegroundColor Cyan
$body = @'
set -u
PORT=@@PORT@@
ADB="$HOME/tools/platform-tools/adb"

echo "--- 板端 wget（只读：输出到 stdout，不落盘）---"
$ADB shell "wget -q -O - http://127.0.0.1:$PORT/ota/manifest.json; echo; echo wget_rc=\$?" </dev/null 2>&1
'@
Invoke-Remote ($body.Replace('@@PORT@@', "$Port"))

Write-Host ""
Write-Host "OK。板端现在可以：" -ForegroundColor Green
Write-Host "    smartcar-ota check"
Write-Host "    smartcar-ota update"
Write-Host ""
Write-Host "（板端 URL 默认 http://127.0.0.1:18080/ota/manifest.json；"
Write-Host "  换端口就用 SMARTCAR_OTA_URL=http://127.0.0.1:<port>/ota/manifest.json）"
Write-Host "日志：VM 上 /tmp/otasrv.log；服务目录：$remoteDir"
