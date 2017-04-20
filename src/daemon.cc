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
#include <unistd.h>

#define CMD_LENGTH 8

struct arguments {
    bool stop;
    char *upgrade_file_path;
    unsigned int diag_interval;
};

bool is_daemon;

static bool do_run;
static const char socket_path[] = "/run/atrusctl/atrusctl.socket";
static const char cmd_stop[] = "stop";

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

static int
wait_for_command(int fd, unsigned int timeout)
{
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
            retval = strncmp(buffer, cmd_stop, CMD_LENGTH);
            if (retval == 0) {
                do_run = false;
            } else {
                psyslog(LOG_WARNING, "Received unknown command from socket: %s",
                        buffer);
            }
        }
    }

    return 0;
}

static void
daemon_loop(int fd, unsigned int diag_interval, char *upgrade_file_path)
{
    psyslog(LOG_INFO, "Polling diagnostics every %u s", diag_interval);

    char *upgrade_argv[] = {"upgrade", upgrade_file_path};
    int upgrade_argc = sizeof(upgrade_argv) / sizeof(upgrade_argv[0]);
    cmd_upgrade(upgrade_argc, upgrade_argv);

    char *diag_argv[] = {"diagnose"};
    int diag_argc = sizeof(diag_argv) / sizeof(diag_argv[0]);
    while (do_run) {
        cmd_diagnose(diag_argc, diag_argv);
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
            arguments->stop = true;
            break;

        case ARGP_KEY_ARG:
            if (state->arg_num >= 1) {
                argp_usage(state);
            }

            arguments->upgrade_file_path = arg;
            break;

        case ARGP_KEY_END:
            if ((state->arg_num < 1) && !arguments->stop) {
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
            {"diag-interval", 1, "s", 0, "Polling interval for diagnostics", 0},
            {"stop", 2, NULL, 0, "Stop running daemon", 0},
            {0}};
    struct argp argp = {options, parser, args_doc, doc, NULL, NULL, NULL};
    struct arguments arguments = {
            .stop = false, .upgrade_file_path = "", .diag_interval = 60};

    int retval = argp_parse(&argp, argc, argv, ARGP_IN_ORDER, NULL, &arguments);
    if (retval < 0) {
        return -1;
    }

    if (arguments.stop) {
        retval = daemon_send_cmd(cmd_stop);
        if (retval < 0) {
            if ((retval == -ENOENT) || (retval == -ECONNREFUSED)) {
                fprintf(stderr, "No deamon is running\n");
            } else {
                fprintf(stderr, "stop: %s\n", strerror(-retval));
            }
        }
    } else {
        int fd;
        retval = daemon_init(&fd);
        if (retval < 0) {
            return -1;
        }
        psyslog(LOG_INFO, "%s started", argv[0]);
        daemon_loop(fd, arguments.diag_interval, arguments.upgrade_file_path);
        psyslog(LOG_INFO, "%s stopped", argv[0]);
        daemon_exit(fd);
    }

    return 0;
}
