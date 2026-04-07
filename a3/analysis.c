#include "analysis.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define ANALYSIS_BUFFER_SIZE 4096

static void set_analysis_error(AnalysisResult *result, int error_code)
{
    result->success = 0;
    result->line_count = 0;
    result->word_count = 0;
    result->char_count = 0;
    result->error_code = error_code;
    snprintf(result->error_message, sizeof(result->error_message), "%s", strerror(error_code));
}

int analyze_file(const char *filename, AnalysisResult *result)
{
    char buffer[ANALYSIS_BUFFER_SIZE];
    int fd;
    int in_word = 0;

    if (filename == NULL || result == NULL) {
        errno = EINVAL;
        return -1;
    }

    memset(result, 0, sizeof(*result));

    fd = open(filename, O_RDONLY);
    if (fd < 0) {
        set_analysis_error(result, errno);
        return -1;
    }
    //Github Copilot (model: gpt-5.4) helped with the logic of the loop below. Particularly the logic to read the file in chunks, to update the line, word, and character counts based on the content of each chunk, and to handle different error cases such as read errors and short reads.
    for (;;) {
        ssize_t bytes_read = read(fd, buffer, sizeof(buffer));

        if (bytes_read == 0) {
            break;
        }

        if (bytes_read < 0) {
            if (errno == EINTR) {
                continue;
            }
            set_analysis_error(result, errno);
            if (close(fd) < 0) {
                perror("close");
            }
            return -1;
        }

        result->char_count += (uint64_t)bytes_read;

        for (ssize_t index = 0; index < bytes_read; index++) {
            unsigned char current = (unsigned char)buffer[index];

            if (current == '\n') {
                result->line_count++;
            }

            if (isspace(current)) {
                in_word = 0;
            } else if (!in_word) {
                result->word_count++;
                in_word = 1;
            }
        }
    }

    if (close(fd) < 0) {
        set_analysis_error(result, errno);
        return -1;
    }

    result->success = 1;
    return 0;
}