#include "ipc.h"

#include <errno.h>
#include <stdio.h>
#include <unistd.h>

IpcStatus read_full(int fd, void *buffer, size_t count)
{
    char *cursor = buffer;
    size_t total_read = 0;
    //Github Copilot (model: gpt-5.4) helped with the logic of the while loop below. Particularly the if-statement logic inside the loop.
    while (total_read < count) {
        ssize_t bytes_read = read(fd, cursor + total_read, count - total_read);

        if (bytes_read == 0) {
            if (total_read == 0) {
                return IPC_EOF;
            }
            return IPC_SHORT;
        }
        
        if (bytes_read < 0) {
            if (errno == EINTR) {
                continue;
            }
            return IPC_ERROR;
        }

        total_read += (size_t)bytes_read;
    }

    return IPC_OK;
}

IpcStatus write_full(int fd, const void *buffer, size_t count)
{
    const char *cursor = buffer;
    size_t total_written = 0;
    //Github Copilot (model: gpt-5.4) helped with the logic of the while loop below. Particularly the if-statement logic inside the loop.
    while (total_written < count) {
        ssize_t bytes_written = write(fd, cursor + total_written, count - total_written);

        if (bytes_written < 0) {
            if (errno == EINTR) {
                continue;
            }
            return IPC_ERROR;
        }

        total_written += (size_t)bytes_written;
    }

    return IPC_OK;
}

void close_fd_if_open(int *fd)
{
    //Github Copilot (model: gpt-5.4) helped with the logic of the if statements below.
    if (fd == NULL || *fd < 0) {
        return;
    }

    if (close(*fd) < 0) {
        perror("close");
    }

    *fd = -1;
}