# 570 Final Project — Simple File Manager Kernel

CYSE 570 | Group 11: Huy Than, Faisal Ahmed

A 32-bit x86 OS kernel extended with a FAT16 filesystem driver, POSIX-style
system calls, a fork-based process model, and an interactive user-space shell.

---

## Prerequisites

- `i686-elf` cross-compiler toolchain (GCC + binutils) at `$HOME/opt/cross`
- `nasm` assembler
- `qemu-system-i386`
- `sudo` access (the build mounts a FAT16 disk image)

### Installing the cross-compiler

Build a `i686-elf` toolchain at `$HOME/opt/cross/bin/` following the
[OSDev cross-compiler guide](https://wiki.osdev.org/GCC_Cross-Compiler).

---

## Building

```bash
./build.sh
```

This sets up the cross-compiler path and runs `make all`, which:

1. Assembles the bootloader → `bin/boot.bin`
2. Compiles and links the kernel → `bin/kernel.bin`
3. Builds user programs (`programs/blank`, `programs/shell`)
4. Creates a 16 MB FAT16 disk image `bin/os.bin` and copies `hello.txt`,
   `blank.bin`, and `shell.bin` into it

To build manually:

```bash
export PREFIX="$HOME/opt/cross"
export TARGET=i686-elf
export PATH="$PREFIX/bin:$PATH"
make all
```

---

## Running

```bash
qemu-system-i386 -hda ./bin/os.bin
```

The kernel boots, runs a FAT16 self-test (`fopen` / `fread` on `hello.txt`),
then launches the interactive shell.

---

## Cleaning

```bash
make clean
```

---

## Project structure

```
src/
  boot/         Stage-1 bootloader
  kernel.c      Kernel entry point
  idt/          Interrupt descriptor table
  gdt/          Global descriptor table
  memory/       Heap allocator and paging (paging_new_4gb, paging_copy_4gb)
  disk/         ATA PIO disk driver + byte-granular streamer
                  disk_read_block / disk_write_block
                  diskstreamer_read / diskstreamer_write
  fs/fat/       FAT16 filesystem driver
                  Read:   fopen("r"), fread, fstat, fseek, fclose
                  Write:  fopen("w"), fwrite  — cluster alloc, FAT flush
                  Delete: fdelete            — cluster free, dir entry 0xE5
  keyboard/     PS/2 keyboard driver (IRQ 1, per-process circular buffer)
  task/         Round-robin scheduler + process_fork (page-directory clone)
  isr80h/       INT 0x80 syscall handlers
programs/
  shell/        Interactive shell (x86 ASM flat binary)
  blank/        Minimal stub process
bin/            Build output
build/          Intermediate object files
```

---

## Syscall interface (INT 0x80)

Arguments are pushed right-to-left; command number in EAX.

| Cmd | Name        | Signature                  | Status   |
|-----|-------------|----------------------------|----------|
| 1   | sys_print   | print(msg_ptr)             | done     |
| 2   | sys_open    | open(path_ptr, mode)       | done     |
| 3   | sys_read    | read(fd, buf, count)       | done     |
| 4   | sys_fork    | fork()                     | done     |
| 5   | sys_exit    | exit()                     | done     |
| 6   | sys_readline| readline(buf, max)         | done     |
| 7   | sys_write   | write(fd, buf, count)      | **TODO** |
| 8   | sys_delete  | delete(path_ptr)           | **TODO** |

---

## Implementation status

| Component | Status |
|-----------|--------|
| Base kernel (boot, GDT, IDT, TSS, heap, paging) | Complete |
| FAT16 read | Complete |
| FAT16 write + cluster allocation | **Complete** |
| FAT16 delete + cluster chain free | **Complete** |
| ATA PIO disk write (`disk_write_block`) | **Complete** |
| Byte-granular disk write (`diskstreamer_write`) | **Complete** |
| VFS `fwrite` / `fdelete` | **Complete** |
| Keyboard driver (IRQ 1) | Complete |
| INT 0x80 dispatcher | Complete |
| sys_open / sys_read / sys_fork / sys_exit / sys_readline | Complete |
| sys_write (cmd 7) | TODO |
| sys_delete (cmd 8) | TODO |
| Shell `write` / `del` commands | TODO |
| Shell `ls` from real FAT16 directory | TODO |
| Shell `cat <filename>` argument parsing | TODO |
| Shell fork() per command | TODO |
