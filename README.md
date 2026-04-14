# 570 Final Project — Simple File Manager

## Prerequisites

- `i686-elf` cross-compiler toolchain (GCC + binutils) installed at `$HOME/opt/cross`
- `nasm` assembler
- `qemu-system-i386` (to run the OS)
- `sudo` access (the build mounts a FAT16 disk image to copy files into it)

### Installing the cross-compiler

The toolchain must target `i686-elf` and be placed at `$HOME/opt/cross/bin/`. Follow the [OSDev cross-compiler guide](https://wiki.osdev.org/GCC_Cross-Compiler) or your course instructions to build it.

## Building

```bash
./build.sh
```

This sets up the cross-compiler path and runs `make all`, which:

1. Assembles the bootloader → `bin/boot.bin`
2. Compiles and links the kernel → `bin/kernel.bin`
3. Builds user programs (`programs/blank`, `programs/shell`)
4. Creates a FAT16 disk image `bin/os.bin` and copies `hello.txt`, `blank.bin`, and `shell.bin` into it

To build manually without the script:

```bash
export PREFIX="$HOME/opt/cross"
export TARGET=i686-elf
export PATH="$PREFIX/bin:$PATH"
make all
```

## Running

Boot the OS image in QEMU:

```bash
qemu-system-i386 -hda ./bin/os.bin
```

## Cleaning

```bash
make clean
```

## Project Structure

```
src/          Kernel source (C + ASM)
  boot/       Stage-1 bootloader
  kernel.c    Kernel entry point
  idt/        Interrupt descriptor table
  gdt/        Global descriptor table
  memory/     Heap allocator and paging
  disk/       ATA disk driver and streamer
  fs/fat/     FAT16 filesystem driver
  keyboard/   PS/2 keyboard driver
  task/       Process/task scheduler and TSS
  isr80h/     Syscall handlers (int 0x80)
programs/     User-space programs
  blank/      Minimal stub process
  shell/      Interactive shell
bin/          Build output (bootloader, kernel, disk image)
build/        Intermediate object files
```
