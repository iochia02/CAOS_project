---
title: QEMU emulation of NXP S32K3X8EVB
author: Chiara Iorio, Sara Braidotti, Matteo Pani
patat:
    slideNumber: true
    wrap: true
    margins:
        left: 10
        right: 10
    theme:
        emph: [vividGreen]
        strong: [bold, vividMagenta]
        codeBlock: [onRgb##36454F]
        code: [onRgb##36454F]
        imageTarget: [onDullWhite, vividRed]
    incrementalLists: true
    transition:
        type: dissolve
        duration: 0.5
    images:
        backend: iterm2
...

# The goal
The project is composed by two main phases:

1. Generate a custom *QEMU* version to support the **NXP S32K3X8EVB board**:
    - CPU
    - memory map
    - peripherals
        + LPUART
        + timers

2. Make *FreeRTOS* run on the emulated board.

---

# NXP S32K3X8EVB board
It is based on the **32-bit Arm®Cortex®-M7 S32K358 MCU**.

## S32K358 MCU
The microcontroller includes:

- an Arm®Cortex®-M7 core;
- the following memories:

| Type   | Name            | Size   | Start address |
| ------ | --------------- | ------ | ------------- |
| Flash  | Code Flash 0    | 2 MB   | 0x00400000    |
| Flash  | Code Flash 1    | 2 MB   | 0x00600000    |
| Flash  | Code Flash 2    | 2 MB   | 0x00800000    |
| Flash  | Code Flash 3    | 2 MB   | 0x00A00000    |
| Flash  | Data Flash      | 128 KB | 0x10000000    |
|        |                 |        |               |
| Ram    | SRAM 0          | 256 KB | 0x20400000    |
| Ram    | SRAM 1          | 256 KB | 0x20440000    |
| Ram    | SRAM 2          | 256 KB | 0x20480000    |
| Ram    | **ITCM0**       | 64 KB  | **0x00000000**|
| Ram    | ITCM2           | 64 KB  | 0x00000000    |
| Ram    | DTCM0           | 128 KB | 0x20000000    |
| Ram    | DTCM2           | 128 KB | 0x20000000    |


When the microcontroller is powered on:

1. it reads the interrupt vector table stored in *ITCM0*, located at 0x0: it executes the reset;
2. it loads the kernel from the address written in the reset handler (*NOR flash* memory).

---

# The S32K358 MCU in QEMU (1)
- QEMU is an **emulator**;

- QEMU is based on the **QOM**:
    + each device is represented in an *object-oriented* way;
    + classes are represented as *structs*:
        - structs deriving from ObjectClass are DeviceClass and have per-class information:
        ```C
        struct S32K358MachineClass {
            MachineClass parent;
        };
        ```
        - structs deriving from Object are DeviceState and contain per-instance information:
        ```C
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
        ```

---

# The S32K358 MCU in QEMU (2)
When creating a new device, we need to:

- create the relative *data types*:
```C
#define TYPE_S32K358_MACHINE MACHINE_TYPE_NAME("s32k358")

OBJECT_DECLARE_TYPE(S32K358MachineState, S32K358MachineClass, S32K358_MACHINE)
```
- create a static instance of *TypeInfo* for the type to tell QOM how to create the object:
```C
static const TypeInfo s32k358_info = {
    .name = TYPE_S32K358_MACHINE,
    .parent = TYPE_MACHINE,
    .instance_size = sizeof(S32K358MachineState),
    .class_size = sizeof(S32K358MachineClass),
    .class_init = s32k358_class_init,
};
```
- *register* it:
```C
static void s32k358_machine_init(void)
{
    type_register_static(&s32k358_info);
}
```
- call the *init* function, that creates new instances of the objects.

---

# The S32K358 MCU in QEMU (3)
We have two different init functions:

- the `s32k358_class_init` initializes the *class*, setting its properties.

- the `s32k358_init` initializes the *objects*:
    * creates the *CPU* object (`armv7m`);
    * creates and connects the *clock*;
    * creates the *memory regions*, adds them to the memory regions of the system and links them to the CPU;
    * creates and connects the *peripherals*:
        + peripherals are attached to the specific memory bus;
        + peripherals generate the appropriate interrupt to notify HW events, using the correct IRQ lines;
        + peripheral registers are accessible through the memory bus.
        ```C
        // Creation of a timer instance

        // Memory bus
        SysBusDevice *sbd;

        // Create a new instance of timer object
        object_initialize_child(OBJECT(mms), name, &mms->timer[i], TYPE_S32K358_TIMER);

        // Connect it to the bus, the clock and the irq
        sbd = SYS_BUS_DEVICE(&mms->timer[i]);
        qdev_connect_clock_in(DEVICE(&mms->timer[i]), "pclk", mms->sysclk);
        sysbus_realize_and_unref(sbd, &error_fatal);
        sysbus_mmio_map(sbd, 0, timerbase[i]);
        sysbus_connect_irq(sbd, 0, qdev_get_gpio_in(armv7m, irqno[i]));

        ```

---

# Low Power Universal Asynchronous Receiver/Transmitter (LPUART)
The board contains **sixteen instances** of LPUART, providing asynchronous, serial communication capabilities with external devices.

```
                                  ┌─────────┐                                     ┌─────────┐
                                  │         │                                     │         │
000000004048c000-000000004048c7ff │ LPUART8 ├─────────────────┐   ┌───────────────┤ LPUART0 │ 0000000040328000-00000000403287ff
                          IRQ:149 │         │                 │   │               │         │ IRQ:141
                                  └─────────┘                 │   │               └─────────┘
                                  ┌─────────┐                 │   │               ┌─────────┐
                                  │         │                 │   │               │         │
0000000040490000-00000000404907ff │ LPUART9 ├───────────────┐ │   │ ┌─────────────┤ LPUART1 │ 000000004032c000-000000004032c7ff
                          IRQ:150 │         │               │ │   │ │             │         │ IRQ:142
                                  └─────────┘               │ │   │ │             └─────────┘
                                  ┌─────────┐               │ │   │ │             ┌─────────┐
                                  │         │               │ │   │ │             │         │
0000000040494000-00000000404947ff │ LPUART10├─────────────┐ │ │   │ │ ┌───────────┤ LPUART2 │ 0000000040330000-00000000403307ff
                          IRQ:151 │         │             │ │ │   │ │ │           │         │ IRQ:143
                                  └─────────┘             │ │ │   │ │ │           └─────────┘
                                  ┌─────────┐             │ │ │   │ │ │           ┌─────────┐
                                  │         │             │ │ │   │ │ │           │         │
0000000040498000-00000000404987ff │ LPUART11├──────┐      │ │ │   │ │ │     ┌─────┤ LPUART3 │ 0000000040334000-00000000403347ff
                          IRQ:152 │         │      │     ┌┴─┴─┴───┴─┴─┴┐    │     │         │ IRQ:144
                                  └─────────┘      └─────┤             ├────┘     └─────────┘
                                  ┌─────────┐            │  cortex-M7  │          ┌─────────┐
                                  │         │      ┌─────┤             ├────┐     │         │
000000004049c000-000000004049c7ff │ LPUART12├──────┘     └┬─┬─┬───┬─┬─┬┘    └─────┤ LPUART4 │ 0000000040338000-00000000403387ff
                          IRQ:153 │         │             │ │ │   │ │ │           │         │ IRQ:145
                                  └─────────┘             │ │ │   │ │ │           └─────────┘
                                  ┌─────────┐             │ │ │   │ │ │           ┌─────────┐
                                  │         │             │ │ │   │ │ │           │         │
00000000404a0000-00000000404a07ff │ LPUART13├─────────────┘ │ │   │ │ └───────────┤ LPUART5 │ 000000004033c000-000000004033c7ff
                          IRQ:154 │         │               │ │   │ │             │         │ IRQ:146
                                  └─────────┘               │ │   │ │             └─────────┘
                                  ┌─────────┐               │ │   │ │             ┌─────────┐
                                  │         │               │ │   │ │             │         │
00000000404a4000-00000000404a47ff │ LPUART14├───────────────┘ │   │ └─────────────┤ LPUART6 │ 0000000040340000-00000000403407ff
                          IRQ:155 │         │                 │   │               │         │ IRQ:147
                                  └─────────┘                 │   │               └─────────┘
                                  ┌─────────┐                 │   │               ┌─────────┐
                                  │         │                 │   │               │         │
00000000404a8000-00000000404a87ff │ LPUART15├─────────────────┘   └───────────────┤ LPUART7 │ 0000000040344000-00000000403447ff
                          IRQ:156 │         │                                     │         │ IRQ:148
                                  └─────────┘                                     └─────────┘
```

---

# LPUART in QEMU (1)
The two main implemented functionalities are:

- **transmit** data from the frontend (e.g. FreeRTOS application) to the backend (the board).
- **receive** data from the backend (the board).

Moreover, it is possible to:

- enable/disable both transmitter and receiver;
- turn on/off the *FIFO functionality* in both directions; FIFO disabled = FIFO of 1 byte;
- configure the *baud rate*, the *parity bit* and the number of *stop bits*;
- configure the *watermark*;
- turn on/off the *interrupt* generation;
- *reset* all registers.

When a user's application tries to write or read the LPUART modelled registers, it triggers the execution of the `lpuart_write` or `lpuart_read` functions. The access in an unauthorized way to a register (e.g. try to write a read-only register) will trigger an error.

If necessary, after a write operation, the configuration of the LPUART is updated through `lpuart_update_parameters`.

---

# LPUART in QEMU (2) - Transmit
```
USER APPLICATION WRITES 1 BYTE
              │
              │
              ▼                 copy into                                                      transmit
  ┌───────────────────────┐                      ┌────────┐                ┌────────────────┐
  │  DATA REGISTER [0:7]  ├───────────────────►  │  FIFO  ├───────────────►│ SHIFT REGISTER ├─────────────►
  └───────────────────────┘                      └────────┘                └────────────────┘
                              if there is
                             space available   FIFO disabled =
                                               FIFO 1 BYTE
```

1. Data is written in the *DATA register* (`lpuart_write`).
2. Data is copied into the *FIFO*, if there is enough space (`lpuart_write_tx_fifo`):
    ```C
    // Check that the transmitter is enabled and there is space in the FIFO (otherwise set the overflow flag)
    // Set the transmitting flag

    // Write the data into the fifo
    memcpy(s->tx_fifo + s->tx_fifo_written, &msg, 1);
    s->tx_fifo_written += 1;
    // Update the irq and watermark
    ```
3. Try to *transmit* data to the backend (`lpuart_transmit`):
    ```C
    // Check that the backend is connected, the transmitter is enabled and the FIFO is not empty

    // Transmission from front-end to back-end
    ret = qemu_chr_fe_write(&s->chr, s->tx_fifo, s->tx_fifo_written);

    // Update the number of elements in the fifo and shift the fifo
    if (ret >= 0) {
        s->tx_fifo_written -= ret;
        memmove(s->tx_fifo, s->tx_fifo + ret, s->tx_fifo_written);
    }

    // If there are still elements in the fifo, try to retransmit
    // Otherwise set transmission completed
    // Update the irq and watermark
    ```

---

# LPUART in QEMU (3) - Receive
```
USER APPLICATION READS 1 BYTE
              ▲
              │
              │                 copy into                     copy into                         receive
  ┌───────────┴───────────┐                      ┌────────┐                ┌────────────────┐
  │  DATA REGISTER [0:7]  │◄─────────────────────┤  FIFO  │◄───────────────┤ SHIFT REGISTER │◄────────────
  └───────────────────────┘                      └────────┘                └────────────────┘

                                               FIFO disabled =
                                               FIFO 1 BYTE
```
1. Data is copied from the backend to the *receive FIFO* (`lpuart_receive`):

    ```C
    // Check that the receiver is enabled
    // Check how much space is left in the receive FIFO

    // Copy the buffer into the receive FIFO
    memcpy(s->rx_fifo + s->rx_fifo_written, buf, 1);
    s->rx_fifo_written++;
    // Set the receive fifo as no more empty
    // Update the irq and watermark
    ```

2. The user's application *tries to read* from the DATA register (`lpuart_read`), triggering the `lpuart_read_rx_fifo` function.
3. If available, data is copied from the receive FIFO to the *DATA register* (`lpuart_read_rx_fifo`):
    ```C
    // Check that the receive FIFO is not empty

    // Copy the first byte from the receive FIFO to the data register (to be read by the user application)
    s->data &= ~R_DATA_R07T07_MASK;
    s->data |= s->rx_fifo[0];
    s->rx_fifo_written--;

    // Update the receive FIFO
    // Update the irq and watermark
    ```

---

# Periodic Interrupt Timers (PIT)
This board has **three instances** of the PIT, composed of **4 channels** each.
```
                                   ┌──────┐                                 ┌──────┐
00000000402fc000-00000000402fc13f  │ PIT2 ├───────────┐       ┌─────────────┤ PIT0 │  00000000400b0000-00000000400b013f
                          IRQ: 98  └───┬──┘           │       │             └───┬──┘  IRQ: 96
                                       │              │       │                 │
                                       │              │       │                 │
                  ┌───────────┐        │              │       │                 │      ┌───────────┐
                  │ Channel 0 ├────────┤              │       │                 ├──────┤ Channel 0 │
                  └───────────┘        │              │       │                 │      └───────────┘
                  ┌───────────┐        │              │       │                 │      ┌───────────┐
                  │ Channel 1 ├────────┤            ┌─┴───────┴─┐               ├──────┤ Channel 1 │
                  └───────────┘        │            │           │               │      └───────────┘
                  ┌───────────┐        │            │ cortex-M7 │               │      ┌───────────┐
                  │ Channel 2 ├────────┤            │           │               ├──────┤ Channel 2 │
                  └───────────┘        │            └──────┬────┘               │      └───────────┘
                  ┌───────────┐        │                   │                    │      ┌───────────┐
                  │ Channel 3 ├────────┘                   │                    └──────┤ Channel 3 │
                  └───────────┘                            │                           └───────────┘
                                                           │
                                                           │
                                                       ┌───┴──┐
                                                       │ PIT1 │  00000000400b4000-00000000400b413f
                                                       └───┬──┘  IRQ: 97
                                                           │
                                                           │
                                                           │      ┌───────────┐
                                                           ├──────┤ Channel 0 │
                                                           │      └───────────┘
                                                           │      ┌───────────┐
                                                           ├──────┤ Channel 1 │
                                                           │      └───────────┘
                                                           │      ┌───────────┐
                                                           ├──────┤ Channel 2 │
                                                           │      └───────────┘
                                                           │      ┌───────────┐
                                                           └──────┤ Channel 3 │
                                                                  └───────────┘
```

---

# PIT in QEMU (1)
The main functionality of PIT is to **count down from the initial value to zero**.

When a channel expires, it may raise an interrupt. The four channels share the same IRQ.

Moreover, it is possible to:

- enable/disable the whole *PIT module*;
- enable/disable each single *PIT channel*;
- enable/disable the *interrupt* for each channel;
- set the *timeout period* in clock cycles for each channel;
- read the *current value* of the PIT channel.

When a user's application tries to write or read the PIT modelled registers, it triggers the execution of the `s32k358_timer_write` or `s32k358_timer_read` functions. The access in an unauthorized way to a register (e.g. try to write a read-only register) will trigger an error.

---

# PIT in QEMU (2) - How they work
1. Enable the *PIT module*, that will trigger the reset of the 4 channels.
2. Enable the *PIT channel*.
3. Set the *timeout* in the LOAD register.
4. The timer starts *counting down* from the value in the LOAD register to zero; the function `s32k358_timer_clk_update` models the timers update:

    ```C
    for (int i = 0; i < ARRAY_SIZE(s->timers); i++) {
        ptimer_transaction_begin(s->timers[i].timer);
        ptimer_set_period_from_clock(s->timers[i].timer, s->pclk, 1);
        ptimer_transaction_commit(s->timers[i].timer);
    }
    ```
5. When the timer expires, the *Timer Interrupt Flag* is set; if the interrupt is enabled, the *interrupt* is generated (`s32k358_timer_tick`):

    ```C
    // set interrupt flag (always)
    st->flag |= R_TFLG0_TIF_MASK;
    // if interrupts are enabled trigger interrupt
    if (st->ctrl & R_TCTRL0_TIE_MASK) {
        qemu_irq_raise(st->parent->timer_irq);
    }
    ```
6. The interrupt *remains set* until the flag is explicitly cleared.
7. The timer *reloads* with the load value and starts counting down again.

---

# PIT in QEMU (3) - Other characteristics
- The timer implementation is based on the *ptimer API*, that implements a simple periodic countdown timer.
- All calls to functions which modify ptimer state must be between *matched calls* to `ptimer_transaction_begin()` and `ptimer_transaction_commit()`.
- If the timer is running and the value in the LOAD register is changed, it will continue counting until it expires, *then it will reload* with the new value:

    ```C
    ptimer_transaction_begin(s->timers[idx].timer);

    // Do not reload immediately the timer, wait that it expires before loading the new value
    // Hence, change the reload value but not the CVAL one
    ptimer_set_limit(s->timers[idx].timer, value, 0);

    // Check if the pit and line of the timer are enabled
    // R_TCTRL0_TEN_MASK is the same for all the registers
    if (!(s->timer_ctrl & R_MCR_MDIS_MASK) && (s->timers[idx].ctrl & R_TCTRL0_TEN_MASK)) {
        // Make sure timer is running (it doesn't reset it if it was already)
        ptimer_run(s->timers[idx].timer, 0 /* reloadable timer */);
    }

    ptimer_transaction_commit(s->timers[idx].timer);

    ```

---

# Customized QEMU
```
                   ┌─────────────┐  add    ┌────────────────────────────────────────────────────────────────┐
qemu/hw/        ┌──┤ meson.build ├─────────┤ arm_ss.add(when: 'CONFIG_S32K358', if_true: files('s32k358.c'))│
                │  └─────────────┘         └────────────────────────────────────────────────────────────────┘
   │            │
   │            │                          ┌─────────────────────────────┐
   │   ./arm    │  ┌───────────┐           │  config S32K358             │
   ├────────────┼──┤ s32k358.c │           │      bool                   │
   │            │  └───────────┘           │      default y              │
   │            │  ┌─────────┐      add    │      depends on TCG && ARM  │
   │            └──│ Kconfig ├─────────────┤      select ARMSSE          │
   │               └─────────┘             │      select UNIMP           │
   │                                       │      select S32K358_TIMER   │
   │                                       │      select S32K358_UART    │
   │                                       └─────────────────────────────┘
   │   ./char
   │                ┌────────────────┐
   ├────────────┬───┤ s32k358_uart.c │
   │            │   └────────────────┘     ┌───────────────────────────┐
   │            │   ┌─────────┐       add  │  config S32K358_UART      │
   │            ├───┤ Kconfig ├────────────┤      bool                 │
   │            │   └─────────┘            └───────────────────────────┘
   │            │   ┌─────────────┐   add  ┌───────────────────────────────────────────────────────────────────────────────┐
   │            └───┤ meson.build ├────────┤ specific_ss.add(when:F'CONFIG_S32K358_UART',fif_true:2files('s32k358_uart.c'))│
   │                └─────────────┘        └───────────────────────────────────────────────────────────────────────────────┘
   │   ./timer
   │                ┌────────────────┐
   └────────────┬───┤ s32k358_timer.c│     ┌───────────────────────────┐
                │   └────────────────┘     │  config S32K358_TIMER     │
                │   ┌─────────┐       add  │      bool                 │
                ├───┤ Kconfig ├────────────┤      select PTIMER        │
                │   └─────────┘            └───────────────────────────┘
                │   ┌─────────────┐   add  ┌─────────────────────────────────────────────────────────────────────────────────┐
                └───┤ meson.build ├────────┤ specific_ss.add(when:F'CONFIG_S32K358_TIMER',fif_true:2files('s32k358_timer.c)) │
                    └─────────────┘        └─────────────────────────────────────────────────────────────────────────────────┘

qemu/include/hw/

   │    ./char      ┌────────────────┐
   ├────────────────┤ s32k358_uart.h │
   │                └────────────────┘
   │    ./timer     ┌────────────────┐
   └────────────────┤ s32k358_timer.h│
                    └────────────────┘
```


---

# FreeRTOS Demo application (1)
FreeRTOS is a class of **RTOS** that is designed to be small enough to run on a **microcontroller**.

To make the Demo application work, we need:

- the *Makefile*, in which we need to specify:
    + the target embedded board and CPU:
    ```makefile
    MACHINE := s32k358
    CPU := cortex-m7
    ```
    + the compiler toolchain for arm-cortex-m7:
    ```makefile
    KERNEL_PORT_DIR := $(FREERTOS_ROOT)/Source/portable/GCC/ARM_CM7/r0p1
    CC := arm-none-eabi-gcc
    LD := arm-none-eabi-gcc
    SIZE := arm-none-eabi-size
    ```
    + the binary file of QEMU built with our custom machine:
    ```makefile
    QEMU := /opt/qemu-9.1.0/bin/qemu-system-arm
    ```

- the *startup* file, that contains:
    + the vector table;
    + the exception handlers;
    + the reset handler.

- the *linker* script, in which we specify:
    + the memory regions defined in QEMU;
    + in which memory the different parts of the application go;
    + the stack top.

- the *nvic* file, necessary to enable/disable external interrupts.

- the *FreeRTOSConfig.h*, containing the application specific definitions.

---

# FreeRTOS Demo application (2) - main
In the **main** file four tasks are created:

- *TaskA:* when timer 0 channel 0 expires, the task prints the current value of the other two timers.
- *TaskB:* when timer 0 channel 1 expires, the task changes the period of timer 1 channel 0 (halves or doubles the value).
- *TaskC:* when timer 1 channel 0 expires, the task prints the number of times it expired.
- *TaskD:* when the user types a sentence followed by enter, the task prints what the user just wrote.

Tasks are unblocked by **semaphores**, given by the relative interrupt service routines.

---

# FreeRTOS Demo application (3) - LPUART
The **uart** file contains the definition of the struct representing the LPUART registers, that are then memory mapped at the corresponding address:

```C
typedef struct
{
	__O uint32_t VERID;
	__O uint32_t PARAM;
    __IO uint32_t GLOBAL;
    char UNIMPLEMENTED1[0x4];
    __IO uint32_t BAUD;
    __IO uint32_t STAT;
    __IO uint32_t CTRL;
    __IO uint32_t DATA;
    char UNIMPLEMENTED2[0x28 - 0x20];
    __IO uint32_t FIFO;
    __IO uint32_t WATER;
} S32K358_UART_Typedef;

#define UART_0_BASE_ADDRESS (0x40328000UL)
#define S32K358_UART0       ((S32K358_UART_Typedef  *) UART_0_BASE_ADDRESS  )
```

This file also contains the functions to:

- *initialize* the peripheral;
- handle the *interrupt* generated when the user types something;
- *retrieve* from the buffer what the user wrote;
- *print* a string.

---

# FreeRTOS Demo application (4) - PIT
The **timer** file contains the definition of the data structure to represent the PIT and their memory mapping. The structs used are:
```C
// Single Channel
typedef struct
{
	__IO uint32_t RELOAD;
	__O  uint32_t  VALUE;
	__IO uint32_t  CTRL;
	union {
		__I    uint32_t  INTSTATUS;
		__O    uint32_t  INTCLEAR;
	};
} S32K358_CHANNEL_TypeDef;

// PIT module
typedef struct
{
	__IO uint32_t PIT_CTRL;
	char UNIMPLEMENTED[0x100 - 0x4];
	S32K358_CHANNEL_TypeDef channels[4];
} S32K358_TIMER_TypeDef;
```

The implemented functions allow to:

- *initialize* the timer modules and channels;
- handle the generated *interrupts*;
- *change* the value of the RELOAD register;
- *get* the value in the RELOAD register and in the VALUE register.


---

```bash
 _____             _
(  _  )           ( )
| (_) |  ___     _| |     ___     _    _   _   _
|  _  |/' _ `\ /'_` |   /' _ `\ /'_`\ ( ) ( ) ( )
| | | || ( ) |( (_| |   | ( ) |( (_) )| \_/ \_/ | _  _  _
(_) (_)(_) (_)`\__,_)   (_) (_)`\___/'`\___x___/'(_)(_)(_)
```

. . .

```bash
 ___                                _                         _
(  _`\                             ( )_  _                   ( )
| | ) |   __    ___ ___     _      | ,_)(_)  ___ ___     __  | |
| | | ) /'__`\/' _ ` _ `\ /'_`\    | |  | |/' _ ` _ `\ /'__`\| |
| |_) |(  ___/| ( ) ( ) |( (_) )   | |_ | || ( ) ( ) |(  ___/| |
(____/'`\____)(_) (_) (_)`\___/'   `\__)(_)(_) (_) (_)`\____)(_)
                                                             (_)
```



---

![Output](./img/output.gif)

---

```bash
 _____  _                    _                 ___
(_   _)( )                  ( )              /'___)
  | |  | |__     _ _   ___  | |/')   ___    | (__   _    _ __
  | |  |  _ `\ /'_` )/' _ `\| , <  /',__)   | ,__)/'_`\ ( '__)
  | |  | | | |( (_| || ( ) || |\`\ \__, \   | |  ( (_) )| |
  (_)  (_) (_)`\__,_)(_) (_)(_) (_)(____/   (_)  `\___/'(_)


 _    _                       _    _                  _                     _
( )_ ( )                     ( )_ ( )_               ( )_  _               ( )
| ,_)| |__     __        _ _ | ,_)| ,_)   __    ___  | ,_)(_)   _     ___  | |
| |  |  _ `\ /'__`\    /'_` )| |  | |   /'__`\/' _ `\| |  | | /'_`\ /' _ `\| |
| |_ | | | |(  ___/   ( (_| || |_ | |_ (  ___/| ( ) || |_ | |( (_) )| ( ) || |
`\__)(_) (_)`\____)   `\__,_)`\__)`\__)`\____)(_) (_)`\__)(_)`\___/'(_) (_)(_)
                                                                           (_)
```

. . .


```bash
 _____                      _                            _
(  _  )                    ( )_  _                     /'_`\
| ( ) | _   _    __    ___ | ,_)(_)   _     ___    ___(_) ) |
| | | |( ) ( ) /'__`\/',__)| |  | | /'_`\ /' _ `\/',__)  /'/'
| (('\|| (_) |(  ___/\__, \| |_ | |( (_) )| ( ) |\__, \ |_|
(___\_)`\___/'`\____)(____/`\__)(_)`\___/'(_) (_)(____/ (_)


```