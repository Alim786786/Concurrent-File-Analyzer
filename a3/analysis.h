#ifndef ANALYSIS_H
#define ANALYSIS_H

#include <stdint.h>

#include "protocol.h"

typedef struct {
    int success;
    uint64_t line_count;
    uint64_t word_count;
    uint64_t char_count;
    int error_code;
    char error_message[ANALYZER_ERROR_SIZE];
} AnalysisResult;

int analyze_file(const char *filename, AnalysisResult *result);

#endif