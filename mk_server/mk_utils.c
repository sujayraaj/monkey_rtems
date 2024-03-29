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


/* local headers */
#include <monkey/monkey.h>
#include <monkey/mk_core.h>
#include <monkey/mk_utils.h>
#include <monkey/mk_config.h>
#include <monkey/mk_socket.h>
#include <monkey/mk_clock.h>
#include <monkey/mk_user.h>
#include <monkey/mk_cache.h>
#include <monkey/mk_tls.h>

#include <assert.h>
#include <ctype.h>
#include <stdarg.h>
#include <time.h>
#include <inttypes.h>
#include <sys/socket.h>

/* stacktrace */
#include <dlfcn.h>
#ifndef NO_BACKTRACE
#include <execinfo.h>
#endif

#define MK_UTILS_GMT_DATEFORMAT "%a, %d %b %Y %H:%M:%S GMT"

/* Date helpers */
static const char mk_date_wd[][6]  = {"Sun, ", "Mon, ", "Tue, ", "Wed, ", "Thu, ", "Fri, ", "Sat, "};
static const char mk_date_ym[][5] = {"Jan ", "Feb ", "Mar ", "Apr ", "May ", "Jun ", "Jul ",
                                     "Aug ", "Sep ", "Oct ", "Nov ", "Dec "};

#ifdef __rtems__
time_t timegm(struct tm *tm)
{
    time_t ret;
    char *tz;

    tz = getenv("TZ");
    setenv("TZ", "", 1);

    tzset();

    ret = mktime(tm);

    if (tz)
        setenv("TZ", tz, 1);
    else
        unsetenv("TZ");
    tzset();

    return ret;
}
#endif

static int mk_utils_gmt_cache_get(char **data, time_t date)
{
    unsigned int i;
    struct mk_gmt_cache *gcache = MK_TLS_GET(mk_tls_cache_gmtext);

    if (mk_unlikely(!gcache)) {
        return MK_FALSE;
    }

    for (i = 0; i < MK_GMT_CACHES; i++) {
        if (date == gcache[i].time) {
            memcpy(*data, gcache[i].text, 32);
            gcache[i].hits++;
            return MK_TRUE;
        }
    }

    return MK_FALSE;
}

static void mk_utils_gmt_cache_add(char *data, time_t time)
{
    unsigned int i, min = 0;
    struct mk_gmt_cache *gcache = MK_TLS_GET(mk_tls_cache_gmtext);

    for (i = 1; i < MK_GMT_CACHES; i++) {
        if (gcache[i].hits < gcache[min].hits)
            min = i;
    }

    gcache[min].hits = 1;
    gcache[min].time = time;
    memcpy(gcache[min].text, data, 32);
}

/*
 *This function given a unix time, set in a mk_ptr_t
 * the date in the RFC1123 format like:
 *
 *    Wed, 23 Jun 2010 22:32:01 GMT
 *
 * it also adds a 'CRLF' at the end
 */
int mk_utils_utime2gmt(char **data, time_t date)
{
    const int size = 31;
    unsigned short year, mday, hour, min, sec;
    char *buf=0;
    struct tm *gtm;

    if (date == 0) {
        if ((date = time(NULL)) == -1) {
            return -1;
        }
    }
    else {
        /* Maybe it's converted already? */
        if (mk_utils_gmt_cache_get(data, date) == MK_TRUE) {
            return size;
        }
    }

    /* Convert unix time to struct tm */
    gtm = MK_TLS_GET(mk_tls_cache_gmtime);

    /* If this function was invoked from a non-thread context it should exit */
    mk_bug(!gtm);
    gtm = gmtime_r(&date, gtm);
    if (!gtm) {
        return -1;
    }

    /* struct tm -> tm_year counts number of years after 1900 */
    year = gtm->tm_year + 1900;

    /* Signed division is slow, by using unsigned we gain 25% speed */
    mday = gtm->tm_mday;
    hour = gtm->tm_hour;
    min = gtm->tm_min;
    sec = gtm->tm_sec;

    /* Compose template */
    buf = *data;

    /* Week day */
    memcpy(buf, mk_date_wd[gtm->tm_wday], 5);
    buf += 5;

    /* Day of the month */
    *buf++ = ('0' + (mday / 10));
    *buf++ = ('0' + (mday % 10));
    *buf++ = ' ';

    /* Month */
    memcpy(buf, mk_date_ym[gtm->tm_mon], 4);
    buf += 4;

    /* Year */
    *buf++ = ('0' + (year / 1000) % 10);
    *buf++ = ('0' + (year / 100) % 10);
    *buf++ = ('0' + (year / 10) % 10);
    *buf++ = ('0' + (year % 10));
    *buf++ = ' ';

    /* Hour */
    *buf++ = ('0' + (hour / 10));
    *buf++ = ('0' + (hour % 10));
    *buf++ = ':';

    /* Minutes */
    *buf++ = ('0' + (min / 10));
    *buf++ = ('0' + (min % 10));
    *buf++ = ':';

    /* Seconds */
    *buf++ = ('0' + (sec / 10));
    *buf++ = ('0' + (sec % 10));

    /* GMT Time zone + CRLF */
    memcpy(buf, " GMT\r\n\0", 7);

    /* Add new entry to the cache */
    mk_utils_gmt_cache_add(*data, date);

    /* Set mk_ptr_t data len */
    return size;
}

time_t mk_utils_gmt2utime(char *date)
{
    time_t new_unix_time;
    struct tm t_data;
    memset(&t_data, 0, sizeof(struct tm));

    if (!strptime(date, MK_UTILS_GMT_DATEFORMAT, (struct tm *) &t_data)) {
        return -1;
    }

    new_unix_time = timegm((struct tm *) &t_data);

    return (new_unix_time);
}

int mk_buffer_cat(mk_ptr_t *p, char *buf1, int len1, char *buf2, int len2)
{
    /* Validate lengths */
    if (mk_unlikely(len1 < 0 || len2 < 0)) {
         return -1;
    }

    /* alloc space */
    p->data = (char *) mk_mem_malloc(len1 + len2 + 1);

    /* copy data */
    memcpy(p->data, buf1, len1);
    memcpy(p->data + len1, buf2, len2);
    p->data[len1 + len2] = '\0';

    /* assign len */
    p->len = len1 + len2;

    return 0;
}

/* Convert hexadecimal to int */
int mk_utils_hex2int(char *hex, int len)
{
    int i = 0;
    int res = 0;
    char c;

    while ((c = *hex++) && i < len) {
        res *= 0x10;

        if (c >= 'a' && c <= 'f') {
            res += (c - 0x57);
        }
        else if (c >= 'A' && c <= 'F') {
            res += (c - 0x37);
        }
        else if (c >= '0' && c <= '9') {
            res += (c - 0x30);
        }
        else {
            return -1;
        }
        i++;
    }

    if (res < 0) {
        return -1;
    }

    return res;
}

/* If the URI contains hexa format characters it will return
 * convert the Hexa values to ASCII character
 */
char *mk_utils_url_decode(mk_ptr_t uri)
{
    int tmp, hex_result;
    unsigned int i;
    int buf_idx = 0;
    char *buf;
    char hex[3];

    if ((tmp = mk_string_char_search(uri.data, '%', uri.len)) < 0) {
        return NULL;
    }

    i = tmp;

    buf = mk_mem_malloc_z(uri.len + 1);
    if (i > 0) {
        memcpy(buf, uri.data, i);
        buf_idx = i;
    }

    while (i < uri.len) {
        if (uri.data[i] == '%' && i + 2 < uri.len) {
            memcpy(hex, uri.data + i + 1, 2);
            hex[2] = '\0';

            hex_result = mk_utils_hex2int(hex, 2);

            if (hex_result != -1) {
                buf[buf_idx] = hex_result;
            }
            else {
                mk_mem_free(buf);
                return NULL;
            }
            i += 2;
        }
        else {
            buf[buf_idx] = uri.data[i];
        }
        i++;
        buf_idx++;
    }
    buf[buf_idx] = '\0';

    return buf;
}

#ifdef NO_BACKTRACE
void mk_utils_stacktrace(void) {}
#else
void mk_utils_stacktrace(void)
{
    unsigned int i;
    int ret;
    size_t size;
    void *arr[10];
    Dl_info d;

    printf("[stack trace]\n");
    size = backtrace(arr, 10);

    for (i = 1; i < size && i < 10; i++) {
      ret = dladdr(arr[i], &d);
      if (ret == 0 || !d.dli_sname) {
          printf(" #%i  0x%016" PRIxPTR " in \?\?\?\?\?\?\?()\n",
                 (i - 1), (uintptr_t) arr[i]);
          continue;
      }

      printf(" #%i  0x%016" PRIxPTR " in %s() from %s\n",
             (i - 1), (uintptr_t) arr[i], d.dli_sname, d.dli_fname);
    }
}
#endif



/*
 * This hash generation function is taken originally from Redis source code:
 *
 *  https://github.com/antirez/redis/blob/unstable/src/dict.c#L109
 *
 * ----
 * MurmurHash2, by Austin Appleby
 * Note - This code makes a few assumptions about how your machine behaves -
 * 1. We can read a 4-byte value from any address without crashing
 * 2. sizeof(int) == 4
 *
 * And it has a few limitations -
 *
 * 1. It will not work incrementally.
 * 2. It will not produce the same results on little-endian and big-endian
 *    machines.
 */
unsigned int mk_utils_gen_hash(const void *key, int len)
{
    /* 'm' and 'r' are mixing constants generated offline.
       They're not really 'magic', they just happen to work well.  */
    uint32_t seed = 5381;
    const uint32_t m = 0x5bd1e995;
    const int r = 24;

    /* Initialize the hash to a 'random' value */
    uint32_t h = seed ^ len;

    /* Mix 4 bytes at a time into the hash */
    const unsigned char *data = (const unsigned char *)key;

    while(len >= 4) {
        uint32_t k = *(uint32_t*) data;

        k *= m;
        k ^= k >> r;
        k *= m;

        h *= m;
        h ^= k;

        data += 4;
        len -= 4;
    }

    /* Handle the last few bytes of the input array  */
    switch(len) {
    case 3: h ^= data[2] << 16;
    case 2: h ^= data[1] << 8;
    case 1: h ^= data[0]; h *= m;
    };

    /* Do a few final mixes of the hash to ensure the last few
     * bytes are well-incorporated. */
    h ^= h >> 13;
    h *= m;
    h ^= h >> 15;

    return (unsigned int) h;
}
