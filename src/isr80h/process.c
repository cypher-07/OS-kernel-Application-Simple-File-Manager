#include "process.h"
#include "isr80h.h"
#include "task/task.h"
#include "task/process.h"
#include "keyboard/keyboard.h"
#include "idt/idt.h"
#include "status.h"
#include "memory/heap/kheap.h"
#include "memory/memory.h"
#include "kernel.h"

void* isr80h_command4_fork(struct interrupt_frame* frame)
{
    struct process* parent = task_current()->process;
    struct process* child  = 0;

    int res = process_fork(parent, &child);
    if (res < 0)
        return (void*)res;

    return (void*)(int)child->id;
}

void* isr80h_command5_exit(struct interrupt_frame* frame)
{
    struct process* process = task_current()->process;
    process_free(process);
    return 0;
}

void* isr80h_command11_wait(struct interrupt_frame* frame)
{
    int pid = (int)task_get_stack_item(task_current(), 0);
    if (process_get(pid) != 0)
        return (void*)1;   /* child still alive */
    return 0;
}

void* isr80h_command6_readline(struct interrupt_frame* frame)
{
    struct task* task = task_current();
    void* user_buf    = task_get_stack_item(task, 0);
    int   max         = (int)task_get_stack_item(task, 1);

    if (max <= 0 || max > 512)
        return (void*)-1;

    char* kbuf = kzalloc(max);
    if (!kbuf)
        return (void*)-1;

    int i = 0;

    while (i < max - 1)
    {
        enable_interrupts();
        asm volatile("hlt");
        disable_interrupts();

        char c = keyboard_pop();
        if (c == 0)
            continue;       /* non-keyboard IRQ woke us, try again */

        if (c == 0x08)      /* backspace */
        {
            if (i > 0)
            {
                i--;
                terminal_writechar(0x08, 15);
            }
            continue;
        }

        terminal_writechar(c == '\r' ? '\n' : c, 15);
        kbuf[i++] = c;

        if (c == '\n' || c == '\r')
            break;
    }

    kbuf[i] = 0;

    if (i > 0)
        copy_to_task(task, user_buf, kbuf, i + 1);

    kfree(kbuf);
    return (void*)i;
}