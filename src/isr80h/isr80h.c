#include "isr80h.h"
#include "idt/idt.h"
#include "misc.h"
#include "io.h"
#include "process.h"

void isr80h_register_commands()
{
    isr80h_register_command(SYSTEM_COMMAND0_SUM,      isr80h_command0_sum);
    isr80h_register_command(SYSTEM_COMMAND1_PRINT,    isr80h_command1_print);
    isr80h_register_command(SYSTEM_COMMAND2_OPEN,     isr80h_command2_open);
    isr80h_register_command(SYSTEM_COMMAND3_READ,     isr80h_command3_read);
    isr80h_register_command(SYSTEM_COMMAND4_FORK,     isr80h_command4_fork);
    isr80h_register_command(SYSTEM_COMMAND5_EXIT,     isr80h_command5_exit);
    isr80h_register_command(SYSTEM_COMMAND6_READLINE, isr80h_command6_readline);
}