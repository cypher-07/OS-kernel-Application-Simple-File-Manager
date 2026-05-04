section .asm

extern int21h_handler
extern int20h_handler
extern no_interrupt_handler
extern isr80h_handler
extern current_task
extern task_return

global int21h
global int20h
global idt_load
global no_interrupt
global enable_interrupts
global disable_interrupts
global isr80h_wrapper

enable_interrupts:
    sti
    ret

disable_interrupts:
    cli
    ret


idt_load:
    push ebp
    mov ebp, esp

    mov ebx, [ebp+8]
    lidt [ebx]
    pop ebp
    ret


int21h:
    pushad
    call int21h_handler
    popad
    iret

; IRQ 0 — preemptive timer: save state, switch to next task if one exists
int20h:
    pushad
    push esp
    call int20h_handler
    add esp, 4
    test eax, eax
    jz .no_switch
    push eax            ; &next_task->registers
    call task_return    ; never returns — jumps to next task
.no_switch:
    popad
    iret

no_interrupt:
    pushad
    call no_interrupt_handler
    popad
    iret

isr80h_wrapper:
    ; INTERRUPT FRAME START
    ; ALREADY PUSHED TO US BY THE PROCESSOR UPON ENTRY TO THIS INTERRUPT
    ; uint32_t ip
    ; uint32_t cs;
    ; uint32_t flags
    ; uint32_t sp;
    ; uint32_t ss;
    ; Pushes the general purpose registers to the stack
    pushad

    ; INTERRUPT FRAME END

    ; Snapshot current_task so we can detect a task switch inside the syscall
    mov eax, [current_task]
    mov [saved_task], eax

    ; Original EAX (syscall command) was saved by pushad at [esp+28]
    mov eax, [esp+28]

    ; Push the stack pointer so that we are pointing to the interrupt frame
    push esp

    ; EAX holds our command lets push it to the stack for isr80h_handler
    push eax
    call isr80h_handler
    mov dword[tmp_res], eax
    add esp, 8

    ; Did the syscall change current_task (e.g. sys_exit freed the child)?
    mov eax, [current_task]
    cmp eax, [saved_task]
    jne .task_switched

    ; Restore general purpose registers for user land
    popad
    mov eax, [tmp_res]
    iretd

.task_switched:
    ; A different task is now current — restore it via task_return
    mov ebx, [current_task]
    add ebx, 4           ; offset of registers field in struct task
    push ebx
    call task_return     ; never returns — jumps to new current task

section .data
tmp_res:    dd 0
saved_task: dd 0
