#ifndef OTA_VERSION_H
#define OTA_VERSION_H

/*
 * ota_version.h —— 软件版本号的**唯一来源**（2026-10-12 新增，为 OTA 服务）
 * ============================================================================
 * 为什么要有这个文件：
 *   OTA 的核心问题是"我现在跑的是哪个版本？装上去的到底是哪一份？"。
 *   以前这个答案只存在于编译时间戳里，板子上无法查询、无法对比、无法取证。
 *   这里把版本号固化成一个**带独特前缀的字符串常量**：
 *
 *     SMARTCAR_OTA_VERSION=v1.0.0
 *
 *   它会被编译进 ELF 的 .rodata，于是板端可以直接取证：
 *     grep -ao 'SMARTCAR_OTA_VERSION=[0-9A-Za-z._-]*' \
 *         /userdata/smart_car/Smart_in-car_entertainment
 *     readlink /proc/$(pidof Smart_in-car_entertainment)/exe   # 再看跑的是哪个文件
 *
 *   这条信息同时用于：
 *     ① OTA 升级后校验"磁盘上的文件"确实是新版本（md5 之外的第二重证据）；
 *     ② OTA 后确认"正在跑的进程"就是新版本（/proc/<pid>/exe 指向的文件）；
 *     ③ 失败时判断该回滚到哪个 slot。
 *
 * 版本号从哪里来（优先级从高到低）：
 *   ① ota_version_local.h —— 发布脚本在编译前生成的那份，**发布版本走这条**；
 *   ② 下面的默认值 —— 手工编译（比如只跑 qmake && make 调试）时用的兜底值。
 *
 * 为什么用"生成一个头文件"而不是 `-DSMARTCAR_OTA_VERSION_STR=1.1.0`：
 *   shell 转义要穿过 PowerShell -> python -> ssh -> sh -> qmake 五层，
 *   实测被啃成 `-D...=\1.1.0\`，宏变成裸数字 1.1，编译直接报
 *   "expected ')' before numeric constant"。写文件没有转义问题，一次就对。
 */
#if defined(__has_include)
#  if __has_include("ota_version_local.h")
#    include "ota_version_local.h"
#  endif
#endif

#ifndef SMARTCAR_OTA_VERSION_STR
#define SMARTCAR_OTA_VERSION_STR "v1.0.0"
#endif

/*
 * 前缀本身也是取证锚点：board/smartcar-ota、otapage.cpp、
 * tools/ota_publish.py 三处都用同一个字面量匹配，改这里要一起改。
 */
#define SMARTCAR_OTA_VERSION_TAG "SMARTCAR_OTA_VERSION=" SMARTCAR_OTA_VERSION_STR

#endif // OTA_VERSION_H
