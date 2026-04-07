#ifndef IPC_H
#define IPC_H

#include <stddef.h>

typedef enum {
    IPC_OK = 0,
    IPC_EOF = 1,
    IPC_SHORT = 2,
    IPC_ERROR = 3
} IpcStatus;

IpcStatus read_full(int fd, void *buffer, size_t count);
IpcStatus write_full(int fd, const void *buffer, size_t count);
void close_fd_if_open(int *fd);

#endif