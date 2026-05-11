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
  idt/          Interrupt descriptor table + IRQ handlers
                  int20h_handler  — preemptive timer (round-robin scheduler)
                  int21h_handler  — PS/2 keyboard (IRQ 1)
                  isr80h_handler  — INT 0x80 syscall dispatcher
  gdt/          Global descriptor table + TSS
  memory/       Heap allocator and paging (paging_new_4gb, paging_copy_4gb)
  disk/         ATA PIO disk driver + byte-granular streamer
                  disk_read_block / disk_write_block
                  diskstreamer_read / diskstreamer_write
  fs/fat/       FAT16 filesystem driver
                  Read:   fopen("r"), fread, fstat, fseek, fclose
                  Write:  fopen("w"), fwrite  — cluster alloc, FAT flush
                  Delete: fdelete            — cluster free, dir entry 0xE5
                  Dir:    freaddir           — filename by directory index
  keyboard/     PS/2 keyboard driver (IRQ 1, per-process circular buffer)
  task/         Round-robin scheduler + process_fork (page-directory clone)
                  task_get_next  — picks next task in linked list
                  task_return    — restores register context via iretd
                  task_switch    — swaps page directory on context switch
  isr80h/       INT 0x80 syscall handlers
                  io.c      — sys_print, sys_open, sys_read, sys_write,
                               sys_delete, sys_close, sys_readdir
                  process.c — sys_fork, sys_exit, sys_readline, sys_wait
programs/
  shell/        Interactive shell (x86 ASM flat binary)
                  Commands: ls, cat <file>, write <file>, del <file>, exit
  blank/        Minimal stub process
bin/            Build output
build/          Intermediate object files
```

---

## Syscall interface (INT 0x80)

Arguments are pushed right-to-left onto the stack; command number in `EAX`.
Return value comes back in `EAX` after `iretd`.

| Cmd | Name          | Signature                     | Status   |
|-----|---------------|-------------------------------|----------|
| 1   | sys_print     | print(msg_ptr)                | Done     |
| 2   | sys_open      | open(path_ptr, mode)          | Done     |
| 3   | sys_read      | read(fd, buf, count)          | Done     |
| 4   | sys_fork      | fork()                        | Done     |
| 5   | sys_exit      | exit()                        | Done     |
| 6   | sys_readline  | readline(buf, max)            | Done     |
| 7   | sys_write     | write(fd, buf, count)         | Done     |
| 8   | sys_delete    | delete(path_ptr)              | Done     |
| 9   | sys_close     | close(fd)                     | Done     |
| 10  | sys_readdir   | readdir(index, buf, size)     | Done     |
| 11  | sys_wait      | wait(pid)                     | Done     |

---

## Interrupt architecture

Three interrupt sources are active in the kernel, all registered in the IDT:

| Vector | Source       | Handler          | Purpose                                  |
|--------|--------------|------------------|------------------------------------------|
| 0x20   | IRQ0 — timer | `int20h_handler` | Preemptive round-robin task switching    |
| 0x21   | IRQ1 — kbd   | `int21h_handler` | Scan code → ASCII → per-process buffer   |
| 0x80   | Software     | `isr80h_wrapper` | User-space syscall gate (DPL=3)          |

---

## Implementation status

| Component                                              | Status   |
|--------------------------------------------------------|----------|
| Base kernel (boot, GDT, IDT, TSS, heap, paging)       | Complete |
| FAT16 read                                             | Complete |
| FAT16 write + cluster allocation                       | Complete |
| FAT16 delete + cluster chain free                      | Complete |
| FAT16 directory listing (freaddir)                     | Complete |
| ATA PIO disk write (`disk_write_block`)                | Complete |
| Byte-granular disk write (`diskstreamer_write`)        | Complete |
| VFS `fwrite` / `fdelete`                               | Complete |
| Keyboard driver (IRQ 1, circular buffer)               | Complete |
| Preemptive timer (IRQ 0, round-robin scheduler)        | Complete |
| INT 0x80 dispatcher                                    | Complete |
| sys_print / sys_open / sys_read                        | Complete |
| sys_fork / sys_exit / sys_readline                     | Complete |
| sys_write / sys_delete / sys_close                     | Complete |
| sys_readdir / sys_wait                                 | Complete |
| Shell — ls (real FAT16 directory listing)              | Complete |
| Shell — cat \<filename\>                               | Complete |
| Shell — write \<filename\>                             | Complete |
| Shell — del \<filename\>                               | Complete |
| Shell — fork() per command + sys_wait                  | Complete |
| Shell — exit / quit                                    | Complete |
