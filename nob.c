#include <stdio.h>
#include <string.h>
#define NOB_IMPLEMENTATION
#include "nob.h"

typedef enum {
    COMMAND_BUILD,
    COMMAND_INSTALL,
} CliCommand;

typedef struct {
    CliCommand command;
    bool help;
} CliArgs;

bool parse_cli(int argc, char **argv, CliArgs *args) {
    *args = (CliArgs){0};

    int pos = 0;
    bool result = true;
    for (int i = 1; i < argc; i++) {
        char *arg = argv[i];
        if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) {
            args->help = true;
        } else if (pos == 0) {
            pos++;
            if (strcmp(arg, "install") == 0) {
                args->command = COMMAND_INSTALL;
            } else if (strcmp(arg, "build") == 0) {
                args->command = COMMAND_BUILD;
            } else {
                fprintf(stderr, "Unknown command \"%s\"\n", arg);
                result = false;
            }
        } else {
            fprintf(stderr, "Unknown argument \"%s\"\n", arg);
            result = false;
        }
    }
    return result;
}

void help(char *name) {
    printf("USAGE: %s <build|install> [-|--help]\n", name);
}

bool build(Nob_Cmd *cmd) {
    nob_log(NOB_INFO, "Building the executable...");
    nob_cc(cmd);
    nob_cc_flags(cmd);
    nob_cc_inputs(cmd, "daemon.c");
    nob_cc_output(cmd, "daemon");
    return nob_cmd_run(cmd);
}

bool install(Nob_Cmd *cmd) {
    return true;
}

int main(int argc, char **argv) {
    NOB_GO_REBUILD_URSELF(argc, argv);

    CliArgs args = {0};
    if (!parse_cli(argc, argv, &args)) {
        help(*argv);
        return 1;
    }
    if (args.help) {
        help(*argv);
        return 0;
    }

    Nob_Cmd cmd = {0};
    switch (args.command) {
    case COMMAND_BUILD:
        return !build(&cmd);
    case COMMAND_INSTALL:
        return !(build(&cmd) && install(&cmd));
    }
    return 0;
}
