#ifndef COMMAND_H
#define COMMAND_H

#include <signal.h>

/* Dispatch commands; owns the OLED-only interactive mode when selected. */
int command_run(int argc, char **argv, const volatile sig_atomic_t *stopping);

#endif
