/*
 * Copyright 2017 Limes Audio AB. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "daemon.h"
#include "diagnose.h"
#include "upgrade.h"
#include "util.h"

#include <argp.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

#define CMD_LENGTH 16

bool is_daemon;

static bool do_run;
static bool do_diag;
static const char socket_path[] = "/run/atrusctl/atrusctl.socket";

enum cmd_key {
    cmd_unknown = -1,
    cmd_stop = 0,
    cmd_disable_diag,
    cmd_enable_diag,
    cmd_numkeys
};

struct cmd_sym {
    const char *str;
    enum cmd_key val;
};

struct arguments {
    bool stop;
    char *upgrade_file_path;
    unsigned int diag_interval;
    unsigned int ext_diag_interval;
    enum cmd_key cmd;
};

static const struct cmd_sym cmd_lookup_table[] = {
        {"stop", cmd_stop},
        {"diag_off", cmd_disable_diag},
        {"diag_on", cmd_enable_diag},
        {0}};

static int
socket_init(int *fd, struct sockaddr_un *address)
{
    *fd = socket(AF_UNIX, SOCK_RAW | SOCK_CLOEXEC, 0);
    if (*fd < 0) {
        perror("socket");
        return -1;
    }

    memset(address, 0, sizeof(*address));
    address->sun_family = AF_UNIX;
    strncpy(address->sun_path, socket_path, sizeof(address->sun_path) - 1);

    return 0;
}

static int
daemon_send_cmd(const char *cmd)
{
    int fd;
    struct sockaddr_un address;
    int retval = socket_init(&fd, &address);
    if (retval < 0) {
        return -errno;
    }
    retval = connect(fd, reinterpret_cast<struct sockaddr*>(&address),
                     sizeof(address));
    if (retval < 0) {
        return -errno;
    }

    char buffer[CMD_LENGTH];
    strncpy(buffer, cmd, CMD_LENGTH);
    retval = write(fd, buffer, CMD_LENGTH);
    if (retval < 0) {
        return -errno;
    }

    return 0;
}

static enum cmd_key
cmd_from_string(const char *str)
{
    const struct cmd_sym *sym;
    for (sym = cmd_lookup_table; (*sym).str; ++sym) {
        if (strncmp(sym->str, str, CMD_LENGTH) == 0) {
            return sym->val;
        }
    }
    return cmd_unknown;
}

static const char *
string_from_cmd(enum cmd_key cmd)
{
    const struct cmd_sym *sym;
    for (sym = cmd_lookup_table; (*sym).str; ++sym) {
        if (sym->val == cmd) {
            return sym->str;
        }
    }
    return NULL;
}

static int
wait_for_command(int fd, unsigned int timeout)
{
    enum cmd_key cmd;
    struct pollfd fds = {.fd = fd, .events = POLLIN, .revents = POLLIN};
    int retval = poll(&fds, 1, timeout * 1000);
    if (retval < 0) {
        psyslog(LOG_ERR, "poll: %m");
        if (errno != EINTR) {
            return -1;
        }
    } else if (retval > 0) {
        char buffer[CMD_LENGTH + 1];
        memset(buffer, 0, sizeof(buffer));
        retval = read(fd, buffer, CMD_LENGTH);
        if (retval < 0) {
            psyslog(LOG_ERR, "read: %m");
        } else {
            cmd = cmd_from_string(buffer);
            switch (cmd) {
                case cmd_stop:
                    do_run = false;
                    break;
                case cmd_disable_diag:
                    do_diag = false;
                    psyslog(LOG_INFO, "Diagnostics disabled");
                    break;
                case cmd_enable_diag:
                    do_diag = true;
                    psyslog(LOG_INFO, "Diagnostics enabled");
                    break;
                case cmd_numkeys:
                case cmd_unknown:
                    psyslog(LOG_WARNING,
                            "Received unknown command from socket: %s", buffer);
                    break;
            }
        }
    }

    return 0;
}

static void
daemon_loop(int fd,
            unsigned int diag_interval,
            unsigned int ext_interval,
            char *upgrade_file_path)
{
    time_t last_extended_diag = time(NULL);

    psyslog(LOG_INFO, "Polling diagnostics every %u s", diag_interval);

    char *upgrade_argv[] = {"upgrade", upgrade_file_path};
    int upgrade_argc = sizeof(upgrade_argv) / sizeof(upgrade_argv[0]);
    cmd_upgrade(upgrade_argc, upgrade_argv);

    char *diag_argv[] = {"diagnose"};
    int diag_argc = sizeof(diag_argv) / sizeof(diag_argv[0]);
    char *diag_ext_argv[] = {"diagnose", "--extended"};
    int diag_ext_argc = sizeof(diag_ext_argv) / sizeof(diag_ext_argv[0]);
    while (do_run) {
        if (do_diag) {
            time_t now = time(NULL);
            if ((now - last_extended_diag) >= ext_interval) {
                cmd_diagnose(diag_ext_argc, diag_ext_argv);
                last_extended_diag = now;
            } else {
                cmd_diagnose(diag_argc, diag_argv);
            }
        }

        int retval = wait_for_command(fd, diag_interval);
        if (retval < 0) {
            do_run = false;
        }
    }
}

static int
daemon_init(int *fd)
{
    struct sockaddr_un address;
    int retval = socket_init(fd, &address);
    if (retval < 0) {
        return -1;
    }
    retval = connect(*fd, reinterpret_cast<struct sockaddr*>(&address),
                     sizeof(address));
    if ((retval < 0) && (errno == ECONNREFUSED)) {
        fprintf(stderr, "Stale socket %s detected, removing\n", socket_path);
        retval = unlink(socket_path);
        if (retval < 0) {
            perror("unlink");
            return -1;
        }
    }
    retval = bind(*fd, reinterpret_cast<struct sockaddr*>(&address),
                  sizeof(address));
    if (retval < 0) {
        perror("bind");
        return -1;
    }

    // Daemonize
    retval = daemon(0, 0);
    if (retval < 0) {
        perror("daemon");
        return -1;
    }

    is_daemon = true;
    do_run = true;
    do_diag = true;

    openlog(NULL, 0, LOG_DAEMON);

    return 0;
}

static void
daemon_exit(int fd)
{
    int retval = close(fd);
    if (retval < 0) {
        psyslog(LOG_ERR, "close: %m");
    }
    retval = unlink(socket_path);
    if (retval < 0) {
        psyslog(LOG_ERR, "unlink: %m");
    }

    closelog();
}

static error_t
parser(int key, char *arg, struct argp_state *state)
{
    struct arguments *arguments =
        reinterpret_cast<struct arguments*>(state->input);

    switch (key) {
        case 1:
            arguments->diag_interval = (unsigned int)strtol(arg, NULL, 0);
            break;

        case 2:
            arguments->cmd = cmd_stop;
            break;

        case 3:
            arguments->cmd = cmd_disable_diag;
            break;

        case 4:
            arguments->cmd = cmd_enable_diag;
            break;

        case 5:
            arguments->ext_diag_interval =
                    static_cast<unsigned int>(strtol(arg, NULL, 0));
            break;

        case ARGP_KEY_ARG:
            if (state->arg_num >= 1) {
                argp_usage(state);
            }

            arguments->upgrade_file_path = arg;
            break;

        case ARGP_KEY_END:
            if ((state->arg_num < 1) && (arguments->cmd == cmd_unknown)) {
                argp_usage(state);
            }
            break;

        default:
            return ARGP_ERR_UNKNOWN;
    }

    return 0;
}

int
cmd_daemon(int argc, char **argv)
{
    char args_doc[] = "FILE";
    char doc[] =
            "\n"
            "Options:";
    struct argp_option options[] = {
            {"diag-interval", 1, "s", 0,
             "Polling interval for diagnostics (in seconds)", 0},
            {"stop", 2, NULL, 0, "Stop running daemon", 0},
            {"diag-disable", 3, NULL, 0, "Disable diagnostics", 0},
            {"diag-enable", 4, NULL, 0, "Enable diagnostics", 0},
            {"ext-diag-interval", 5, "s", 0,
             "Polling interval for extended diagnostics (in seconds)", 0},
            {0}};
    struct argp argp = {options, parser, args_doc, doc, NULL, NULL, NULL};
    struct arguments arguments = {.upgrade_file_path = "",
                                  .diag_interval = 60,
                                  .ext_diag_interval = 1800,
                                  .cmd = cmd_unknown};

    int retval = argp_parse(&argp, argc, argv, ARGP_IN_ORDER, NULL, &arguments);
    if (retval < 0) {
        return -1;
    }

    int fd;
    const char *cmd;
    switch (arguments.cmd) {
        case cmd_stop:
        case cmd_enable_diag:
        case cmd_disable_diag:
            cmd = string_from_cmd(arguments.cmd);
            if (!cmd) {
                return -1;
            }
            retval = daemon_send_cmd(cmd);
            if (retval < 0) {
                if ((retval == -ENOENT) || (retval == -ECONNREFUSED)) {
                    fprintf(stderr, "No deamon is running\n");
                } else {
                    fprintf(stderr, "stop: %s\n", strerror(-retval));
                }
            }
            break;
        default:
            retval = daemon_init(&fd);
            if (retval < 0) {
                return -1;
            }
            psyslog(LOG_INFO, "%s started", argv[0]);
            daemon_loop(fd, arguments.diag_interval,
                        arguments.ext_diag_interval,
                        arguments.upgrade_file_path);
            psyslog(LOG_INFO, "%s stopped", argv[0]);
            daemon_exit(fd);
            break;
    }
    return 0;
}
