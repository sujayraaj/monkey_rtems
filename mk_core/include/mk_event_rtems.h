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


#ifndef MK_EVENT_RTEMS_H
#define MK_EVENT_RTEMS_H

#include <sys/event.h>

#ifndef NOTE_SECONDS
    #define NOTE_SECONDS 0x00000001
#endif

struct mk_event_ctx {
    int kfd;
    int queue_size;
    struct kevent *events;
};

static inline int filter_mask(int16_t f)
{
    if (f == EVFILT_READ) {
        return MK_EVENT_READ;
    }

    if (f == EVFILT_WRITE) {
        return MK_EVENT_WRITE;
    }

    return 0;
}


#define mk_event_foreach(event, evl)                                    \
    int __i;                                                            \
    struct mk_event_ctx *ctx = evl->data;                               \
                                                                        \
    if (evl->n_events > 0) {                                            \
        event = ctx->events[0].udata;                                   \
    }                                                                   \
                                                                        \
    for (__i = 0;                                                       \
         __i < evl->n_events;                                           \
         __i++,                                                         \
             event = ctx->events[__i].udata                             \
         )
#endif
