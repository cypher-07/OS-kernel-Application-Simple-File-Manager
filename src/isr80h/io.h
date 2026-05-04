#ifndef ISR80H_IO_H
#define ISR80H_IO_H

struct interrupt_frame;
void* isr80h_command1_print(struct interrupt_frame* frame);
void* isr80h_command2_open(struct interrupt_frame* frame);
void* isr80h_command3_read(struct interrupt_frame* frame);
void* isr80h_command7_write(struct interrupt_frame* frame);
void* isr80h_command8_delete(struct interrupt_frame* frame);
void* isr80h_command9_close(struct interrupt_frame* frame);
void* isr80h_command10_readdir(struct interrupt_frame* frame);
#endif