#ifndef RT_CONFIG_H__
#define RT_CONFIG_H__

/*
 * RT-Thread configuration for the RK3506 "BUS M0" (AMP co-processor).
 *
 * This file is the single source of truth for the build: RT-Thread's SCons
 * build reads it (building.py parses the #defines) and every kernel source
 * guards itself with these macros.  It is deliberately hand-written and
 * minimal - the M0 has 32 KiB of RAM shared with .text/.rodata/.data/.bss,
 * the heap and the stacks, so every disabled component is bytes won back.
 *
 * NOT enabled on purpose (all of these are plain missing on the M0 build):
 *   device framework (RT_USING_DEVICE), finsh/msh, DFS/ELM, network, POSIX,
 *   event / messagequeue / mempool / soft-timer / hooks / debug asserts.
 */

/* RT-Thread Kernel */

#define RT_USING_CORE_RTTHREAD
#define RT_NAME_MAX 8
#define RT_ALIGN_SIZE 4
/*
 * 8 priority levels instead of 32: this firmware needs 4 distinct levels
 * (carctl > sr04 > rpmsg > main, plus idle), and each level costs 4 bytes of
 * rt_thread_priority_table[] plus a wider bitmap walk in the scheduler.
 */
#define RT_THREAD_PRIORITY_8
#define RT_THREAD_PRIORITY_MAX 8

/*
 * 8 kHz tick = 125 us.
 *
 * Every interval this firmware has to produce is an exact multiple of 125 us
 * (1 kHz software PWM: 500 us on-phase; 2 kHz buzzer carrier: 125 us / 375 us;
 * 1.5 Hz indicator: 333000 us; reverse beep: 60000/120000/620000 us), so the
 * tick grid quantises none of them.  The tick ISR costs well under 1 % of a
 * 200 MHz M0.
 */
#define RT_TICK_PER_SECOND 8000

#define RT_USING_IDLE_HOOK
#define RT_IDLE_HOOK_LIST_SIZE 1
#define IDLE_THREAD_STACK_SIZE 192

/* Inter-Thread communication */

#define RT_USING_SEMAPHORE
#define RT_USING_MUTEX
#define RT_USING_MAILBOX

/* Memory Management */

#define RT_USING_SMALL_MEM
#define RT_USING_SMALL_MEM_AS_HEAP
#define RT_USING_HEAP

/* Kernel Device Object */

#define RT_USING_CONSOLE
/* 64 B is enough for the 53 character sample line and keeps the rt_kprintf
 * frame (which is allocated on the caller's stack) small */
#define RT_CONSOLEBUF_SIZE 64

#define RT_VER_NUM 0x40101
#define ARCH_ARM
#define ARCH_ARM_CORTEX_M
#define ARCH_ARM_CORTEX_M0

/* RT-Thread Components */

#define RT_USING_USER_MAIN
#define RT_MAIN_THREAD_STACK_SIZE 512
#define RT_MAIN_THREAD_PRIORITY 5

/* kservice optimization */

#define RT_KSERVICE_USING_STDLIB

#endif
