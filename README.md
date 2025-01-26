# CAOS project: QEMU emulation of NXP S32K3X8EVB
This project was developed by Chiara Iorio, Sara Braidotti and Matteo Pani during the Computer Architectures and Operation Systems course of the Master's degree in Cybersecurity at Politecnico di Torino.

The goal of this project is to emulate the NXP S32K3X8EVB board, based on the 32-bit Arm®Cortex®-M7 S32K358 MCU ([NXP website](https://www.nxp.com/design/design-center/development-boards-and-designs/S32K3X8EVB-Q289)).

To generate a custom QEMU version to emulate this board (its CPU, memory map, UART and timers) please refer to the following steps.

To read the whole documentation of the project refer [click here](documentation.md). The references and the description of the files in the `docs`folder can be found [here](references.md).

## Preparing the environment
First of all, you need to install git and some other dependencies. For the list of the necessary dependencies look at the [official Qemu documentation](https://wiki.qemu.org/Hosts/Linux). Then we get the code from the QEMU's GitHub repository, checkout to a stable branch and check if the build configuration works correctly:

```shell
git clone https://github.com/qemu/qemu.git
cd qemu
git checkout stable-9.1
git submodule init
git submodule update --recursive
./configure
```
Afterwards, we create and give the necessary permissions to the folder in which the QEMU binary file containing the new board will be installed:

```shell
sudo install --directory --owner $(id --user) --group $(id --group) /opt/qemu-9.1.0
```

## Add the board NXP S32K3X8EVB
The easiest way to add the support for the board in QEMU is by applying the patch. Copy the file `qemu.patch` in the QEMU folder and apply it:

```shell
cd qemu
git apply < qemu.patch
```

Otherwise, you can manually add the board by following the subsequent steps. The source files can be found in the `src`folder.

### S32K358 MCU
1. Go to directory `qemu/hw/arm`
2. Copy the file `s32k358.c`
3. At the end of the `Kconfig` file add the code necessary to tell the peripherals needed by the board:
```
config S32K358
    bool
    default y
    depends on TCG && ARM
    select ARMSSE
    select UNIMP
    select S32K358_TIMER
    select S32K358_UART
```
4. At the end of the `meson.build` file (that coordinates the configuration and build of all executables) add:
```
arm_ss.add(when: 'CONFIG_S32K358', if_true: files('s32k358.c'))
```

### S32K358 LPUART
1. Go to directory `qemu/hw/char`
2. Copy the file `s32k358_uart.c`
3. At the end of the `Kconfig` file add:
```
config S32K358_UART
    bool
```
4. At the end of the `meson.build` file add:
```
specific_ss.add(when: 'CONFIG_S32K358_UART', if_true: files('s32k358_uart.c'))
```
5. Go to `qemu/include/hw/char/` and copy the file `s32k358_uart.h`

### S32K358 PIT TIMERS
1. Go to directory `qemu/hw/timer`
2. Copy the file `s32k358_timer.c`
3. At the end of the `Kconfig` file add:
```
config S32K358_TIMER
    bool
    select PTIMER
```
4. At the end of the `meson.build` file add:
```
specific_ss.add(when: 'CONFIG_S32K358_TIMER', if_true: files('s32k358_timer.c'))
```
5. Go to `qemu/include/hw/timer/` and copy the file `s32k358_timer.h`

## Recompile QEMU
After adding the support for the new board, we need to recompile QEMU:
```shell
./configure \
    --target-list=arm-softmmu,arm-linux-user \
    --prefix=/opt/qemu-9.1.0/
make
make install
```

## Test the application
Now we can test FreeRTOS on our custom machine. Therefore, the first step is to clone the FreeRTOS repository using the following command:
```shell
git clone https://github.com/FreeRTOS/FreeRTOS.git --recurse-submodules
```
At this point, we can try to run the demo application (`Demo` folder). Compile the code with the NXP toolchain by typing `make` and then `make qemu_start` to launch the application. Try to type something and press enter to experience the LPUART receive functionality!

**Remember to put the correct directory for your FreeRTOS folder in the Makefile.**

## Copyright
This project is distributed under the CC-BY_NC 4.0 - copyright (c) 2025 Braidotti Sara, Iorio Chiara, Pani Matteo.

You can view additional details on [this page](https://creativecommons.org/licenses/by-nc/4.0/).