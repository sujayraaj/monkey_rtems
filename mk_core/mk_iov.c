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

#define _GNU_SOURCE
#include <fcntl.h>

#include <sys/uio.h>
#include <sys/mman.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "mk_macros.h"
#include "mk_memory.h"
#include "mk_iov.h"

const mk_ptr_t mk_iov_crlf = mk_ptr_init(MK_IOV_CRLF);
const mk_ptr_t mk_iov_lf = mk_ptr_init(MK_IOV_LF);
const mk_ptr_t mk_iov_space = mk_ptr_init(MK_IOV_SPACE);
const mk_ptr_t mk_iov_slash = mk_ptr_init(MK_IOV_SLASH);
const mk_ptr_t mk_iov_none = mk_ptr_init(MK_IOV_NONE);
const mk_ptr_t mk_iov_equal = mk_ptr_init(MK_IOV_EQUAL);

#ifdef __rtems__
ssize_t rtems_writev(int fd, const struct iovec *vector, int count)
{
    char *buffer;
    char *bp;
    size_t bytes, to_copy, copied;
    size_t i;
    /* Find the total number of bytes to be written. */
    bytes = 0;
    for (i = 0; i < count; ++i)
        bytes += vector[i].iov_len;
    /* Allocate a temporary buffer to hold the data. */
    buffer = (char *) malloc(bytes);
    /* Copy the data into BUFFER. */
    to_copy = bytes;
    bp = buffer;
    for (i = 0; i < count; ++i)
    {
        #define	min(a, b) ((a) > (b) ? (b) : (a))
        size_t copy = min(vector[i].iov_len, to_copy);
        (void) memcpy( bp, vector[i].iov_base, copy);
        bp += copy;
        to_copy -= copy;
        if (bytes == 0)
            break;
    }
    copied = write(fd, buffer, bytes);
    if(buffer)
        free(buffer);
    return copied;
}
#endif


struct mk_iov *mk_iov_create(int n, int offset)
{
    int s_all;
    int s_iovec;
    int s_free_buf;
    void *p;
    struct mk_iov *iov;

    s_all      = sizeof(struct mk_iov);       /* main mk_iov structure */
    s_iovec    = (n * sizeof(struct iovec));  /* iovec array size      */
    s_free_buf = (n * sizeof(void *));        /* free buf array        */

    p = mk_mem_malloc_z(s_all + s_iovec + s_free_buf);
    if (!p) {
        return NULL;
    }

    /* Set pointer address */
    iov     = p;
    iov->io = p + sizeof(struct mk_iov);
    iov->buf_to_free = (void *) (p + sizeof(struct mk_iov) + s_iovec);

    mk_iov_init(iov, n, offset);
    return iov;
}

struct mk_iov *mk_iov_realloc(struct mk_iov *mk_io, int new_size)
{
    int i;
    struct mk_iov *iov;

    /*
     * We do not perform a memory realloc because our struct iov have
     * self references on it 'io' and 'buf_to_free' pointers. So we create a
     * new mk_iov and perform a data migration.
     */
    iov = mk_iov_create(new_size, 0);
    if (!iov) {
        return NULL;
    }

    /* Migrate data */
    iov->iov_idx   = mk_io->iov_idx;
    iov->buf_idx   = mk_io->buf_idx;
    iov->size      = new_size;
    iov->total_len = mk_io->total_len;

    for (i = 0; i < mk_io->iov_idx; i++) {
        iov->io[i].iov_base = mk_io->io[i].iov_base;
        iov->io[i].iov_len  = mk_io->io[i].iov_len;
    }

    for (i = 0; i < mk_io->buf_idx; i++) {
        iov->buf_to_free[i] = mk_io->buf_to_free[i];
    }

    return iov;
}

int mk_iov_set_entry(struct mk_iov *mk_io, void *buf, int len,
                     int free, int idx)
{
    mk_io->io[idx].iov_base = buf;
    mk_io->io[idx].iov_len = len;
    mk_io->total_len += len;

    if (free == MK_TRUE) {
        _mk_iov_set_free(mk_io, buf);
    }

    return 0;
}

ssize_t mk_iov_send(int fd, struct mk_iov *mk_io)
{
#ifdef __rtems__
    ssize_t n = rtems_writev(fd, mk_io->io, mk_io->iov_idx);
#else
    ssize_t n = writev(fd, mk_io->io, mk_io->iov_idx);
#endif
    if (mk_unlikely(n < 0)) {
        MK_TRACE( "[FD %i] writev() '%s'", fd, strerror(errno));
        return -1;
    }

    return n;
}

void mk_iov_free(struct mk_iov *mk_io)
{
    mk_iov_free_marked(mk_io);
    mk_mem_free(mk_io);
}

void mk_iov_free_marked(struct mk_iov *mk_io)
{
    int i, limit = 0;

    limit = mk_io->buf_idx;

    for (i = 0; i < limit; i++) {

#ifdef DEBUG_IOV
        printf("\nDEBUG IOV :: going free (idx: %i/%i): %s", i,
               limit, mk_io->buf_to_free[i]);
        fflush(stdout);
#endif
        mk_mem_free(mk_io->buf_to_free[i]);
    }

    mk_io->iov_idx = 0;
    mk_io->buf_idx = 0;
    mk_io->total_len = 0;
}

void mk_iov_print(struct mk_iov *mk_io)
{
    int i;
    unsigned j;
    char *c;

    for (i = 0; i < mk_io->iov_idx; i++) {
        printf("\n[index=%i len=%i]\n'", i, (int) mk_io->io[i].iov_len);
        fflush(stdout);

        for (j=0; j < mk_io->io[i].iov_len; j++) {
            c = mk_io->io[i].iov_base;
            printf("%c", c[j]);
            fflush(stdout);
        }
        printf("'[end=%i]\n", j);
        fflush(stdout);
    }
}

int mk_iov_consume(struct mk_iov *mk_io, size_t bytes)
{
    int idx;
    size_t len;

    if (mk_io->total_len == bytes) {
        mk_io->total_len = 0;
        mk_io->iov_idx   = 0;
        return 0;
    }

    for (idx = 0; idx < mk_io->iov_idx; idx++) {
        len = mk_io->io[idx].iov_len;

        if (bytes < len) {
            mk_io->io[idx].iov_base += bytes;
            mk_io->io[idx].iov_len   = (len - bytes);
            break;
        }
        else if (bytes == len) {
            /* this entry was consumed */
            mk_io->io[idx].iov_len = 0;
            break;
        }
        else if (bytes > len) {
            mk_io->io[idx].iov_len = 0;
            bytes -= len;
        }
    }

    mk_io->total_len -= bytes;
    return 0;
}
