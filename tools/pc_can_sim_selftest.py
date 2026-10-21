#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""pc_can_sim 自检：不接 CAN 硬件，纯逻辑校验下行编帧与上行解码是否符合手册。
   期望值全部来自 docs/CAN_接口定义与操作_20260929.md 的逐位定义。"""
import sys

sys.path.insert(0, __file__.rsplit("\\", 1)[0])
sys.stdout.reconfigure(encoding="utf-8", errors="replace")

import pc_can_sim as m                                                   # noqa: E402

ok = fail = 0


def check(name, got, want):
    global ok, fail
    if got == want:
        ok += 1
        print("  ✓ %-46s %s" % (name, got))
    else:
        fail += 1
        print("  ✗ %-46s got=%s want=%s" % (name, got, want))


print("=== 下行 0x100 编帧 ===")
d = m.build_0x100(speed=66, gear="D", left=True)
check("车速66+D档+左转 -> B0/B1/B2", (d[0], d[1], d[2]), (66, 0x01, 3))
d = m.build_0x100(speed=40, gear="R", brake=True, reverse=True, seatbelt_off=True)
check("刹车+倒车+安全带未系 -> B1", d[1], 0x04 | 0x08 | 0x10)
d = m.build_0x100(speed=None, gear="P")
check("车速无效 -> B0=0xFF", d[0], 0xFF)
check("0x100 描述", m.describe_tx(0x100, bytes([66, 0x01, 3, 8, 0, 0, 0, 0])),
      "0x100 车速=66 km/h 档位=D 转速=800rpm 左转")

print("=== 下行 0x101 / 0x102 编帧 ===")
check("左前门开+未上锁", tuple(m.build_0x101(0x01, False)[:2]), (1, 0))
check("已锁", m.build_0x101(0, True)[1], 1)
d = m.build_0x102(lights=0x03, ac=True, recirc=True, ac_temp=24, outside=20)
check("AC+内循环+24℃+车外20℃", (d[1], d[2], d[3]), (0x03, 24, 60))
check("0x102 描述", m.describe_tx(0x102, bytes([0x03, 0x03, 24, 60, 0, 0, 0, 0])),
      "0x102 灯[近光/远光] AC开 24℃ 车外20℃")

print("=== 上行 0x201/0x202/0x203 解码 ===")
check("0x201 空调开 24℃ 风量3 内循环",
      m.decode_0x201(bytes([1, 24, 3, 1, 0, 0, 0, 0])),
      "空调：开  温度 24℃  风量 3  内循环")
check("0x201 空调关", m.decode_0x201(bytes([0, 16, 0, 0, 0, 0, 0, 0])),
      "空调：关  温度 16℃  风量 0  外循环")
check("0x202 左前 升 100%", m.decode_0x202(bytes([1, 1, 100, 0, 0, 0, 0, 0])),
      "车窗：左前 升  目标 100%")
check("0x202 全部 降 0%", m.decode_0x202(bytes([0, 2, 0, 0, 0, 0, 0, 0])),
      "车窗：全部 降  目标 0%")
check("0x203 开近光/关远光", m.decode_0x203(bytes([0x01, 0x02, 0, 0, 0, 0, 0, 0])),
      "灯光：开[近光] 关[远光]")

print("=== 与车机内置演示时间线一致性（掩码位）===")
check("倒车位 = bit3", m.build_0x100(speed=0, gear="R", reverse=True)[1], 0x08)
check("双闪位 = bit6", m.build_0x100(speed=0, gear="P", hazard=True)[1], 0x40)
check("手刹位 = bit5", m.build_0x100(speed=0, gear="P", handbrake=True)[1], 0x20)

print("\n结果：%d 项通过，%d 项失败" % (ok, fail))
sys.exit(1 if fail else 0)
