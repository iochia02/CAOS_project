/*
 * ARM s32k358 board emulation.
 *
 * SPDX-License-Identifier: CC-BY-NC-4.0
 * Copyright (c) 2025 Braidotti Sara, Iorio Chiara, Pani Matteo.
 *
 */

#include "qemu/osdep.h" // Pull in some common system headers that most code in QEMU will want
#include "qemu/units.h" // Simply defines KiB, MiB
#include "qemu/cutils.h" // Defines some function about strings
#include "qapi/error.h" // Error reporting system loosely patterned after Glib's GError.
#include "qemu/error-report.h" // error reporting
#include "hw/arm/boot.h" // ARM kernel loader.
#include "hw/arm/armv7m.h" // ARMv7M CPU object
#include "hw/boards.h" //  Declarations for use by board files for creating devices (e.g machine_type_name)
#include "exec/address-spaces.h" //  Internal memory management interfaces
#include "sysemu/sysemu.h" // Misc. things related to the system emulator.
#include "hw/qdev-properties.h" // Define device properties
#include "hw/misc/unimp.h" // create_unimplemented_device
#include "hw/qdev-clock.h" // Device's clock input and output
#include "qapi/qmp/qlist.h" // QList Module (provides dynamic arrays)
#include "qom/object.h" // QEMU Object Model
#include "hw/char/s32k358_uart.h" // LPUART s32k358
#include "hw/timer/s32k358_timer.h" // PIT s32k358

// Data types representing the machine
struct S32K358MachineClass {
    MachineClass parent;
};

struct S32K358MachineState {
    MachineState parent;
    ARMv7MState armv7m; // CPU
    MemoryRegion utest;
    MemoryRegion cflash0; // Code flash
    MemoryRegion cflash1;
    MemoryRegion cflash2;
    MemoryRegion cflash3;
    MemoryRegion dflash0; // Data flash
    MemoryRegion itcm0; // memory located in 0x0, here we put the interrupt vector table
    MemoryRegion dtcm0;
    MemoryRegion sram0; // RAM
    MemoryRegion sram1;
    MemoryRegion sram2;
    S32K358Timer timer[3];
    Clock *sysclk; // Clock
    Clock *refclk;
};

#define TYPE_S32K358_MACHINE MACHINE_TYPE_NAME("s32k358")

OBJECT_DECLARE_TYPE(S32K358MachineState, S32K358MachineClass, S32K358_MACHINE)


/* Main SYSCLK frequency in Hz */
/* Accordimg to the documentation, it can run up to 240 MHz */
/* We found this value in the offical freeRTOS demo for s32k3x8 */
#define SYSCLK_FRQ 24000000

/*
 * The Application Notes don't say anything about how the
 * systick reference clock is configured. (Quite possibly
 * they don't have one at all.) This 1MHz clock matches the
 * pre-existing behaviour that used to be hardcoded in the
 * armv7m_systick implementation.
 */
#define REFCLK_FRQ (1 * 1000 * 1000)

/* Initialize the auxiliary RAM region @mr and map it into
 * the memory map at @base.
 */
static void make_ram(MemoryRegion *system_memory, MemoryRegion *mr,
                     const char *name, hwaddr base, hwaddr size)
{
    memory_region_init_ram(mr, NULL, name, size, &error_fatal);
    memory_region_add_subregion(system_memory, base, mr);
}

static void s32k358_init(MachineState *machine)
{
    S32K358MachineState *mms = S32K358_MACHINE(machine);
    MemoryRegion *system_memory = get_system_memory();
    DeviceState *armv7m;
    int i;

    /* This clock doesn't need migration because it is fixed-frequency */
    mms->sysclk = clock_new(OBJECT(machine), "SYSCLK");
    clock_set_hz(mms->sysclk, SYSCLK_FRQ);

    mms->refclk = clock_new(OBJECT(machine), "REFCLK");
    clock_set_hz(mms->refclk, REFCLK_FRQ);

    /*
     * Memory regions on the system
     * We need to define the base address and the size of each memory space
     * Refer to pages 3 (Flash memories) and 13 (RAM memories) of the S32K3 Memories Guide
     * Create the memory region, add it to the memory regions of the system and finally link it to the CPU
    */

    make_ram(system_memory, &mms->itcm0, "s32k358.itcm0", 0x00000000, 0x10000);
    make_ram(system_memory, &mms->cflash0, "s32k358.cflash0", 0x00400000, 0x200000);
    make_ram(system_memory, &mms->cflash1, "s32k358.cflash1", 0x00600000, 0x200000);
    make_ram(system_memory, &mms->cflash2, "s32k358.cflash2", 0x00800000, 0x200000);
    make_ram(system_memory, &mms->cflash3, "s32k358.cflash3", 0x00A00000, 0x200000);
    make_ram(system_memory, &mms->dflash0, "s32k358.dflash0", 0x10000000, 0x20000);
    make_ram(system_memory, &mms->dtcm0, "s32k358.dtcm0", 0x20000000, 0x20000);
    make_ram(system_memory, &mms->utest, "s32k358.utest", 0x1B000000, 0x2000);
    make_ram(system_memory, &mms->sram0, "s32k358.sram0", 0x20400000, 0x40000);
    make_ram(system_memory, &mms->sram1, "s32k358.sram1", 0x20440000, 0x40000);
    make_ram(system_memory, &mms->sram2, "s32k358.sram2", 0x20480000, 0x40000);

    // CPU: arm-cortex-m7
    object_initialize_child(OBJECT(mms), "armv7m", &mms->armv7m, TYPE_ARMV7M);
    armv7m = DEVICE(&mms->armv7m);
    // Number of interrupts (see interrupt map)
    qdev_prop_set_uint32(armv7m, "num-irq", 240);

    qdev_connect_clock_in(armv7m, "cpuclk", mms->sysclk);
    qdev_connect_clock_in(armv7m, "refclk", mms->refclk);
    qdev_prop_set_string(armv7m, "cpu-type", machine->cpu_type);
    qdev_prop_set_bit(armv7m, "enable-bitband", true);
    object_property_set_link(OBJECT(&mms->armv7m), "memory",
                             OBJECT(system_memory), &error_abort);
    sysbus_realize(SYS_BUS_DEVICE(&mms->armv7m), &error_fatal);

    // UART
    static const hwaddr uartbase[] = {0x40328000, 0x4032C000, 0x40330000, 0x40334000,
                                        0x40338000, 0x4033C000, 0x40340000, 0x40344000,
                                        0x4048C000, 0x40490000, 0x40494000, 0x40498000,
                                        0x4049C000, 0x404A0000, 0x404A4000, 0X404A8000
                                        };
    static const int uartirq_base = 141;


    for (i = 0; i < 16; i++) {
        DeviceState *dev;
        SysBusDevice *s;

        dev = qdev_new(TYPE_S32K358_LPUART);
        s = SYS_BUS_DEVICE(dev);
        qdev_prop_set_chr(dev, "chardev", serial_hd(i));
        qdev_prop_set_uint32(dev, "pclk-frq", SYSCLK_FRQ);
        qdev_prop_set_uint32(dev, "id", i);
        sysbus_realize_and_unref(s, &error_fatal);
        sysbus_mmio_map(s, 0, uartbase[i]);
        sysbus_connect_irq(s, 0, qdev_get_gpio_in(armv7m, uartirq_base + i));
    }

    // Timers - refer to page 2816 of the manual (68.7.1)
    static const hwaddr timerbase[] = {0x400B0000, 0x400B4000, 0x402FC000};
    // irq from the s32kxxrm interrupt map
    int irqno[] = {96, 97, 98};

    // Create the timers and connect them
    for (i = 0; i < ARRAY_SIZE(mms->timer); i++) {
        g_autofree char *name = g_strdup_printf("timer%d", i);
        SysBusDevice *sbd;

        object_initialize_child(OBJECT(mms), name, &mms->timer[i],
                                TYPE_S32K358_TIMER);
        sbd = SYS_BUS_DEVICE(&mms->timer[i]);
        qdev_connect_clock_in(DEVICE(&mms->timer[i]), "pclk", mms->sysclk);
        sysbus_realize_and_unref(sbd, &error_fatal);
        sysbus_mmio_map(sbd, 0, timerbase[i]);
        sysbus_connect_irq(sbd, 0, qdev_get_gpio_in(armv7m, irqno[i]));
    }

    // Address from which load the kernel
    // The address specified here is usually not used
    // (only if it's not specified in the elf file)
    armv7m_load_kernel(ARM_CPU(first_cpu), machine->kernel_filename,
                       0x00400000, 0x200000);
}

// Machine init
static void s32k358_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->init = s32k358_init;
    mc->max_cpus = 1;
    mc->default_cpu_type = ARM_CPU_TYPE_NAME("cortex-m7");
    mc->desc = "ARM S32K358";
}

static const TypeInfo s32k358_info = {
    .name = TYPE_S32K358_MACHINE,
    .parent = TYPE_MACHINE,
    .instance_size = sizeof(S32K358MachineState),
    .class_size = sizeof(S32K358MachineClass),
    .class_init = s32k358_class_init,
};

static void s32k358_machine_init(void)
{
    type_register_static(&s32k358_info);
}

type_init(s32k358_machine_init);
