# 电脑侧 CAN 模拟器（车况由电脑发给车机）

对应车机 CAN 功能的两端：**电脑 → 车机** 发车况（0x100/0x101/0x102），
**车机 → 电脑** 收控制回执（0x201/0x202/0x203）。

## 1. 准备

- 硬件：PEAK **PCAN-USB** 分析仪 + **SN65HVD230** 收发模块（板子侧 A19=A 的 D、A20=R，
  两端各一个 **120 Ω** 终端电阻，**共地**）。
- 驱动：装 PEAK 驱动，设备管理器里能看到 PCAN-USB。
- 软件二选一（脚本会自动挑）：
  ```bat
  pip install "python-can[pcan]"
  ```
  或者用 PEAK 驱动自带的 `PCANBasic.py`（把它所在目录加进 `PYTHONPATH` 即可，免 pip）。

## 2. 跑起来

```bat
:: 开车时间线（和车机内置演示同一节拍，便于两端对照）
python pc_can_sim.py --scenario drive

:: 固定车况（发 66 km/h、D 档、左转）
python pc_can_sim.py --scenario static --speed 66 --gear D --left

:: 手发一帧（十六进制/十进制都行）
python pc_can_sim.py --raw 0x100 42 01 03 00 00 00 00 00

:: 只听：在车机「车内控制」页点空调/车窗/灯光，这里会解码打印
python pc_can_sim.py --listen

:: 换通道/波特率
python pc_can_sim.py --channel PCAN_USBBUS1 --bitrate 500000
```

跑起来后会看到这样的输出（车况谁在变化一目了然）：

```
  TX 0x100 车速=66 km/h 档位=D 转速=800rpm 左转
  TX 0x101 开[-] 已锁
  TX 0x102 灯[近光] AC开 24℃ 车外20℃
  RX ← 车机 空调：开  温度 24℃  风量 3  内循环
  RX ← 车机 车窗：左前 升  目标 100%
  RX ← 车机 灯光：开[近光] 关[远光]
```

## 3. 车机侧会有什么反应

| 电脑发 | 车机表现 |
| --- | --- |
| `0x100` 车速 66 | 桌面右侧状态栏车速 66、进度条同步 |
| `0x100` bit0 左转 | 状态栏左箭头 **1.5 Hz 闪烁**；**A29 外接 LED 同步闪** |
| `0x100` bit3 倒车 | 状态栏"倒车中"；**蜂鸣器"滴滴-滴滴-滴滴"（60/120/620 ms，音量约警报一半）** 直到该位清零 |
| `0x100` bit4 安全带未系 | 安全带图标变红并出现断开斜杠 |
| `0x101` bit0 左前门 | 俯视车图上左前门**向外摆出并变红** |
| `0x102` bit0 近光 | 灯光行"近光 亮"；若在车机上也点过近光，两 LED 半亮 |
| 停发 > 1 s | 按超时语义：车速/档位转暗灰、指示灯清零显示"关"、转向灯停闪、蜂鸣停 |

## 4. 不接硬件也能测（vcan）

板子上有内核 CAN 支持后，不需要分析仪就能自测：

```sh
adb shell "ip link add dev vcan0 type vcan; ip link set vcan0 up"
adb shell "cantool send -i vcan0 100 42 01 03 00 00 00 00 00"     # 板子自己发一帧
adb shell "cantool dump -i vcan0"                                  # 另一个窗口收
adb shell "SMARTCAR_CAN_IFACE=vcan0 /userdata/smart_car/Smart_in-car_entertainment"
```

另有**完全不依赖 CAN 的演示模式**（车机自己造假帧，走同一条解析通路）：

```sh
adb shell "sh /userdata/smart_car/car-demo-start.sh"   # 起（等 ~9 秒看状态灯变化）
adb shell "sh /userdata/smart_car/car-demo-stop.sh"    # 停（1 秒后全部转灰/清零）
```

## 5. 自检（不开硬件）

```bat
python pc_can_sim_selftest.py
```
校验 `pc_can_sim.py` 的编帧/解码是否与 `docs/CAN_接口定义与操作_20260929.md`
的逐位定义一致（16 项：车速/档位掩码、门窗、灯光、空调、车窗动作、超时位）。
