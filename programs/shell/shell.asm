[BITS 32]

section .asm

global _start

%define SYS_PRINT    1
%define SYS_OPEN     2
%define SYS_READ     3
%define SYS_READLINE 6

_start:
    push msg_welcome
    mov  eax, SYS_PRINT
    int  0x80
    add  esp, 4

.prompt_loop:
    ; Zero the command buffer
    push 64
    push cmd_buf
    call memzero
    add  esp, 8

    ; Print prompt
    push msg_prompt
    mov  eax, SYS_PRINT
    int  0x80
    add  esp, 4

.read_again:
    ; Non-blocking readline — spin until we get a full line
    push 63
    push cmd_buf
    mov  eax, SYS_READLINE
    int  0x80
    add  esp, 8

    test eax, eax
    jle  .read_again


    push cmd_ls
    push cmd_buf
    call str_startswith
    add  esp, 8
    test eax, eax
    jnz  .do_ls

    push cmd_cat
    push cmd_buf
    call str_startswith
    add  esp, 8
    test eax, eax
    jnz  .do_cat

    ; Unknown command
    push msg_unknown
    mov  eax, SYS_PRINT
    int  0x80
    add  esp, 4
    jmp  .prompt_loop

.do_ls:
    push msg_ls_header
    mov  eax, SYS_PRINT
    int  0x80
    add  esp, 4

    push msg_ls_files
    mov  eax, SYS_PRINT
    int  0x80
    add  esp, 4
    jmp  .prompt_loop

.ls_fail:
    push msg_err_open
    mov  eax, SYS_PRINT
    int  0x80
    add  esp, 4
    jmp  .prompt_loop

.do_cat:
    push msg_cat_header
    mov  eax, SYS_PRINT
    int  0x80
    add  esp, 4

    ; Open hello.txt
    push 0
    push path_hello
    mov  eax, SYS_OPEN
    int  0x80
    add  esp, 8

    test eax, eax
    jz   .cat_fail

    mov  [fd_store], eax

    ; Read contents
    push 63
    push read_buf
    push dword [fd_store]
    mov  eax, SYS_READ
    int  0x80
    add  esp, 12

    test eax, eax
    jle  .cat_fail

    ; Print file contents
    push read_buf
    mov  eax, SYS_PRINT
    int  0x80
    add  esp, 4

    push msg_newline
    mov  eax, SYS_PRINT
    int  0x80
    add  esp, 4
    jmp  .prompt_loop

.cat_fail:
    push msg_err_read
    mov  eax, SYS_PRINT
    int  0x80
    add  esp, 4
    jmp  .prompt_loop


; ── str_startswith(buf, prefix) → eax=1 if match ──
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


; ── memzero(buf, len) 
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
msg_welcome:    db 'CYSEOS Shell v0.1', 0x0A,
                db 'Commands: ls  cat', 0x0A, 0
msg_prompt:     db '> ', 0
msg_unknown:    db 'Unknown command. Try: ls  cat', 0x0A, 0
msg_ls_header:  db 'Listing 0:/', 0x0A, 0
msg_ls_files:   db '  hello.txt', 0x0A,
                db '  blank.bin', 0x0A,
                db '  shell.bin', 0x0A, 0
msg_cat_header: db 'cat 0:/hello.txt:', 0x0A, 0
msg_newline:    db 0x0A, 0
msg_err_open:   db 'Error: could not open', 0x0A, 0
msg_err_read:   db 'Error: could not read', 0x0A, 0

cmd_ls:         db 'ls', 0
cmd_cat:        db 'cat', 0
path_root:      db '0:/', 0
path_hello:     db '0:/hello.txt', 0

section .bss
cmd_buf:   resb 64
read_buf:  resb 64
fd_store:  resd 1