#include "controller.h"

#include <errno.h>
#include <inttypes.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "ipc.h"
#include "protocol.h"
#include "worker.h"

typedef enum {
    TASK_QUEUED = 0,
    TASK_RUNNING = 1,
    TASK_DONE = 2
} TaskState;

typedef struct {
    int task_id;
    const char *filename;
    TaskState state;
    int assigned_worker;
    int success;
    uint64_t line_count;
    uint64_t word_count;
    uint64_t char_count;
    int error_code;
    char error_message[ANALYZER_ERROR_SIZE];
} TaskRecord;

typedef struct {
    size_t index;
    pid_t pid;
    int task_write_fd;
    int result_read_fd;
    int alive;
    int busy;
    int current_task_id;
} WorkerState;

typedef struct {
    WorkerState *workers;
    size_t worker_count;
    TaskRecord *tasks;
    size_t task_count;
    size_t completed_tasks;
    size_t successful_tasks;
    size_t failed_tasks;
    uint64_t total_lines;
    uint64_t total_words;
    uint64_t total_chars;
} ControllerState;

static void initialize_tasks(ControllerState *controller, char *const files[])
{
    //Github Copilot (model: gpt-5.4) helped with the logic of the for loop below. Particularly the configuration of each field in the TaskRecord struct for each task.
    for (size_t index = 0; index < controller->task_count; index++) {
        controller->tasks[index].task_id = (int)index;
        controller->tasks[index].filename = files[index];
        controller->tasks[index].state = TASK_QUEUED;
        controller->tasks[index].assigned_worker = -1;
        controller->tasks[index].success = 0;
        controller->tasks[index].line_count = 0;
        controller->tasks[index].word_count = 0;
        controller->tasks[index].char_count = 0;
        controller->tasks[index].error_code = 0;
    /* Unexpected worker death returns its unfinished task to the pending queue. */
        controller->tasks[index].error_message[0] = '\0';
    }
}

static size_t count_live_workers(const ControllerState *controller)
{
    size_t live_workers = 0;

    for (size_t index = 0; index < controller->worker_count; index++) {
        if (controller->workers[index].alive) {
            live_workers++;
        }
    }

    return live_workers;
}

static int has_incomplete_tasks(const ControllerState *controller)
{
    return controller->completed_tasks < controller->task_count;
}

static int next_queued_task_index(const ControllerState *controller)
{
    for (size_t index = 0; index < controller->task_count; index++) {
        if (controller->tasks[index].state == TASK_QUEUED) {
            return (int)index;
        }
    }

    return -1;
}

static void print_summary(const ControllerState *controller)
{
    printf("\nSummary:\n");
    printf("Successful files: %zu\n", controller->successful_tasks);
    printf("Failed files: %zu\n", controller->failed_tasks);
    printf("Total lines: %" PRIu64 "\n", controller->total_lines);
    printf("Total words: %" PRIu64 "\n", controller->total_words);
    printf("Total chars: %" PRIu64 "\n", controller->total_chars);
}

static void reap_worker_if_possible(WorkerState *worker, int nohang)
{
    int status;
    int options = nohang ? WNOHANG : 0;
    pid_t wait_result;

    if (worker->pid <= 0) {
        return;
    }

    do {
        wait_result = waitpid(worker->pid, &status, options);
    } while (wait_result < 0 && errno == EINTR);
    //Github Copilot (model: gpt-5.4) helped with the logic of the if statements with regrads to the wait_result and errno values.
    if (wait_result == worker->pid || (wait_result < 0 && errno == ECHILD)) {
        worker->pid = -1;
    } else if (wait_result < 0) {
        perror("waitpid");
    }
}

static void send_shutdown_to_workers(ControllerState *controller)
{
    TaskMessage shutdown_message;

    memset(&shutdown_message, 0, sizeof(shutdown_message));
    shutdown_message.type = MSG_SHUTDOWN;
    shutdown_message.task_id = -1;

    /* Shutdown is broadcast after all queued work is accounted for. */
    for (size_t index = 0; index < controller->worker_count; index++) {
        WorkerState *worker = &controller->workers[index];
        
        if (!worker->alive || worker->task_write_fd < 0) {
            continue;
        }
        //Github Copilot (model: gpt-5.4) helped with the logic of the if statement below. Particularly the error handling logic if the shutdown message could not be sent to a worker.
        if (write_full(worker->task_write_fd, &shutdown_message, sizeof(shutdown_message)) != IPC_OK) {
            fprintf(stderr, "Warning: failed to send shutdown to worker %zu\n", worker->index);
            worker->alive = 0;
        }
        close_fd_if_open(&worker->task_write_fd);
    }
}

static void cleanup_controller(ControllerState *controller, int send_shutdown)
{
    if (controller == NULL) {
        return;
    }

    if (send_shutdown) {
        send_shutdown_to_workers(controller);
    }
    //Github Copilot (model: gpt-5.4) helped with the logic of the if statement below. Particularly the cleanup logic for each worker and task in the controller.
    for (size_t index = 0; index < controller->worker_count; index++) {
        close_fd_if_open(&controller->workers[index].task_write_fd);
        close_fd_if_open(&controller->workers[index].result_read_fd);
        reap_worker_if_possible(&controller->workers[index], 0);
    }

    free(controller->workers);
    free(controller->tasks);
    controller->workers = NULL;
    controller->tasks = NULL;
}

static int spawn_worker(ControllerState *controller, size_t worker_index)
{
    int parent_to_worker[2] = {-1, -1};
    int worker_to_parent[2] = {-1, -1};
    pid_t child_pid;

    /* Each worker gets one pipe for tasks and one pipe for results. */
    if (pipe(parent_to_worker) < 0) {
        perror("pipe");
        return -1;
    }

    if (pipe(worker_to_parent) < 0) {
        perror("pipe");
        close_fd_if_open(&parent_to_worker[0]);
        close_fd_if_open(&parent_to_worker[1]);
        return -1;
    }

    /* fork creates the controller/worker split after both pipes are ready. */
    child_pid = fork();
    if (child_pid < 0) {
        perror("fork");
        close_fd_if_open(&parent_to_worker[0]);
        close_fd_if_open(&parent_to_worker[1]);
        close_fd_if_open(&worker_to_parent[0]);
        close_fd_if_open(&worker_to_parent[1]);
        return -1;
    }
    //Github Copilot (model: gpt-5.4) helped with the logic of the if statement below. Particularly the logic for the child process to close the unused pipe ends and enter the worker main loop, and the parent process to close the unused pipe ends and update the worker state in the controller. 
    if (child_pid == 0) {
        /* The child keeps only the read end of its task pipe and the write end of its result pipe. */
        for (size_t index = 0; index < controller->worker_count; index++) {
            close_fd_if_open(&controller->workers[index].task_write_fd);
            close_fd_if_open(&controller->workers[index].result_read_fd);
        }

        close_fd_if_open(&parent_to_worker[1]);
        close_fd_if_open(&worker_to_parent[0]);
        _exit(worker_main_loop(parent_to_worker[0], worker_to_parent[1]));
    }

    /* The parent keeps only the write end for tasks and the read end for results. */
    close_fd_if_open(&parent_to_worker[0]);
    close_fd_if_open(&worker_to_parent[1]);

    controller->workers[worker_index].index = worker_index;
    controller->workers[worker_index].pid = child_pid;
    controller->workers[worker_index].task_write_fd = parent_to_worker[1];
    controller->workers[worker_index].result_read_fd = worker_to_parent[0];
    controller->workers[worker_index].alive = 1;
    controller->workers[worker_index].busy = 0;
    controller->workers[worker_index].current_task_id = -1;

    return 0;
}

static void requeue_running_task(ControllerState *controller, WorkerState *worker)
{
    if (!worker->busy || worker->current_task_id < 0) {
        return;
    }

    if ((size_t)worker->current_task_id >= controller->task_count) {
        worker->busy = 0;
        worker->current_task_id = -1;
        return;
    }

    TaskRecord *task = &controller->tasks[worker->current_task_id];

    if (task->state == TASK_RUNNING) {
        task->state = TASK_QUEUED;
        task->assigned_worker = -1;
    }

    worker->busy = 0;
    worker->current_task_id = -1;
}

static void handle_worker_failure(ControllerState *controller, size_t worker_index, const char *reason)
{
    WorkerState *worker = &controller->workers[worker_index];

    if (!worker->alive) {
        return;
    }

    fprintf(stderr, "Worker %zu failed: %s\n", worker->index, reason);
    requeue_running_task(controller, worker);
    worker->alive = 0;
    close_fd_if_open(&worker->task_write_fd);
    close_fd_if_open(&worker->result_read_fd);
    reap_worker_if_possible(worker, 1);
}

static void fail_task_locally(ControllerState *controller, TaskRecord *task, int error_code)
{
    //Github Copilot (model: gpt-5.4) helped with the logic below. Particularly the logic to update the task state and error details, and to print the error message for the failed task.
    task->state = TASK_DONE;
    task->assigned_worker = -1;
    task->success = 0;
    task->error_code = error_code;
    snprintf(task->error_message, sizeof(task->error_message), "%s", strerror(error_code));

    controller->completed_tasks++;
    controller->failed_tasks++;

    printf("Error processing %s: %s\n", task->filename, task->error_message);
}

static int dispatch_task_to_worker(ControllerState *controller, size_t worker_index, int task_index)
{
    TaskMessage message;
    TaskRecord *task = &controller->tasks[task_index];
    WorkerState *worker = &controller->workers[worker_index];

    if (!worker->alive || worker->busy) {
        return 0;
    }

    if (strlen(task->filename) >= sizeof(message.filename)) {
        fail_task_locally(controller, task, ENAMETOOLONG);
        return 0;
    }

    memset(&message, 0, sizeof(message));
    message.type = MSG_TASK;
    message.task_id = task->task_id;
    snprintf(message.filename, sizeof(message.filename), "%s", task->filename);

    if (write_full(worker->task_write_fd, &message, sizeof(message)) != IPC_OK) {
        handle_worker_failure(controller, worker_index, "could not send task message");
        return -1;
    }

    task->state = TASK_RUNNING;
    task->assigned_worker = (int)worker_index;
    worker->busy = 1;
    worker->current_task_id = task->task_id;

    printf("Worker %zu (pid %ld) assigned: %s\n", worker->index, (long)worker->pid, task->filename);
    return 0;
}

static int dispatch_pending_tasks(ControllerState *controller)
{
    //Github Copilot (model: gpt-5.4) helped with the logic of the for loop below. Particularly the logic to skip busy or dead workers, to dispatch queued tasks to idle workers, and to handle the error case where a task could not be dispatched and all workers are dead.
    for (size_t worker_index = 0; worker_index < controller->worker_count; worker_index++) {
        int task_index;
        WorkerState *worker = &controller->workers[worker_index];

        if (!worker->alive || worker->busy) {
            continue;
        }

        task_index = next_queued_task_index(controller);
        if (task_index < 0) {
            break;
        }

        if (dispatch_task_to_worker(controller, worker_index, task_index) < 0 &&
            count_live_workers(controller) == 0 && has_incomplete_tasks(controller)) {
            return -1;
        }
    }

    return 0;
}

static int mark_task_completed(ControllerState *controller, size_t worker_index, const ResultMessage *result)
{
    TaskRecord *task;
    WorkerState *worker = &controller->workers[worker_index];

    if (result->task_id < 0 || (size_t)result->task_id >= controller->task_count) {
        return -1;
    }

    task = &controller->tasks[result->task_id];

    if (!worker->busy || worker->current_task_id != result->task_id) {
        return -1;
    }

    if (task->state != TASK_RUNNING || task->assigned_worker != (int)worker_index) {
        return -1;
    }

    task->state = TASK_DONE;
    task->assigned_worker = -1;
    task->success = result->success;
    task->line_count = result->line_count;
    task->word_count = result->word_count;
    task->char_count = result->char_count;
    task->error_code = result->error_code;
    snprintf(task->error_message, sizeof(task->error_message), "%s", result->error_message);

    worker->busy = 0;
    worker->current_task_id = -1;
    controller->completed_tasks++;
    //Github Copilot (model: gpt-5.4) helped with the logic of the if statement below. Particularly the logic to update the controller state with the completed task details, and to print either the result or error message for the completed task.
    if (task->success) {
        controller->successful_tasks++;
        controller->total_lines += task->line_count;
        controller->total_words += task->word_count;
        controller->total_chars += task->char_count;
        printf("Result for %s: %" PRIu64 " lines, %" PRIu64 " words, %" PRIu64 " chars (worker %" PRId32 ")\n",
               task->filename, task->line_count, task->word_count, task->char_count,
               result->worker_pid);
    } else {
        controller->failed_tasks++;
        printf("Error processing %s: %s\n", task->filename,
               task->error_message[0] == '\0' ? "unknown error" : task->error_message);
    }

    return 0;
}

static int collect_ready_results(ControllerState *controller)
{
    struct pollfd *poll_fds;
    size_t *worker_map;
    size_t live_workers = count_live_workers(controller);
    int poll_result;

    if (live_workers == 0) {
        return -1;
    }

    poll_fds = calloc(live_workers, sizeof(*poll_fds));
    worker_map = calloc(live_workers, sizeof(*worker_map));
    if (poll_fds == NULL || worker_map == NULL) {
        perror("calloc");
        free(poll_fds);
        free(worker_map);
        return -1;
    }

    live_workers = 0;
    for (size_t index = 0; index < controller->worker_count; index++) {
        if (!controller->workers[index].alive) {
            continue;
        }

        poll_fds[live_workers].fd = controller->workers[index].result_read_fd;
        poll_fds[live_workers].events = POLLIN | POLLHUP | POLLERR;
        worker_map[live_workers] = index;
        live_workers++;
    }

    /* poll waits for whichever live worker produces the next result or disconnects. */
    do {
        poll_result = poll(poll_fds, live_workers, -1);
    } while (poll_result < 0 && errno == EINTR);

    if (poll_result < 0) {
        perror("poll");
        free(poll_fds);
        free(worker_map);
        return -1;
    }
    //Github Copilot (model: gpt-5.4) helped with the logic of the for loop below. Specifications are given below.
    for (size_t index = 0; index < live_workers; index++) {
        size_t worker_index = worker_map[index];
        //Github Copilot (model: gpt-5.4) helped with the logic of the if statements below. Particularly the logic to read the result message from the ready worker, to mark the corresponding task as completed, and to handle different error cases such as malformed messages, unexpected pipe closure, short reads, and read errors.
        if ((poll_fds[index].revents & POLLIN) != 0) {
            ResultMessage result;
            IpcStatus status = read_full(poll_fds[index].fd, &result, sizeof(result));

            if (status == IPC_OK) {
                if (result.type != MSG_RESULT || mark_task_completed(controller, worker_index, &result) < 0) {
                    handle_worker_failure(controller, worker_index, "received malformed result message");
                }
            } else if (status == IPC_EOF) {
                handle_worker_failure(controller, worker_index, "result pipe closed unexpectedly");
            } else if (status == IPC_SHORT) {
                handle_worker_failure(controller, worker_index, "short read on result pipe");
            } else {
                handle_worker_failure(controller, worker_index, "error reading result pipe");
            }
        } else if ((poll_fds[index].revents & (POLLHUP | POLLERR | POLLNVAL)) != 0) {
            handle_worker_failure(controller, worker_index, "worker pipe reported hangup or error");
        }
    }

    free(poll_fds);
    free(worker_map);
    return 0;
}

static int report_all_workers_dead_if_needed(const ControllerState *controller)
{
    if (count_live_workers(controller) == 0 && has_incomplete_tasks(controller)) {
        fprintf(stderr, "Fatal error: all workers exited before all tasks completed.\n");
        return -1;
    }

    return 0;
}

int run_controller(char *const files[], size_t file_count, size_t worker_count)
{
    ControllerState controller;

    memset(&controller, 0, sizeof(controller));
    controller.worker_count = worker_count;
    controller.task_count = file_count;

    controller.workers = calloc(worker_count, sizeof(*controller.workers));
    controller.tasks = calloc(file_count, sizeof(*controller.tasks));
    if (controller.workers == NULL || controller.tasks == NULL) {
        perror("calloc");
        cleanup_controller(&controller, 0);
        return EXIT_FAILURE;
    }

    for (size_t index = 0; index < controller.worker_count; index++) {
        controller.workers[index].pid = -1;
        controller.workers[index].task_write_fd = -1;
        controller.workers[index].result_read_fd = -1;
        controller.workers[index].current_task_id = -1;
    }

    initialize_tasks(&controller, files);

    for (size_t index = 0; index < controller.worker_count; index++) {
        if (spawn_worker(&controller, index) < 0) {
            cleanup_controller(&controller, 1);
            return EXIT_FAILURE;
        }
    }
    //Github Copilot (model: gpt-5.4) helped with the logic of the while loop below. Particularly the logic to dispatch pending tasks, to check for worker failures, and to collect ready results until all tasks are completed.
    while (controller.completed_tasks < controller.task_count) {
        if (dispatch_pending_tasks(&controller) < 0) {
            cleanup_controller(&controller, 1);
            return EXIT_FAILURE;
        }

        if (controller.completed_tasks >= controller.task_count) {
            break;
        }

        if (report_all_workers_dead_if_needed(&controller) < 0) {
            cleanup_controller(&controller, 0);
            return EXIT_FAILURE;
        }

        if (collect_ready_results(&controller) < 0) {
            if (report_all_workers_dead_if_needed(&controller) < 0) {
                cleanup_controller(&controller, 0);
                return EXIT_FAILURE;
            }

            cleanup_controller(&controller, 1);
            return EXIT_FAILURE;
        }
    }

    send_shutdown_to_workers(&controller);
    print_summary(&controller);
    cleanup_controller(&controller, 0);
    return EXIT_SUCCESS;
}