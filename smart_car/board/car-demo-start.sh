#!/bin/sh
#
# car-demo-start.sh —— 打开"假帧演示"（不需要电脑、不需要 CAN 总线）
# ============================================================================
# 原理：车机 app 里的 CanMgr 每 100 ms 会看一次 /tmp/smartcar_can_demo 这个标记
# 文件；文件在就自己按时间线造帧（**走的是和真帧完全相同的解析通路**），
# 文件没了就自动停喂 —— 停喂 1 秒后状态会按超时语义变灰/清零。
# 所以这两个脚本只是 touch / rm，不用重启 app。
#
# 时间线（26 秒一轮，循环演示）：
#   怠速 → 车速爬到 66 → 左转 → 刹车 → 安全带未系 + 右转 + 左前门开 → 倒车 → 回到怠速
#
# 用法：
#   sh /userdata/smart_car/car-demo-start.sh    # 起演示（等 ~9 秒再操作看变化）
#   sh /userdata/smart_car/car-demo-stop.sh     # 停止，切回真实 CAN
#
set -u
FLAG=/tmp/smartcar_can_demo
: > "$FLAG"
echo "OK:演示模式已打开（$FLAG）；车机桌面右侧状态栏会按时间线自动变化"
echo "    停止： sh /userdata/smart_car/car-demo-stop.sh"
if ! pidof Smart_in-car_entertainment >/dev/null 2>&1; then
    echo "注意：车机 app 没在跑（pidof Smart_in-car_entertainment 为空）"
fi
