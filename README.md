# CAOS_project
This project was developed during the Computer Architectures and Operation Systems of the Master's degree in Cybersecurity at Politecnico di Torino.

The target board is the NXP S32K3X8EVB, that is bases on a the 32-bit Arm®Cortex®-M7 S32K358 MCU. According to the documentation, arm cortex-m7 it's based on a armv7-m architecture. This means that we can use the already implemented cpu defined in the file `hw/arm/armv7m.c` (so no need of following the second and following chapters of the [Florian Göhler's guide](https://fgoehler.com/blog/adding-a-new-architecture-to-qemu-01/).

Useful links and documentation (docs folder):
- [Tutorial](https://fgoehler.com/blog/adding-a-new-architecture-to-qemu-01/)
- [Board documentation](https://www.nxp.com/design/design-center/development-boards-and-designs/automotive-development-platforms/s32k-mcu-platforms/s32k3x8evb-q289-evaluation-board-for-automotive-general-purpose:S32K3X8EVB-Q289)
- [arm cortex m7 documentation](https://developer.arm.com/documentation/#q=cortex%20m7&cf-navigationhierarchiesproducts=%20IP%20Products,Processors,Cortex-M,Cortex-M7&numberOfResults=48): especially the user guide
- [Adding a custom ARM platform to Qemu](http://souktha.github.io/software/qemu-port/)
- [A deep dive into QEMU](https://airbus-seclab.github.io/qemu_blog/)
- [qemu documentation](https://qemu.weilnetz.de/doc/6.0/devel/index.html)
- [arm v7 peripherals](https://developer.arm.com/documentation/dui0646/c/Cortex-M7-Peripherals?lang=en) (timers addresses)
- [arm plt uart](https://krinkinmu.github.io/2020/11/29/PL011.html)
- S32K3xx.pdf: is the datasheet of the s32k358 microcontroller
- S32K3XXRM.pdf: contains the whole manual of s32k358 (5000 pages 😱)
- AN13388.pdf: S32K3 Memories Guide
- DDI0403E_e_armv7m_arm.pdf: Armv7-M Architecture Reference Manual
- DDI0183G_uart_pl011_arm.pdf: PrimeCell UART (PL011)
- DDI0489D_cortex_m7_trm.pdf: ARM Cortex -M7 Processor Technical reference manual
- Arm-Cortex-M7-Processor-Datasheet.pdf: Arm Cortex-M7 Processor Datasheet
- .xlsx: files attached to the manual containing the interrupts, memory mappings,...

In the source folder there is the file implementing the board (even though you can avoid copying it in the directory directly aplying the git patch, see below for more details) + vexpress (the one the "Adding a custom ARM platform to Qemu" link talks about); The #include are commented.

## Preparing the environment
First of all, you need to install git and some other dependencies. For the list of the necessary dependencies look at the [official Qemu documentation](https://wiki.qemu.org/Hosts/Linux). Then we get the code from the Qemu's GitHub repository, checkout to a stable branch and check if the build configuration works correctly:
```
    git clone https://github.com/qemu/qemu.git
    cd qemu
    git checkout stable-9.1
    git submodule init
    git submodule update --recursive
    ./configure
```
Now we need to create and give the necessary permissions to the folder in which the qemu binary file containing the s32k358 board we'll be installed:
```
sudo mkdir /opt/qemu-5.2.0
sudo chown debian:debian /opt/qemu-5.2.0/
```

## Adding the board NXP S32K3X8EVB
To add an unsupported board in Qemu we need to create the corresponding file in `Qemu/hw/arm`.
The S32K3X8EVB-Q289 board is based on the 32-bit Arm®Cortex®-M7 S32K358 MCU (see [here](https://www.nxp.com/design/design-center/development-boards-and-designs/S32K3X8EVB-Q289) for more details). So let's have a look to a similar board already implemented. In particular the board ARM V2M MPS2 is similarly based on arm-cortex-m7. Its file can be found in `Qemu/hw/arm/mps2.c`.
You need to copy the qemu.patch file inside your qemu folder. Then, you can simply apply the git-patch with:
```
cd qemu
git apply < qemu.patch
```

Otherwise, you can put the s32k358 file in the directory `hw/arm/`and then follow the paths explained in [this tutorial](http://souktha.github.io/software/qemu-port/)

## Recompile Qemu
After having added the support for the new board, we need to recompile Qemu:
```
./configure --target-list=arm-softmmu,arm-linux-user,aarch64-softmmu,aarch64-linux-user --prefix=/opt/qemu-5.2.0/
make
make install
```

## Test the application
Now we can test FreeRTOS on our custom machine, using the application in `Demo`: simply try to run `make` and `make qemu_start`. This application creates three timers that use timers.

**N.B. Remember to put the correct directory for your FreeRTOS folder in the Makefile**

## What's next
We still need to write the documentation 😢.