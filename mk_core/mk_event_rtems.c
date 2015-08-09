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

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include <sys/socket.h>

#include "mk_event.h"
#include "mk_memory.h"
#include "mk_utils.h"

#define set_nonblocking(fd) fcntl(fd, F_SETFL, fcntl(fd, F_GETFL,0) | O_NONBLOCK)
#define set_blocking(fd) fcntl(fd, F_SETFL, fcntl(fd, F_GETFL,0) & ~O_NONBLOCK)

// TODO: Working. But needs to be changed for a better alternative
int socketpair_monkey(int fd[2]){

    int listener;
    struct sockaddr sock;
    socklen_t socklen = sizeof(sock);
    int len = socklen;
    int one = 1;
    int connect_done = 0;
    fd[0] = fd[1] = listener = -1;

    memset(&sock, 0, sizeof(sock));

    if ((listener = socket(PF_INET, SOCK_STREAM, 0)) == -1)
        goto failed;

    setsockopt(listener,SOL_SOCKET,SO_REUSEADDR,(char *)&one,sizeof(one));

    if (listen(listener, 1) != 0)
        goto failed;

    if (getsockname(listener, &sock, &socklen) != 0)
        goto failed;

    if ((fd[1] = socket(PF_INET, SOCK_STREAM, 0)) == -1)
        goto failed;

    setsockopt(fd[1],SOL_SOCKET,SO_REUSEADDR,(char *)&one,sizeof(one));

    if (connect(fd[1],(struct sockaddr *)&sock,sizeof(sock)) == -1) {
        if (errno != EINPROGRESS)
            goto failed;
    } else {
        connect_done = 1;
    }

    set_nonblocking(fd[1]);

    if ((fd[0] = accept(listener, &sock, &len)) == -1)
        goto failed;

    setsockopt(fd[0],SOL_SOCKET,SO_REUSEADDR,(char *)&one,sizeof(one));
    close(listener);

    if (connect_done == 0) {
        if (connect(fd[1],(struct sockaddr *)&sock,sizeof(sock)) != 0)
            goto failed;
    }

    set_blocking(fd[1]);

    /* all OK! */
    return 0;

    failed:
        if (fd[0] != -1) close(fd[0]);
        if (fd[1] != -1) close(fd[1]);
        if (listener != -1) close(listener);
        return -1;
}

static inline void *_mk_event_loop_create(int size)
{
    struct mk_event_ctx *ctx;

    /* Main event context */
    ctx = mk_mem_malloc_z(sizeof(struct mk_event_ctx));
    if (!ctx) {
        return NULL;
    }

    /* Create the epoll instance */
    ctx->kfd = kqueue();
    if (ctx->kfd == -1) {
        mk_libc_error("kqueue");
        mk_mem_free(ctx);
        return NULL;
    }

    /* Allocate space for events queue */
    ctx->events = mk_mem_malloc_z(sizeof(struct kevent) * size);
    if (!ctx->events) {
        close(ctx->kfd);
        mk_mem_free(ctx);
        return NULL;
    }
    ctx->queue_size = size;
    return ctx;
}

/* Close handlers and memory */
static inline void _mk_event_loop_destroy(struct mk_event_ctx *ctx)
{
    close(ctx->kfd);
    mk_mem_free(ctx->events);
    mk_mem_free(ctx);
}

static inline int _mk_event_add(struct mk_event_ctx *ctx, int fd,
                                int type, uint32_t events, void *data)
{
    int ret;
    int set = MK_FALSE;
    struct mk_event *event;
    struct kevent ke = {0, 0, 0, 0, 0, 0};

    event = (struct mk_event *) data;
    if (event->mask == MK_EVENT_EMPTY) {
        event->fd   = fd;
        event->type = type;
    }

    /* Read flag */
    if ((event->mask ^ MK_EVENT_READ) && (events & MK_EVENT_READ)) {
        EV_SET(&ke, fd, EVFILT_READ, EV_ADD, 0, 0, event);
        set = MK_TRUE;
        //printf("[ADD] fd=%i READ\n", fd);
    }
    else if ((event->mask & MK_EVENT_READ) && (events ^ MK_EVENT_READ)) {
        EV_SET(&ke, fd, EVFILT_READ, EV_DELETE, 0, 0, event);
        set = MK_TRUE;
        //printf("[DEL] fd=%i READ\n", fd);
    }

    if (set == MK_TRUE) {
        ret = kevent(ctx->kfd, &ke, 1, NULL, 0, NULL);
        if (ret < 0) {
            mk_libc_error("kevent");
            return ret;
        }
    }

    /* Write flag */
    set = MK_FALSE;
    if ((event->mask ^ MK_EVENT_WRITE) && (events & MK_EVENT_WRITE)) {
        EV_SET(&ke, fd, EVFILT_WRITE, EV_ADD, 0, 0, event);
        set = MK_TRUE;
        //printf("[ADD] fd=%i WRITE\n", fd);
    }
    else if ((event->mask & MK_EVENT_WRITE) && (events ^ MK_EVENT_WRITE)) {
        EV_SET(&ke, fd, EVFILT_WRITE, EV_DELETE, 0, 0, event);
        set = MK_TRUE;
        //printf("[DEL] fd=%i WRITE\n", fd);
    }

    if (set == MK_TRUE) {
        ret = kevent(ctx->kfd, &ke, 1, NULL, 0, NULL);
        if (ret < 0) {
            mk_libc_error("kevent");
            return ret;
        }
    }

    event->mask = events;
    return 0;
}

static inline int _mk_event_del(struct mk_event_ctx *ctx, struct mk_event *event)
{
    int ret;
    struct kevent ke = {0, 0, 0, 0, 0, 0};

    if (event->mask & MK_EVENT_READ) {
        EV_SET(&ke, event->fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
        ret = kevent(ctx->kfd, &ke, 1, NULL, 0, NULL);
        if (ret < 0) {
            mk_libc_error("kevent");
            return ret;
        }
    }

    if (event->mask & MK_EVENT_WRITE) {
        EV_SET(&ke, event->fd, EVFILT_WRITE, EV_DELETE, 0, 0, NULL);
        ret = kevent(ctx->kfd, &ke, 1, NULL, 0, NULL);
        if (ret < 0) {
            mk_libc_error("kevent");
            return ret;
        }
    }

    return 0;
}

static inline int _mk_event_timeout_create(struct mk_event_ctx *ctx,
                                           int expire, void *data)
{
    int fd;
    int ret;
    struct mk_event *event;
    struct kevent ke;

    /*
     * We just need a file descriptor number, we don't care from where it
     * comes from.
     */
    fd = open("/dev/null", 0);
    if (fd == -1) {
        mk_libc_error("open");
        return -1;
    }

    event = data;
    event->fd = fd;
    event->type = MK_EVENT_NOTIFICATION;
    event->mask = MK_EVENT_EMPTY;

    EV_SET(&ke, fd, EVFILT_TIMER, EV_ADD, NOTE_SECONDS, expire, event);

    ret = kevent(ctx->kfd, &ke, 1, NULL, 0, NULL);
    if (ret < 0) {
        close(fd);
        mk_libc_error("kevent");
        return -1;
    }

    event->mask = MK_EVENT_READ;
    return fd;
}

static inline int _mk_event_channel_create(struct mk_event_ctx *ctx,
                                           int *r_fd, int *w_fd, void *data)
{
    int ret;
    int fd[2];
    struct mk_event *event;

    ret = socketpair_monkey(fd);
    if (ret < 0) {
        mk_libc_error("pipe");
        return ret;
    }

    event = data;
    event->fd = fd[0];
    event->type = MK_EVENT_NOTIFICATION;
    event->mask = MK_EVENT_EMPTY;

    ret = _mk_event_add(ctx, fd[0],
                        MK_EVENT_NOTIFICATION, MK_EVENT_READ, event);
    if (ret != 0) {
        close(fd[0]);
        close(fd[1]);
        return ret;
    }

    *r_fd = fd[0];
    *w_fd = fd[1];

    return 0;
}

static inline int _mk_event_wait(struct mk_event_loop *loop)
{
    struct mk_event_ctx *ctx = loop->data;

    loop->n_events = kevent(ctx->kfd, NULL, 0, ctx->events, ctx->queue_size, NULL);
    return loop->n_events;
}

static inline char *_mk_event_backend()
{
    return "kqueue";
}
