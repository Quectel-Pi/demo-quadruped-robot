#include "command.h"

#include <signal.h>
#include <stdio.h>

static volatile sig_atomic_t stopping;

static void stop_requested(int signal_number)
{
    (void)signal_number;
    stopping = 1;
}

int main(int argc, char **argv)
{
    if (signal(SIGINT, stop_requested) == SIG_ERR ||
        signal(SIGTERM, stop_requested) == SIG_ERR) {
        perror("signal"); return 1;
    }
    return command_run(argc, argv, &stopping);
}