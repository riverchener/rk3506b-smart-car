#!/bin/sh
#
# car-demo-stop.sh —— 停止假帧演示，切回真实 CAN
# 说明见 car-demo-start.sh：app 只是不再喂帧，1 秒后按超时语义
# （车速/档位转暗灰、指示灯清零显示"关"）自动收敛。
#
set -u
FLAG=/tmp/smartcar_can_demo
rm -f "$FLAG"
echo "OK:演示模式已关闭（已删除 $FLAG）"
echo "    1 秒后车机状态会按超时语义变灰/清零；真实 CAN 帧一进来立刻恢复"
