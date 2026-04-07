#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>

#define DEFAULT_WORKER_COUNT 3
#define ANALYZER_PATH_SIZE 512
#define ANALYZER_ERROR_SIZE 128

typedef enum {
    MSG_TASK = 1,
    MSG_SHUTDOWN = 2,
    MSG_RESULT = 3
} MessageType;

/*
 * Parent -> Worker: fixed-size command message written with one write_full call.
 * type selects either a filename-bearing task or an explicit shutdown request.
 */
typedef struct {
    int32_t type;
    int32_t task_id;
    char filename[ANALYZER_PATH_SIZE];
} TaskMessage;

/*
 * Worker -> Parent: fixed-size reply message written with one write_full call.
 * Each accepted task produces exactly one result carrying counts or error details.
 */
typedef struct {
    int32_t type;
    int32_t task_id;
    int32_t worker_pid;
    int32_t success;
    uint64_t line_count;
    uint64_t word_count;
    uint64_t char_count;
    int32_t error_code;
    char filename[ANALYZER_PATH_SIZE];
    char error_message[ANALYZER_ERROR_SIZE];
} ResultMessage;

#endif