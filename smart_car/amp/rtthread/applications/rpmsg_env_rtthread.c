/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * rpmsg_env_rtthread.c - RT-Thread environment layer for rpmsg-lite (M0 side).
 *
 * rpmsg-lite ships environment ports for bare-metal, FreeRTOS, ThreadX, Zephyr,
 * QNX and XOS.  This SDK has no RT-Thread port, so this file is it: the same
 * API surface as rpmsg_env_bm.c, but backed by real RT-Thread primitives
 *
 *     memory    -> rt_malloc / rt_free
 *     lock      -> rt_mutex (recursive owner semantics, thread context)
 *     sleep     -> rt_thread_mdelay
 *     link wait -> rt_semaphore (event driven, no busy loop)
 *     ISR hook  -> virtqueue_notification, called from the mailbox ISR
 *
 * Two deliberate differences from the bare-metal port:
 *
 * 1. The ISR table is 16 entries, not the 510 that rpmsg_config.h sizes for
 *    12 instances.  This firmware runs exactly one instance on link id 3, so
 *    the virtqueue ids are 6 and 7 - the bare-metal table costs 2 KiB of the
 *    M0's 32 KiB for nothing.
 *
 * 2. env_lock_mutex()/env_unlock_mutex() become no-ops inside an interrupt.
 *    rpmsg-lite calls platform_notify() from the RX path (ISR context) when it
 *    returns consumed buffers, and an interrupt handler must never block on a
 *    mutex.  Skipping the lock there is safe: the only thing the ISR path does
 *    is write one mailbox register, and an extra notification can never be lost
 *    or reordered with respect to the vring update that precedes it.
 */

#include "rpmsg_compiler.h"
#include "rpmsg_config.h"
#include "rpmsg_env.h"
#include "rpmsg_platform.h"
#include "virtqueue.h"

#include <rtthread.h>
#include <string.h>

#ifndef SC_RPMSG_ISR_COUNT
#define SC_RPMSG_ISR_COUNT 16U
#endif

static int32_t env_init_counter = 0;

struct isr_info
{
    void *data;
};
static struct isr_info isr_table[SC_RPMSG_ISR_COUNT];

static struct rt_semaphore sc_link_sem;
static rt_uint8_t sc_link_sem_ready;

#if defined(RL_USE_ENVIRONMENT_CONTEXT) && (RL_USE_ENVIRONMENT_CONTEXT == 1)
#error "This RPMsg-Lite port requires RL_USE_ENVIRONMENT_CONTEXT set to 0"
#endif

int32_t env_init(void)
{
    RL_ASSERT(env_init_counter >= 0);
    if (env_init_counter < 0)
    {
        return -1;
    }
    env_init_counter++;
    if (1 < env_init_counter)
    {
        return 0;
    }

    if (sc_link_sem_ready == 0U)
    {
        rt_sem_init(&sc_link_sem, "rplink", 0, RT_IPC_FLAG_FIFO);
        sc_link_sem_ready = 1U;
    }

    (void)memset(isr_table, 0, sizeof(isr_table));

    return platform_init();
}

int32_t env_deinit(void)
{
    RL_ASSERT(env_init_counter > 0);
    if (env_init_counter <= 0)
    {
        return -1;
    }
    env_init_counter--;
    if (0 < env_init_counter)
    {
        return 0;
    }

    return platform_deinit();
}

/* --- memory ------------------------------------------------------------- */
void *env_allocate_memory(uint32_t size)
{
    return rt_malloc((rt_size_t)size);
}

void env_free_memory(void *ptr)
{
    if (ptr != ((void *)0))
    {
        rt_free(ptr);
    }
}

/* --- libc helpers ------------------------------------------------------- */
void env_memset(void *ptr, int32_t value, uint32_t size)
{
    (void)memset(ptr, value, size);
}

void env_memcpy(void *dst, void const *src, uint32_t len)
{
    (void)memcpy(dst, src, len);
}

int32_t env_strcmp(const char *dst, const char *src)
{
    return (strcmp(dst, src));
}

void env_strncpy(char *dest, const char *src, uint32_t len)
{
    (void)strncpy(dest, src, len);
}

int32_t env_strncmp(char *dest, const char *src, uint32_t len)
{
    return (strncmp(dest, src, len));
}

/* --- memory barriers ---------------------------------------------------- */
void env_mb(void)
{
    MEM_BARRIER();
}

void env_rmb(void)
{
    MEM_BARRIER();
}

void env_wmb(void)
{
    MEM_BARRIER();
}

/* --- address translation (flat on this part) ---------------------------- */
uint32_t env_map_vatopa(void *address)
{
    return platform_vatopa(address);
}

void *env_map_patova(uint32_t address)
{
    return platform_patova(address);
}

uint64_t env_get_timestamp(void)
{
    return (uint64_t)rt_tick_get();
}

/* --- locks -------------------------------------------------------------- */
#if defined(RL_USE_STATIC_API) && (RL_USE_STATIC_API == 1)
int32_t env_create_mutex(void **lock, int32_t count, void *context)
#else
int32_t env_create_mutex(void **lock, int32_t count)
#endif
{
    rt_mutex_t mutex;

    RT_UNUSED(count);
#if defined(RL_USE_STATIC_API) && (RL_USE_STATIC_API == 1)
    RT_UNUSED(context);
#endif

    mutex = rt_mutex_create("rpmsg", RT_IPC_FLAG_PRIO);
    if (mutex == RT_NULL)
    {
        return -1;
    }
    *lock = (void *)mutex;

    return 0;
}

void env_delete_mutex(void *lock)
{
    if (lock != ((void *)0))
    {
        rt_mutex_delete((rt_mutex_t)lock);
    }
}

void env_lock_mutex(void *lock)
{
    if (lock == ((void *)0))
    {
        return;
    }
    if (rt_interrupt_get_nest() != 0U)
    {
        return;                 /* never block inside an ISR - see file header */
    }
    (void)rt_mutex_take((rt_mutex_t)lock, RT_WAITING_FOREVER);
}

void env_unlock_mutex(void *lock)
{
    if (lock == ((void *)0))
    {
        return;
    }
    if (rt_interrupt_get_nest() != 0U)
    {
        return;
    }
    (void)rt_mutex_release((rt_mutex_t)lock);
}

/* --- sleep / link-up ---------------------------------------------------- */
void env_sleep_msec(uint32_t num_msec)
{
    rt_thread_mdelay((rt_int32_t)num_msec);
}

void env_wait_for_link_up(volatile uint32_t *link_state, uint32_t link_id)
{
    RT_UNUSED(link_id);

    /* Event driven instead of the bare-metal busy spin: the A7 sets the link
     * up by kicking the mailbox, and the RX callback releases the semaphore. */
    while (*link_state != 1U)
    {
        (void)rt_sem_take(&sc_link_sem, rt_tick_from_millisecond(20));
    }
}

void env_tx_callback(uint32_t link_id)
{
    RT_UNUSED(link_id);

    if (sc_link_sem_ready != 0U)
    {
        (void)rt_sem_release(&sc_link_sem);
    }
}

/* --- ISR plumbing ------------------------------------------------------- */
void env_register_isr(uint32_t vector_id, void *data)
{
    RL_ASSERT(vector_id < SC_RPMSG_ISR_COUNT);
    if (vector_id < SC_RPMSG_ISR_COUNT)
    {
        isr_table[vector_id].data = data;
    }
}

void env_unregister_isr(uint32_t vector_id)
{
    RL_ASSERT(vector_id < SC_RPMSG_ISR_COUNT);
    if (vector_id < SC_RPMSG_ISR_COUNT)
    {
        isr_table[vector_id].data = ((void *)0);
    }
}

void env_enable_interrupt(uint32_t vector_id)
{
    (void)platform_interrupt_enable(vector_id);
}

void env_disable_interrupt(uint32_t vector_id)
{
    (void)platform_interrupt_disable(vector_id);
}

void env_map_memory(uint32_t pa, uint32_t va, uint32_t size, uint32_t flags)
{
    platform_map_mem_region(va, pa, size, flags);
}

void env_disable_cache(void)
{
    platform_cache_all_flush_invalidate();
    platform_cache_disable();
}

/* Called from the mailbox ISR (via the platform layer). */
void env_isr(uint32_t vector)
{
    struct isr_info *info;

    RL_ASSERT(vector < SC_RPMSG_ISR_COUNT);
    if (vector < SC_RPMSG_ISR_COUNT)
    {
        info = &isr_table[vector];
        virtqueue_notification((struct virtqueue *)info->data);
    }
}
