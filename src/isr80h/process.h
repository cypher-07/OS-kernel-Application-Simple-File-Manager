#ifndef ISR80H_PROCESS_H
#define ISR80H_PROCESS_H

struct interrupt_frame;
void* isr80h_command4_fork(struct interrupt_frame* frame);
void* isr80h_command5_exit(struct interrupt_frame* frame);
void* isr80h_command6_readline(struct interrupt_frame* frame);

#endif