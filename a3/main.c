#include <stdio.h>
#include <stdlib.h>
#include <signal.h>

#include "controller.h"
#include "protocol.h"

static void print_usage(const char *program_name)
{
    fprintf(stderr, "Usage: %s file1.txt [file2.txt ...]\n", program_name);
}

int main(int argc, char *argv[])
{
    if (signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
        perror("signal");
        return EXIT_FAILURE;
    }

    if (argc < 2) {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    return run_controller(&argv[1], (size_t)(argc - 1), DEFAULT_WORKER_COUNT);
}