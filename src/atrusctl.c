/*
 * Copyright 2017 Limes Audio AB. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "daemon.h"
#include "diagnose.h"
#include "list.h"
#include "upgrade.h"

#include <argp.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

struct arguments {
    int (*func)(int argc, char **argv);
    int argc;
    char **argv;
    bool debug;
};

bool is_debug;

static void
cmd_prepare(struct argp_state *state, struct arguments *arguments)
{
    if (state->next <= 0) {
        argp_error(state, "unable to parse command line argument");
        return;
    }

    int argc = state->argc - state->next + 1;
    char **argv = &state->argv[state->next - 1];

    char *cmd_name = argv[0];
    argv[0] = malloc(strlen(state->name) + 1 + strlen(cmd_name) + 1);
    if (!argv[0]) {
        argp_failure(state, 1, ENOMEM, NULL);
    }
    sprintf(argv[0], "%s %s", state->name, cmd_name);

    arguments->argc = argc;
    arguments->argv = argv;
}

static error_t
parser(int key, char *arg, struct argp_state *state)
{
    struct arguments *arguments = state->input;

    switch (key) {
        case 1:
            arguments->debug = true;
            break;

        case ARGP_KEY_ARG:
            if (state->arg_num == 0) {
                if (strcmp(arg, "daemon") == 0) {
                    arguments->func = cmd_daemon;
                } else if (strcmp(arg, "diagnose") == 0) {
                    arguments->func = cmd_diagnose;
                } else if (strcmp(arg, "list") == 0) {
                    arguments->func = cmd_list;
                } else if (strcmp(arg, "upgrade") == 0) {
                    arguments->func = cmd_upgrade;
                }

                if (arguments->func) {
                    cmd_prepare(state, arguments);
                    return ARGP_KEY_SUCCESS;
                }

                argp_error(state, "%s is not a valid command", arg);
            }
            break;

        case ARGP_KEY_NO_ARGS:
            argp_state_help(state, stdout, ARGP_HELP_STD_HELP);
            break;

        default:
            return ARGP_ERR_UNKNOWN;
    }

    return 0;
}

int
main(int argc, char **argv)
{
    char args_doc[] = "[COMMAND]";
    char doc[] =
            "\n"
            "Options:"
            "\v"
            "Commands:\n"
            "\n"
            "  daemon                     Run as a daemon\n"
            "  diagnose                   Show diagnostic information\n"
            "  list                       List info about attached devices\n"
            "  upgrade                    Firmware upgrade";
    struct argp_option options[] = {{"debug", 1, NULL, 0, "Debug output", 0},
                                    {0}};
    struct argp argp = {options, parser, args_doc, doc, NULL, NULL, NULL};
    struct arguments arguments = {0};

    int retval = argp_parse(&argp, argc, argv, ARGP_IN_ORDER, NULL, &arguments);
    if (retval < 0) {
        return 1;
    }

    is_debug = arguments.debug;

    retval = arguments.func(arguments.argc, arguments.argv);
    free(arguments.argv[0]);
    if (retval < 0) {
        return 1;
    }

    return 0;
}
