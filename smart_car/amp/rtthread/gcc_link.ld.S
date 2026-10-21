/*
 * Linker script for the RK3506 "BUS M0" running RT-Thread (AMP co-processor).
 * ============================================================================
 * Memory map (identical to the vendor bare-metal layout this firmware
 * replaces, so nothing on the U-Boot / Linux side has to change):
 *
 *   M0 view          A7 view            size     contents
 *   ---------------- ----------------   ------   ---------------------------
 *   0x00000000       0xfff84000         32 KiB   the whole firmware: .vectors,
 *                                                .text, .rodata, .data, .bss,
 *                                                heap, MSP stack
 *   0x03c00000       0x03c00000          2 MiB   rpmsg-lite shared memory
 *                                                (vrings; NOLOAD, untouched by
 *                                                the loader, in DDR)
 *   0x03e0639c       0x03e0639c          16 B    liveness probe block, inside
 *                                                the DTS `amp@3e00000`
 *                                                reserved region so the A7 can
 *                                                read it with devmem
 *
 * U-Boot maps "M0 address 0" onto the value of `load` in the FIT image
 * (arch/arm/mach-rockchip/rk3506/rk3506.c -> sip_smc_mcu_config(),
 *  0xfff84000 for sram / 0x03e00000 for ddr), which is exactly why the 32 KiB
 * window lives at 0x0 here.
 */

MEMORY
{
    RAM(rxw)        : ORIGIN = 0x00000000, LENGTH = 0x8000
    LINUX_RPMSG(rw) : ORIGIN = 0x03c00000, LENGTH = 0x00200000
}

ENTRY(Reset_Handler)

__STACK_SIZE = 0x400;                       /* MSP: exception + boot stack */

SECTIONS
{
    /* ------------------------------------------------------------------ */
    .text :
    {
        KEEP(*(.vectors))
        _stext = .;

        . = ALIGN(4);
        KEEP(*(.init))
        KEEP(*(.fini))

        *(.text)                        /* remaining code */
        *(.text.*)
        *(.rodata)
        *(.rodata*)
        *(.glue_7)
        *(.glue_7t)
        *(.gnu.linkonce.t*)

        . = ALIGN(4);
        /* RT-Thread: rt_components_init() table (unused here, kept so the
         * section is not orphaned if components get enabled later) */
        PROVIDE_HIDDEN(__rt_init_start = .);
        KEEP(*(SORT(.rti_fn*)))
        PROVIDE_HIDDEN(__rt_init_end = .);

        . = ALIGN(4);
        __fsymtab_start = .;
        KEEP(*(FSymTab))
        __fsymtab_end = .;
        . = ALIGN(4);
        __vsymtab_start = .;
        KEEP(*(VSymTab))
        __vsymtab_end = .;

        . = ALIGN(4);
        _etext = .;
    } > RAM

    .ARM.extab :
    {
        *(.ARM.extab* .gnu.linkonce.armextab.*)
    } > RAM

    __exidx_start = .;
    .ARM.exidx :
    {
        *(.ARM.exidx* .gnu.linkonce.armexidx.*)
    } > RAM
    __exidx_end = .;

    /* the rpmsg vrings are in DDR: reserve the address, load nothing */
    .linux_share_rpmsg (NOLOAD) :
    {
        PROVIDE(__linux_share_rpmsg_start__ = .);
        . += LENGTH(LINUX_RPMSG);
        PROVIDE(__linux_share_rpmsg_end__ = .);
    } > LINUX_RPMSG

    .zero.table :
    {
        . = ALIGN(4);
        __zero_table_start__ = .;
        LONG (__bss_start__)
        LONG (__bss_end__ - __bss_start__)
        __zero_table_end__ = .;
    } > RAM

    .copy.table :
    {
        . = ALIGN(4);
        __copy_table_start__ = .;
        __copy_table_end__ = .;
    } > RAM

    . = ALIGN(4);

    .data :
    {
        PROVIDE(__data_start__ = .);
        *(vtable)
        *(.data)
        *(.data.*)
        *(.sdata)
        *(.sdata.*)

        . = ALIGN(4);
        /* preinit data */
        PROVIDE_HIDDEN(__preinit_array_start = .);
        KEEP(*(.preinit_array))
        PROVIDE_HIDDEN(__preinit_array_end = .);

        . = ALIGN(4);
        /* init data */
        PROVIDE_HIDDEN(__init_array_start = .);
        KEEP(*(SORT(.init_array.*)))
        KEEP(*(.init_array))
        PROVIDE_HIDDEN(__init_array_end = .);

        . = ALIGN(4);
        /* finit data */
        PROVIDE_HIDDEN(__fini_array_start = .);
        KEEP(*(SORT(.fini_array.*)))
        KEEP(*(.fini_array))
        PROVIDE_HIDDEN(__fini_array_end = .);

        KEEP(*(.jcr*))
        . = ALIGN(16);
        PROVIDE(__data_end__ = .);
    } > RAM

    . = ALIGN(16);

    .ctors :
    {
        PROVIDE(__ctors_start__ = .);
        KEEP(*(SORT(.ctors.*)))
        KEEP(*(.ctors))
        PROVIDE(__ctors_end__ = .);
    } > RAM

    .dtors :
    {
        PROVIDE(__dtors_start__ = .);
        KEEP(*(SORT(.dtors.*)))
        KEEP(*(.dtors))
        PROVIDE(__dtors_end__ = .);
    } > RAM

    . = ALIGN(16);

    .bss :
    {
        PROVIDE(__bss_start__ = .);
        *(.bss)
        *(.bss.*)
        *(.dynbss)
        *(COMMON)
        . = ALIGN(16);
        PROVIDE(__bss_end__ = .);
    } > RAM
    _end = .;
    PROVIDE(end = .);

    /*
     * NOTE: the A7-visible liveness probe (magic 0x414D5042 / step / served
     * counter) is NOT a section here.  It is reached through a fixed absolute
     * pointer to 0x03e0639c (see struct sc_probe_block in applications/sc_app.h)
     * inside the DTS `amp@3e00000` no-map reservation.  Nothing in this script
     * can ever be placed there - RAM ends at 0x8000 and the rpmsg region ends
     * at 0x03e00000 - so the address stays stable across builds, which is what
     * the A7-side devmem check depends on.
     */

    . = ALIGN(16);
    __STACK_START = ORIGIN(RAM) + LENGTH(RAM) - __STACK_SIZE;

    /*
     * Hard guarantee: the heap is whatever is left between .bss and the MSP
     * stack, and this firmware will not link if that is under 1.25 KiB.
     *
     * Measured need - the only consumers of the heap are:
     *   struct rpmsg_lite_instance   40 B   (rpmsg_lite_remote_init)
     *   struct rpmsg_lite_endpoint   16 B   (rpmsg_lite_create_ept)
     *   struct llist                 16 B   (rpmsg_lite_create_ept)
     *   2x rt_mutex                  ~120 B (platform lock + instance lock)
     *   the "main" thread            ~620 B (512 B stack + TCB; released as
     *                                        soon as main() returns, i.e.
     *                                        right after boot)
     * i.e. a peak of roughly 820 B and a steady state of about 200 B, so
     * 1.25 KiB still leaves ~1.5x headroom over the peak.  A silently
     * auto-shrinking heap on a part this small is exactly the failure that
     * only shows up on the bench, so the floor is checked by the linker
     * instead.
     *
     * The floor was 1536 B until the re-announce loop was added: this part has
     * 32768 B in total and image + heap + stack already sat exactly on that
     * number, so no new code could be linked at all.  The loop costs 128 B of
     * RAM (image 30192 -> 30320 B), which the floor absorbs.  The runtime
     * figure to watch is the "[sc] heap ... used" line on the M0 console.
     */
    ASSERT(__STACK_START - . >= 1280,
           "M0 image too large: fewer than 1.25 KiB left for the rpmsg-lite heap (32 KiB RAM total)")

    __HEAP_SIZE = (__STACK_START - .);

    .heap :
    {
        PROVIDE(__heap_start = .);
        . += __HEAP_SIZE;
        PROVIDE(__heap_end = .);
        PROVIDE(__HeapLimit = .);
    } > RAM

    /*
     * MSP: Reset_Handler does `ldr r3, =__StackTop ; msr msp, r3`, and vector
     * word 0 is __StackTop, so __StackTop MUST be the HIGH address.
     *
     * This is deliberately different from the vendor gcc_bus_m0.ld, where
     * __StackTop is assigned at the *start* of the .stack section - i.e. the
     * initial MSP points at the bottom of the reserved region and the very
     * first exception frame lands in the top of the heap.  The bare-metal
     * firmware got away with it because its MSP usage was a few hundred bytes
     * and its bump allocator filled the heap from the bottom up; with
     * RT-Thread every SysTick (8 kHz) and mailbox interrupt uses MSP, so the
     * region has to be a real, non-overlapping one.
     */
    .stack (NOLOAD) :
    {
        . = ALIGN(16);
        __StackLimit = .;
        . += __STACK_SIZE;
        __StackTop = .;
        __stack = .;
    } > RAM
}
