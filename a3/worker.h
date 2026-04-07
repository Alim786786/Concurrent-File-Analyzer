#ifndef WORKER_H
#define WORKER_H

int worker_main_loop(int task_read_fd, int result_write_fd);

#endif