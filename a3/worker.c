#include "worker.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "analysis.h"
#include "ipc.h"
#include "protocol.h"

static int process_task_message(const TaskMessage *task, int result_write_fd)
{
    AnalysisResult analysis;
    ResultMessage result;

    /* Every valid task produces one struct reply on the worker -> parent pipe. */
    memset(&result, 0, sizeof(result));
    result.type = MSG_RESULT;
    result.task_id = task->task_id;
    result.worker_pid = (int32_t)getpid();
    snprintf(result.filename, sizeof(result.filename), "%s", task->filename);

    //Github Copilot (model: gpt-5.4) helped with the logic of the if statement below.
    if (analyze_file(task->filename, &analysis) == 0) {
        result.success = 1;
        result.line_count = analysis.line_count;
        result.word_count = analysis.word_count;
        result.char_count = analysis.char_count;
    } else {
        result.success = 0;
        result.error_code = analysis.error_code;
        snprintf(result.error_message, sizeof(result.error_message), "%s", analysis.error_message);
    }
    //Github Copilot (model: gpt-5.4) helped with the logic of the if statement below.
    if (write_full(result_write_fd, &result, sizeof(result)) != IPC_OK) {
        perror("worker write_full");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

int worker_main_loop(int task_read_fd, int result_write_fd)
{
    for (;;) {
        TaskMessage task;
        IpcStatus status = read_full(task_read_fd, &task, sizeof(task));
        //Github Copilot (model: gpt-5.4) helped with the logic of the if statements below.
        if (status == IPC_EOF) {
            return EXIT_SUCCESS;
        }

        if (status != IPC_OK) {
            return EXIT_FAILURE;
        }

        if (task.type == MSG_SHUTDOWN) {
            /* The shutdown path is a normal control message, not an error. */
            return EXIT_SUCCESS;
        }

        if (task.type != MSG_TASK) {
            return EXIT_FAILURE;
        }

        /* Workers stay in this loop so the fixed pool can process many tasks concurrently. */
        if (process_task_message(&task, result_write_fd) != EXIT_SUCCESS) {
            return EXIT_FAILURE;
        }
    }
}