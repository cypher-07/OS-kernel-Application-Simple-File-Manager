[BITS 32]

section .asm

global _start

_start:
    ; Test 0: sys_print (command 1) 
    push msg_banner
    mov  eax, 1
    int  0x80
    add  esp, 4

    ; Test 1: sys_open  (command 2)

    push 0               ; flags = 0 (FILE_MODE_READ)
    push path_hello      ; pointer to "0:/hello.txt"
    mov  eax, 2          ; SYSTEM_COMMAND2_OPEN
    int  0x80
    add  esp, 8

    test eax, eax
    jz   .open_failed

    mov  [fd_store], eax ; save the returned file descriptor

    push msg_open_ok
    mov  eax, 1
    int  0x80
    add  esp, 4

    ; Test 2: sys_read  (command 3)  -- first read returns data

    push 63
    push read_buf
    push dword [fd_store]
    mov  eax, 3          ; SYSTEM_COMMAND3_READ
    int  0x80
    add  esp, 12

    test eax, eax
    jle  .read_failed

    push msg_read_ok
    mov  eax, 1
    int  0x80
    add  esp, 4

    ; print the bytes that were copied into read_buf
    push read_buf
    mov  eax, 1
    int  0x80
    add  esp, 4

    push msg_newline
    mov  eax, 1
    int  0x80
    add  esp, 4

    jmp  .done

.open_failed:
    push msg_open_fail
    mov  eax, 1
    int  0x80
    add  esp, 4
    jmp  .done

.read_failed:
    push msg_read_fail
    mov  eax, 1
    int  0x80
    add  esp, 4
    jmp  .done


.done:
    push msg_done
    mov  eax, 1
    int  0x80
    add  esp, 4
    jmp  $                  ; halt loop

section .data
path_hello:    db '0:/hello.txt', 0
msg_banner:    db 'sys_open/sys_read user-space tests', 0x0A, 0
msg_open_ok:   db 'sys_open returned valid fd', 0x0A, 0
msg_read_ok:   db 'sys_read returned data: ', 0
msg_open_fail: db 'sys_open returned 0', 0x0A, 0
msg_read_fail: db 'ys_read returned <=0', 0x0A, 0
msg_done:      db 'All user-space tests done', 0x0A, 0
msg_newline:   db 0x0A, 0
fd_store:      dd 0          ; 4 bytes: holds fd returned by sys_open
read_buf:      times 64 db 0 ; destination buffer for sys_read
eof_buf:       times 4  db 0 ; scratch buffer for the EOF-check read
