[BITS 32]

section .asm

global _start

%define SYS_PRINT    1
%define SYS_OPEN     2
%define SYS_READ     3
%define SYS_FORK     4
%define SYS_EXIT     5
%define SYS_READLINE 6
%define SYS_WRITE    7
%define SYS_DELETE   8
%define SYS_CLOSE    9
%define SYS_READDIR  10
%define SYS_WAIT     11

%define FILE_MODE_READ  0
%define FILE_MODE_WRITE 1

_start:
    push msg_welcome
    mov  eax, SYS_PRINT
    int  0x80
    add  esp, 4

.prompt_loop:
    push 64
    push cmd_buf
    call memzero
    add  esp, 8

    push msg_prompt
    mov  eax, SYS_PRINT
    int  0x80
    add  esp, 4

.read_again:
    push 63
    push cmd_buf
    mov  eax, SYS_READLINE
    int  0x80
    add  esp, 8

    test eax, eax
    jle  .read_again

    ; Fork before dispatch
    mov  eax, SYS_FORK
    int  0x80

    test eax, eax
    js   .prompt_loop   ; fork failed — skip, re-prompt
    jnz  .parent_wait   ; parent — wait for child

    ; === CHILD (eax == 0): dispatch then exit ===

    ; Dispatch: ls
    push cmd_ls
    push cmd_buf
    call str_startswith
    add  esp, 8
    test eax, eax
    jnz  .do_ls

    ; Dispatch: cat
    push cmd_cat
    push cmd_buf
    call str_startswith
    add  esp, 8
    test eax, eax
    jnz  .do_cat

    ; Dispatch: write
    push cmd_write
    push cmd_buf
    call str_startswith
    add  esp, 8
    test eax, eax
    jnz  .do_write

    ; Dispatch: del
    push cmd_del
    push cmd_buf
    call str_startswith
    add  esp, 8
    test eax, eax
    jnz  .do_del

    push msg_unknown
    mov  eax, SYS_PRINT
    int  0x80
    add  esp, 4
    mov  eax, SYS_EXIT
    int  0x80

.parent_wait:
    mov  [child_pid], eax

.wait_loop:
    push dword [child_pid]
    mov  eax, SYS_WAIT
    int  0x80
    add  esp, 4
    test eax, eax
    jnz  .wait_loop
    jmp  .prompt_loop

; ── ls — real FAT16 directory listing ───────────────────────────────────
.do_ls:
    push msg_ls_header
    mov  eax, SYS_PRINT
    int  0x80
    add  esp, 4

    mov  dword [ls_index], 0

.ls_loop:
    ; Zero read_buf before each call
    push 64
    push read_buf
    call memzero
    add  esp, 8

    ; sys_readdir(index, read_buf, 64)
    push 64
    push read_buf
    push dword [ls_index]
    mov  eax, SYS_READDIR
    int  0x80
    add  esp, 12

    ; eax = 1 if entry returned, 0 if end of dir, <0 on error
    cmp  eax, 1
    jne  .ls_done

    ; Print "  " prefix
    push msg_ls_indent
    mov  eax, SYS_PRINT
    int  0x80
    add  esp, 4

    ; Print the filename
    push read_buf
    mov  eax, SYS_PRINT
    int  0x80
    add  esp, 4

    push msg_newline
    mov  eax, SYS_PRINT
    int  0x80
    add  esp, 4

    inc  dword [ls_index]
    jmp  .ls_loop

.ls_done:
    mov  eax, SYS_EXIT
    int  0x80

; ── cat <filename> ──────────────────────────────────────────────────────
; cmd_buf layout: "cat <filename>\n\0"  (4 chars before filename)
.do_cat:
    lea  eax, [cmd_buf+4]
    push eax
    call build_path
    add  esp, 4

    push FILE_MODE_READ
    push path_buf
    mov  eax, SYS_OPEN
    int  0x80
    add  esp, 8

    test eax, eax
    jz   .cat_fail

    mov  [fd_store], eax

    push 63
    push read_buf
    push dword [fd_store]
    mov  eax, SYS_READ
    int  0x80
    add  esp, 12

    test eax, eax
    jle  .cat_fail

    push read_buf
    mov  eax, SYS_PRINT
    int  0x80
    add  esp, 4

    push msg_newline
    mov  eax, SYS_PRINT
    int  0x80
    add  esp, 4

    push dword [fd_store]
    mov  eax, SYS_CLOSE
    int  0x80
    add  esp, 4

    mov  eax, SYS_EXIT
    int  0x80

.cat_fail:
    push msg_err_read
    mov  eax, SYS_PRINT
    int  0x80
    add  esp, 4
    mov  eax, SYS_EXIT
    int  0x80

; ── write <filename> ────────────────────────────────────────────────────
; cmd_buf layout: "write <filename>\n\0"  (6 chars before filename)
.do_write:
    lea  eax, [cmd_buf+6]
    push eax
    call build_path
    add  esp, 4

    push msg_write_prompt
    mov  eax, SYS_PRINT
    int  0x80
    add  esp, 4

    push 512
    push write_buf
    call memzero
    add  esp, 8

    push 511
    push write_buf
    mov  eax, SYS_READLINE
    int  0x80
    add  esp, 8

    mov  [write_count], eax

    push FILE_MODE_WRITE
    push path_buf
    mov  eax, SYS_OPEN
    int  0x80
    add  esp, 8

    test eax, eax
    jz   .write_fail

    mov  [fd_store], eax

    push dword [write_count]
    push write_buf
    push dword [fd_store]
    mov  eax, SYS_WRITE
    int  0x80
    add  esp, 12

    mov  [write_result], eax

    push dword [fd_store]
    mov  eax, SYS_CLOSE
    int  0x80
    add  esp, 4

    cmp  dword [write_result], 0
    jl   .write_fail

    ; Print "File written: <path_buf>\n"
    push msg_write_ok
    mov  eax, SYS_PRINT
    int  0x80
    add  esp, 4

    push path_buf
    mov  eax, SYS_PRINT
    int  0x80
    add  esp, 4

    push msg_newline
    mov  eax, SYS_PRINT
    int  0x80
    add  esp, 4

    mov  eax, SYS_EXIT
    int  0x80

.write_fail:
    push msg_err_write
    mov  eax, SYS_PRINT
    int  0x80
    add  esp, 4
    mov  eax, SYS_EXIT
    int  0x80

; ── del <filename> ──────────────────────────────────────────────────────
; cmd_buf layout: "del <filename>\n\0"  (4 chars before filename)
.do_del:
    lea  eax, [cmd_buf+4]
    push eax
    call build_path
    add  esp, 4

    push path_buf
    mov  eax, SYS_DELETE
    int  0x80
    add  esp, 4

    test eax, eax
    jl   .del_fail

    push msg_del_ok
    mov  eax, SYS_PRINT
    int  0x80
    add  esp, 4

    push path_buf
    mov  eax, SYS_PRINT
    int  0x80
    add  esp, 4

    push msg_newline
    mov  eax, SYS_PRINT
    int  0x80
    add  esp, 4

    mov  eax, SYS_EXIT
    int  0x80

.del_fail:
    push msg_err_del
    mov  eax, SYS_PRINT
    int  0x80
    add  esp, 4
    mov  eax, SYS_EXIT
    int  0x80


; ── build_path(src_ptr) → path_buf = "0:/" + src, newline stripped ──────
build_path:
    push ebp
    mov  ebp, esp
    push esi
    push edi
    push ecx

    mov  edi, path_buf
    mov  byte [edi+0], '0'
    mov  byte [edi+1], ':'
    mov  byte [edi+2], '/'
    add  edi, 3

    mov  esi, [ebp+8]
    mov  ecx, 124
.bp_loop:
    mov  al, [esi]
    test al, al
    jz   .bp_end
    cmp  al, 0x0A
    je   .bp_end
    cmp  al, 0x0D
    je   .bp_end
    mov  [edi], al
    inc  esi
    inc  edi
    dec  ecx
    jnz  .bp_loop
.bp_end:
    mov  byte [edi], 0

    pop  ecx
    pop  edi
    pop  esi
    pop  ebp
    ret


; ── str_startswith(buf, prefix) → eax=1 if match ────────────────────────
str_startswith:
    push ebp
    mov  ebp, esp
    push esi
    push edi
    mov  esi, [ebp+8]
    mov  edi, [ebp+12]
.sw_loop:
    mov  al, [edi]
    test al, al
    jz   .sw_match
    mov  bl, [esi]
    cmp  al, bl
    jne  .sw_no
    inc  esi
    inc  edi
    jmp  .sw_loop
.sw_match:
    mov  eax, 1
    jmp  .sw_done
.sw_no:
    mov  eax, 0
.sw_done:
    pop  edi
    pop  esi
    pop  ebp
    ret


; ── memzero(buf, len) ────────────────────────────────────────────────────
memzero:
    push ebp
    mov  ebp, esp
    push edi
    mov  edi, [ebp+8]
    mov  ecx, [ebp+12]
    xor  eax, eax
    rep  stosb
    pop  edi
    pop  ebp
    ret


section .data
msg_welcome:      db 'CYSEOS Shell v0.1', 0x0A,
                  db 'Commands: ls  cat <file>  write <file>  del <file>', 0x0A, 0
msg_prompt:       db '> ', 0
msg_unknown:      db 'Unknown command. Try: ls  cat <file>  write <file>  del <file>', 0x0A, 0
msg_ls_header:    db 'Directory of 0:/', 0x0A, 0
msg_ls_indent:    db '  ', 0
msg_cat_header:   db 'cat 0:/hello.txt:', 0x0A, 0
msg_newline:      db 0x0A, 0
msg_err_read:     db 'Error: could not read', 0x0A, 0
msg_write_prompt: db 'Enter content: ', 0
msg_write_ok:     db 'File written: ', 0
msg_del_ok:       db 'Deleted: ', 0
msg_err_write:    db 'Error: write failed', 0x0A, 0
msg_err_del:      db 'Error: delete failed', 0x0A, 0

cmd_ls:           db 'ls', 0
cmd_cat:          db 'cat ', 0
cmd_write:        db 'write ', 0
cmd_del:          db 'del ', 0
path_hello:       db '0:/hello.txt', 0

section .bss
cmd_buf:      resb 64
read_buf:     resb 64
fd_store:     resd 1
ls_index:     resd 1
path_buf:     resb 128
write_buf:    resb 512
write_count:  resd 1
write_result: resd 1
child_pid:    resd 1
