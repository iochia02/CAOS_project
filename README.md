# CAOS_project
This project was developed during the Computer Architectures and Operation Systems of the Master's degree in Cybersecurity at Politecnico di Torino.
Useful links:
- [Tutorial](https://fgoehler.com/blog/adding-a-new-architecture-to-qemu-01/)
- [Board documentation](https://www.nxp.com/design/design-center/development-boards-and-designs/automotive-development-platforms/s32k-mcu-platforms/s32k3x8evb-q289-evaluation-board-for-automotive-general-purpose:S32K3X8EVB-Q289)

## Preparing the environment
First of all, you need to install git and some other dependencies. For the list of the necessary dependencies look at the [official Qemu doumentation](https://wiki.qemu.org/Hosts/Linux). Then we get the code from the Qemu's GitHub repository and check if the build configuration works correctly:
```
    git clone https://github.com/qemu/qemu.git
    cd qemu
    ./configure
```
If the configuration succeeds, then you can go to the next step.
Then checkout to a stable branch:
```
git checkout stable-9.1
git submodule init
git submodule update --recursive
```
## Adding the board NXP S32K3X8EVB
To add an unsupported board in Qemu we need to create the corresponding file in Qemu/hw.
The S32K3X8EVB-Q289 board is based on the 32-bit Arm®Cortex®-M7 S32K3 MCU. So let's have a look to a similar board already implemented. In particular the board ARM V2M MPS2 is similarly based on arm-cortex-m7. Its file can be found at Qemu/hw/mps2.c.

## Recompile Qemu
After having added the support for the new board, we need to recompile Qemu:
```
mkdir build
cd build
../configure --target-list=our-arch
make
```