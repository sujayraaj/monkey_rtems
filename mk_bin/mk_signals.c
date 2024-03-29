/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

/*  Monkey HTTP Server
 *  ==================
 *  Copyright 2001-2015 Monkey Software LLC <eduardo@monkey.io>
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#include <monkey/monkey.h>
#include <monkey/mk_core.h>

#include <string.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>

#ifdef __rtems__
#define MK_SIGNALS_FLAGS SA_SIGINFO
#else
#define MK_SIGNALS_FLAGS SA_SIGINFO | SA_NODEFER
#endif

/* when we catch a signal and want to exit we call this function
   to do it gracefully */
static void mk_signal_exit()
{
    /* ignore future signals to properly handle the cleanup */
    signal(SIGTERM, SIG_IGN);
    signal(SIGINT,  SIG_IGN);
    signal(SIGHUP,  SIG_IGN);

    mk_user_undo_uidgid();
    mk_utils_remove_pid(mk_config->pid_file_path);
    mk_exit_all();

    mk_info("Exiting... >:(");
    _exit(EXIT_SUCCESS);
}

static void mk_signal_handler(int signo, siginfo_t *si, void *context UNUSED_PARAM)
{
    switch (signo) {
    case SIGTERM:
    case SIGINT:
        mk_signal_exit();
        break;
    case SIGHUP:
        /*
         * TODO:
         * we should implement the httpd config reload here (not in SIGUSR2).
         * Daemon processes “overload” this signal with a mechanism to instruct them to
         * reload their configuration files. Sending SIGHUP to Apache, for example,
         * instructs it to reread httpd.conf.
         */
        mk_signal_exit();
        break;
    case SIGBUS:
    case SIGSEGV:
#ifdef DEBUG
        mk_utils_stacktrace();
#endif
#ifndef __rtems__
        mk_err("%s (%d), code=%d, addr=%p",
               strsignal(signo), signo, si->si_code, si->si_addr);
#else
        mk_err("%s (%d), code=%d",
               strsignal(signo), signo, si->si_code);
#endif
        //close(sched->server_fd);
        //pthread_exit(NULL);
        abort();
    default:
        /* let the kernel handle it */
        kill(getpid(), signo);
    }

}

void mk_signal_init()
{
    struct sigaction act;
    memset(&act, 0x0, sizeof(act));

    /* allow signals to be handled concurrently */
    act.sa_flags = MK_SIGNALS_FLAGS ;
    act.sa_sigaction = &mk_signal_handler;

    sigaction(SIGSEGV, &act, NULL);
    sigaction(SIGBUS,  &act, NULL);
    sigaction(SIGHUP,  &act, NULL);
    sigaction(SIGINT,  &act, NULL);
    sigaction(SIGTERM, &act, NULL);
}
