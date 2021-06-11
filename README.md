# hhuOS

<p align="center">
  <img src="media/logo/logo_v3.svg">
</p>

<p align="center">
  <a href="https://travis-ci.com/hhuOS/hhuOS"><img src="https://api.travis-ci.com/hhuOS/hhuOS.svg?branch=refactor/minimize"></a>
  <img src="https://img.shields.io/badge/C%2B%2B-17-blue.svg">
  <img src="https://img.shields.io/badge/license-GPLv3-orange.svg">
</p>

## Introduction

Welcome to hhuOS, a **small operating system** written in C++ and Assembler for the x86-architecture. The main purpose of this project is to show how different aspects of operating systems theory can be implemented and linked together. The system is *not* aimed to be a full-featured operating system for daily use.

Check out our [website](https://hhuos.github.io)!

## Compiling

GCC 7 and CMake 3.7 or newer versions of GCC and CMake are required to compile hhuOS.  
Before the kernel can be compiled, some packages must be installed. To install them, you can run the following command (on Ubuntu 18.04):

```sh
sudo apt install cmake make nasm gcc-multilib g++-multilib grub-pc-bin grub-efi-ia32-bin mtools xorriso
```

## Usage

To test hhuOS quickly in QEMU, you can issue the following commands.

```sh
git clone https://github.com/hhuOS/hhuOS.git
cd hhuOS/
./build.sh
./run.sh
```

## Bootloader

hhuOS implements the Multiboot standard and can (in theory) be booted by any Multiboot compliant bootloader. Per default, our own bootloader [towboot](https://github.com/hhuOS/towboot) is used for booting hhuOS. However, it is possible to boot hhuOS using GRUB. Since towboot only support UEFI-based system, it is necessary to use GRUB for booting hhuOS on (older) BIOS-based systems. To use GRUB, some additional packages need to be installed:

```sh
sudo apt install grub-pc-bin grub-efi-ia32-bin xorriso
```

Furthermore, it is necessary to pass the `--target` parameter to the build script, indicating that GRUB should be used when constructing the bootable image:

```sh
./build.sh --target grub
```

This will result a `.iso` file (instead of the `.img` file, when using towboot), which needs to be passed to the run script:

```sh
./run.sh --file hhuOS.iso
```

## BIOS/UEFI support

hhuOS is able to boot on BIOS- as well as UEFI-based systems. To test both configurations, the run script provides an option to choose between BIOS and UEFI.

### UEFI

We use the [EDK2 OVMF](https://github.com/tianocore/edk2/tree/master/OvmfPkg) image to test hhuOS. The run script expects this image to be located at `/usr/share/ovmf/ia32/OVMF.fd`, but this can be adapted by setting the `OVMF` environment variable.

### BIOS

Since towboot only supports UEFI-based systems, GRUB is needed to test hhuOS on BIOS-based systems (see [Bootloader](#bootloader)).

To run hhuOS with GRUB on a BIOS-based machine, use:

```sh
./run.sh --file hhuOS.iso --bios bios
```

## Kernel parameters

hhuOS can be configured via kernel parameters, that are passed to the system by the bootloader. The following parameters are available:

- `bios` can be set to `true` to activate support for BIOS-calls. This enables support for *VESA* and *CGA* graphics modes. CAUTION: Enabling this option will cause hhuOS to not boot on most UEFI-based systems.
- `lfb_provider` is used to set the framebuffer-implementation, that should be used to draw on the screen. The default implementation is named `Kernel::Multiboot::MultibootLinearFrameBufferProvider` and should always work (as long as the bootloader finds a video mode). However, the resolution stays fixes with this configuration. If BIOS-calls are activated, `Device::Graphic::VesaBiosExtensions` may be used to enable support for multiple resolutions and resolution switching.
- `text_provider` is used to set the implementation for drawing text on the screen. The default implementation is named `Device::Graphic::LinearFrameBufferTerminalProvider` and uses rasterized fonts to draw on an underlying linear framebuffer. It works with `lfb_provider` being set to `Kernel::Multiboot::MultibootLinearFrameBufferProvider` or `Device::Graphic::VesaBiosExtensions`. As an alternative, it may be set to `Device::Graphic::ColorGraphicsArrayProvider` (with activated BIOS-calls), to make use of *CGA* text modes, providing a higher drawing speed, but lower resolutions and only 16 colors.
