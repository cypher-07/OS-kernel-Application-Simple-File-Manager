#include "process.h"
#include "config.h"
#include "status.h"
#include "task/task.h"
#include "memory/memory.h"
#include "string/string.h"
#include "fs/file.h"
#include "memory/heap/kheap.h"
#include "memory/paging/paging.h"
#include "kernel.h"

// The current process that is running
struct process* current_process = 0;

static struct process* processes[PEACHOS_MAX_PROCESSES] = {};

static void process_init(struct process* process)
{
    memset(process, 0, sizeof(struct process));
}

struct process* process_current()
{
    return current_process;
}

struct process* process_get(int process_id)
{
    if (process_id < 0 || process_id >= PEACHOS_MAX_PROCESSES)
    {
        return NULL;
    }

    return processes[process_id];
}

static int process_load_binary(const char* filename, struct process* process)
{
    int res = 0;
    int fd = fopen(filename, "r");
    if (!fd)
    {
        res = -EIO;
        goto out;
    }

    struct file_stat stat;
    res = fstat(fd, &stat);
    if (res != PEACHOS_ALL_OK)
    {
        goto out;
    }

    void* program_data_ptr = kzalloc(stat.filesize);
    if (!program_data_ptr)
    {
        res = -ENOMEM;
        goto out;
    }

    if (fread(program_data_ptr, stat.filesize, 1, fd) != 1)
    {
        res = -EIO;
        goto out;
    }

    process->ptr = program_data_ptr;
    process->size = stat.filesize;

out:
    fclose(fd);
    return res;
}
static int process_load_data(const char* filename, struct process* process)
{
    int res = 0;
    res = process_load_binary(filename, process);
    return res;
}

int process_map_binary(struct process* process)
{
    int res = 0;
    paging_map_to(process->task->page_directory, (void*) PEACHOS_PROGRAM_VIRTUAL_ADDRESS, process->ptr, paging_align_address(process->ptr + process->size), PAGING_IS_PRESENT | PAGING_ACCESS_FROM_ALL | PAGING_IS_WRITEABLE);
    return res;
}
int process_map_memory(struct process* process)
{
    int res = 0;
    res = process_map_binary(process);
    if (res < 0)
    {
        goto out;
    }

    paging_map_to(process->task->page_directory, (void*)PEACHOS_PROGRAM_VIRTUAL_STACK_ADDRESS_END, process->stack, paging_align_address(process->stack+PEACHOS_USER_PROGRAM_STACK_SIZE), PAGING_IS_PRESENT | PAGING_ACCESS_FROM_ALL | PAGING_IS_WRITEABLE);
out:
    return res;
}

int process_get_free_slot()
{
    for (int i = 0; i < PEACHOS_MAX_PROCESSES; i++)
    {
        if (processes[i] == 0)
            return i;
    }

    return -EISTKN;
}

int process_load(const char* filename, struct process** process)
{
    int res = 0;
    int process_slot = process_get_free_slot();
    if (process_slot < 0)
    {
        res = -EISTKN;
        goto out;
    }

    res = process_load_for_slot(filename, process, process_slot);
out:
    return res;
}

int process_load_for_slot(const char* filename, struct process** process, int process_slot)
{
    int res = 0;
    struct task* task = 0;
    struct process* _process;
    void* program_stack_ptr = 0;

    if (process_get(process_slot) != 0)
    {
        res = -EISTKN;
        goto out;
    }

    _process = kzalloc(sizeof(struct process));
    if (!_process)
    {
        res = -ENOMEM;
        goto out;
    }

    process_init(_process);
    res = process_load_data(filename, _process);
    if (res < 0)
    {
        goto out;
    }

    program_stack_ptr = kzalloc(PEACHOS_USER_PROGRAM_STACK_SIZE);
    if (!program_stack_ptr)
    {
        res = -ENOMEM;
        goto out;
    }

    strncpy(_process->filename, filename, sizeof(_process->filename));
    _process->stack = program_stack_ptr;
    _process->id = process_slot;

    // Create a task
    task = task_new(_process);
    if (ERROR_I(task) == 0)
    {
        res = ERROR_I(task);
        goto out;
    }

    _process->task = task;

    res = process_map_memory(_process);
    if (res < 0)
    {
        goto out;
    }

    *process = _process;

    // Add the process to the array
    processes[process_slot] = _process;

out:
    if (ISERR(res))
    {
        if (_process && _process->task)
        {
            task_free(_process->task);
        }

       // Free the process data
    }
    return res;
}

int process_fork(struct process* parent, struct process** child_out)
{
    int res = 0;

    int slot = process_get_free_slot();
    if (slot < 0)
    {
        res = -EISTKN;
        goto out;
    }

    struct process* child = kzalloc(sizeof(struct process));
    if (!child)
    {
        res = -ENOMEM;
        goto out;
    }

    process_init(child);

    // Copy program code to new physical memory
    child->ptr = kzalloc(parent->size);
    if (!child->ptr)
    {
        res = -ENOMEM;
        goto out;
    }
    memcpy(child->ptr, parent->ptr, parent->size);
    child->size = parent->size;

    // Allocate new stack
    child->stack = kzalloc(PEACHOS_USER_PROGRAM_STACK_SIZE);
    if (!child->stack)
    {
        res = -ENOMEM;
        goto out;
    }

    // Copy stack contents from parent
    memcpy(child->stack, parent->stack, PEACHOS_USER_PROGRAM_STACK_SIZE);

    strncpy(child->filename, parent->filename, sizeof(child->filename));
    child->id = slot;

    // Create task - this calls paging_new_4gb internally
    struct task* child_task = task_new(child);
    if (ERROR_I(child_task) == 0)
    {
        res = ERROR_I(child_task);
        goto out;
    }
    child->task = child_task;

    // Clone page directory from parent
    paging_free_4gb(child_task->page_directory);
    child_task->page_directory = paging_copy_4gb(parent->task->page_directory);
    if (!child_task->page_directory)
    {
        res = -ENOMEM;
        goto out;
    }

    // Copy parent's saved registers into child
    child_task->registers = parent->task->registers;

    // Child returns 0 from fork
    child_task->registers.eax = 0;

    processes[slot] = child;
    *child_out = child;

out:
    return res;
}