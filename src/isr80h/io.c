#include "io.h"
#include "task/task.h"
#include "kernel.h"
#include "status.h"
#include "memory/heap/kheap.h"

/* Forward declarations for filesystem and paging symbols.
   Avoids pulling in fs/file.h (which has a pparser.h dependency that
   does not resolve cleanly from this compilation unit). */
int fopen(const char* filename, const char* mode_str);
int fread(void* ptr, unsigned int size, unsigned int nmemb, int fd);
int fwrite(void* ptr, unsigned int size, unsigned int nmemb, int fd);
int fclose(int fd);
int fdelete(const char* filename);
int freaddir(int index, char* buf, int buf_size);

/* FILE_MODE_READ / FILE_MODE_WRITE match the enum in fs/file.h */
#define FILE_MODE_READ  0
#define FILE_MODE_WRITE 1

/* PAGING_PAGE_SIZE is already pulled in via task/task.h -> paging.h,
   but guard it so we do not redefine it if the header was found. */
#ifndef PAGING_PAGE_SIZE
#define PAGING_PAGE_SIZE 4096
#endif

/* PEACHOS_MAX_PATH is already pulled in via task/task.h -> config.h */
#ifndef PEACHOS_MAX_PATH
#define PEACHOS_MAX_PATH 108
#endif

void* isr80h_command1_print(struct interrupt_frame* frame)
{
    void* user_space_msg_buffer = task_get_stack_item(task_current(), 0);
    char buf[1024];
    copy_string_from_task(task_current(), user_space_msg_buffer, buf, sizeof(buf));
    print(buf);
    return 0;
}

void* isr80h_command2_open(struct interrupt_frame* frame)
{
    struct task* task = task_current();

    void* user_path_ptr = task_get_stack_item(task, 0);
    int mode = (int)task_get_stack_item(task, 1);

    char path[PEACHOS_MAX_PATH];
    int res = copy_string_from_task(task, user_path_ptr, path, sizeof(path));
    if (res < 0)
        return (void*)res;

    const char* mode_str;
    if (mode == FILE_MODE_READ)
        mode_str = "r";
    else if (mode == FILE_MODE_WRITE)
        mode_str = "w";
    else
        return (void*)-EINVARG;

    int fd = fopen(path, mode_str);
    return (void*)fd;
}

void* isr80h_command3_read(struct interrupt_frame* frame)
{
    struct task* task = task_current();

    int fd         = (int)task_get_stack_item(task, 0);
    void* user_buf = task_get_stack_item(task, 1);
    int count      = (int)task_get_stack_item(task, 2);

    if (count <= 0 || count >= PAGING_PAGE_SIZE)
        return (void*)-EINVARG;

    char* kernel_buf = kzalloc(count);
    if (!kernel_buf)
        return (void*)-ENOMEM;

    /* fread(buf, size, nmemb, fd): read nmemb items of size bytes each.
       We pass size=1, nmemb=count to read individual bytes. */
    int bytes_read = fread(kernel_buf, 1, count, fd);
    if (bytes_read > 0)
    {
        int copy_res = copy_to_task(task, user_buf, kernel_buf, bytes_read);
        if (copy_res < 0)
        {
            kfree(kernel_buf);
            return (void*)copy_res;
        }
    }

    kfree(kernel_buf);
    return (void*)bytes_read;
}

void* isr80h_command7_write(struct interrupt_frame* frame)
{
    struct task* task = task_current();

    int fd         = (int)task_get_stack_item(task, 0);
    void* user_buf = task_get_stack_item(task, 1);
    int count      = (int)task_get_stack_item(task, 2);

    if (count <= 0 || count >= PAGING_PAGE_SIZE)
        return (void*)-EINVARG;

    char* kernel_buf = kzalloc(count);
    if (!kernel_buf)
        return (void*)-ENOMEM;

    int res = copy_string_from_task(task, user_buf, kernel_buf, count);
    if (res < 0)
    {
        kfree(kernel_buf);
        return (void*)res;
    }

    int bytes_written = fwrite(kernel_buf, 1, count, fd);
    kfree(kernel_buf);
    return (void*)bytes_written;
}

void* isr80h_command8_delete(struct interrupt_frame* frame)
{
    struct task* task = task_current();

    void* user_path_ptr = task_get_stack_item(task, 0);
    char path[PEACHOS_MAX_PATH];
    int res = copy_string_from_task(task, user_path_ptr, path, sizeof(path));
    if (res < 0)
        return (void*)res;

    res = fdelete(path);
    return (void*)res;
}

void* isr80h_command9_close(struct interrupt_frame* frame)
{
    struct task* task = task_current();
    int fd = (int)task_get_stack_item(task, 0);
    return (void*)fclose(fd);
}

void* isr80h_command10_readdir(struct interrupt_frame* frame)
{
    struct task* task = task_current();

    int   index    = (int)task_get_stack_item(task, 0);
    void* user_buf = task_get_stack_item(task, 1);
    int   buf_size = (int)task_get_stack_item(task, 2);

    if (index < 0 || buf_size <= 0 || buf_size > 256)
        return (void*)-EINVARG;

    char* kernel_buf = kzalloc(buf_size);
    if (!kernel_buf)
        return (void*)-ENOMEM;

    int res = freaddir(index, kernel_buf, buf_size);
    if (res > 0)
    {
        int copy_res = copy_to_task(task, user_buf, kernel_buf, buf_size);
        if (copy_res < 0)
        {
            kfree(kernel_buf);
            return (void*)copy_res;
        }
    }

    kfree(kernel_buf);
    return (void*)res;
}