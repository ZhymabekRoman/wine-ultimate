/*
 * Ntdll lock-free in-memory Flight Recorder
 *
 * Copyright 2026 Wine Staging Contributors
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#if 0
#pragma makedep unix
#endif

#include "config.h"

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "ntstatus.h"
#include "windef.h"
#include "winnt.h"
#include "winternl.h"
#include "unix_private.h"

extern pid_t wine_cached_unix_pid(void);

#define FLIGHT_RECORDER_RING_BYTES (4 * 1024 * 1024)
#define FLIGHT_RECORDER_TEXT_LEN   232
#define FLIGHT_RECORDER_DUMP_TAIL  1000

struct flight_recorder_entry
{
    UINT64 seq;          /* 1-based monotonic sequence once committed; 0 while writing */
    UINT64 timestamp_ns; /* CLOCK_MONOTONIC nanoseconds */
    DWORD  pid;
    DWORD  tid;
    char   text[FLIGHT_RECORDER_TEXT_LEN];
};

#define FLIGHT_RECORDER_SLOTS (FLIGHT_RECORDER_RING_BYTES / sizeof(struct flight_recorder_entry))

C_ASSERT( sizeof(struct flight_recorder_entry) == 256 );
C_ASSERT( FLIGHT_RECORDER_SLOTS == 16384 );
C_ASSERT( FLIGHT_RECORDER_SLOTS * sizeof(struct flight_recorder_entry) == FLIGHT_RECORDER_RING_BYTES );

static struct flight_recorder_entry flight_ring[FLIGHT_RECORDER_SLOTS];
static LONGLONG volatile flight_write_idx;

static inline LONGLONG interlocked_xchg_add( LONGLONG volatile *dest, LONGLONG incr )
{
    return InterlockedExchangeAdd64( dest, incr );
}

static inline UINT64 flight_now_ns(void)
{
    struct timespec ts;
    if (clock_gettime( CLOCK_MONOTONIC, &ts ) != 0) return 0;
    return (UINT64)ts.tv_sec * 1000000000ULL + (UINT64)ts.tv_nsec;
}

static void flight_write_all( int fd, const char *buf, size_t len )
{
    while (len > 0)
    {
        ssize_t n = write( fd, buf, len );
        if (n < 0)
        {
            if (errno == EINTR) continue;
            break;
        }
        buf += n;
        len -= (size_t)n;
    }
}

void flight_recorder_vlog( const char *fmt, va_list ap )
{
    UINT64 now_ns = flight_now_ns();
    LONGLONG idx = interlocked_xchg_add( &flight_write_idx, 1 );
    UINT64 seq = (UINT64)idx + 1;
    struct flight_recorder_entry *e = &flight_ring[(UINT64)idx & (FLIGHT_RECORDER_SLOTS - 1)];
    struct thread_data *tdata = get_thread_data();
    int n;
    size_t len, i;

    __atomic_store_n( &e->seq, 0, __ATOMIC_RELEASE );
    e->timestamp_ns = now_ns;
    e->pid = pid ? pid : (DWORD)wine_cached_unix_pid();
    e->tid = tdata ? tdata->tid : 0;

    n = vsnprintf( e->text, sizeof(e->text), fmt, ap );
    if (n < 0)
    {
        e->text[0] = 0;
    }
    else
    {
        len = ((size_t)n >= sizeof(e->text)) ? (sizeof(e->text) - 1) : (size_t)n;
        while (len > 0 && (e->text[len - 1] == '\n' || e->text[len - 1] == '\r'))
            e->text[--len] = 0;
        for (i = 0; i < len; i++)
            if (e->text[i] == '\n' || e->text[i] == '\r') e->text[i] = ' ';
        e->text[sizeof(e->text) - 1] = 0;
    }

    __atomic_store_n( &e->seq, seq, __ATOMIC_RELEASE );
}

void flight_recorder_log( const char *fmt, ... )
{
    va_list ap;

    if (!fmt) return;
    va_start( ap, fmt );
    flight_recorder_vlog( fmt, ap );
    va_end( ap );
}

static void flight_record_one_line( const char *line, size_t len, UINT64 now_ns, DWORD cur_pid, DWORD cur_tid )
{
    LONGLONG idx;
    UINT64 seq;
    struct flight_recorder_entry *e;
    size_t copy_len, i;

    while (len > 0 && (line[len - 1] == '\r' || line[len - 1] == '\n')) len--;
    if (!len) return;

    idx = interlocked_xchg_add( &flight_write_idx, 1 );
    seq = (UINT64)idx + 1;
    e = &flight_ring[(UINT64)idx & (FLIGHT_RECORDER_SLOTS - 1)];

    __atomic_store_n( &e->seq, 0, __ATOMIC_RELEASE );
    e->timestamp_ns = now_ns;
    e->pid = cur_pid;
    e->tid = cur_tid;

    copy_len = (len >= sizeof(e->text)) ? (sizeof(e->text) - 1) : len;
    memcpy( e->text, line, copy_len );
    for (i = 0; i < copy_len; i++)
        if (e->text[i] == '\r' || e->text[i] == '\n') e->text[i] = ' ';
    e->text[copy_len] = 0;

    __atomic_store_n( &e->seq, seq, __ATOMIC_RELEASE );
}

void flight_recorder_log_raw( const char *str, unsigned int len )
{
    UINT64 now_ns;
    struct thread_data *tdata;
    DWORD cur_pid, cur_tid;
    const char *p, *end, *nl;

    if (!str || !len) return;

    now_ns = flight_now_ns();
    tdata = get_thread_data();
    cur_pid = pid ? pid : (DWORD)wine_cached_unix_pid();
    cur_tid = tdata ? tdata->tid : 0;

    p = str;
    end = str + len;
    while (p < end)
    {
        nl = memchr( p, '\n', (size_t)(end - p) );
        if (nl)
        {
            flight_record_one_line( p, (size_t)(nl - p), now_ns, cur_pid, cur_tid );
            p = nl + 1;
        }
        else
        {
            flight_record_one_line( p, (size_t)(end - p), now_ns, cur_pid, cur_tid );
            break;
        }
    }
}

void flight_recorder_dump( const char *path )
{
    char fallback_path[256];
    char out_buf[4096];
    size_t out_pos = 0;
    LONGLONG total;
    UINT64 end_seq, count, start_seq, s;
    DWORD cur_pid;
    int fd;
    int hdr_len;

    if (!path || !path[0])
    {
        cur_pid = pid ? pid : (DWORD)wine_cached_unix_pid();
        snprintf( fallback_path, sizeof(fallback_path), "/var/tmp/wa-lanes/dumps/wine_%u_trace.txt",
                  (unsigned int)cur_pid );
        path = fallback_path;
    }

    mkdir( "/var/tmp/wa-lanes", 0755 );
    mkdir( "/var/tmp/wa-lanes/dumps", 0755 );

    fd = open( path, O_CREAT | O_TRUNC | O_WRONLY | O_CLOEXEC, 0644 );
    if (fd < 0) return;

    total = __atomic_load_n( &flight_write_idx, __ATOMIC_ACQUIRE );
    end_seq = (total > 0) ? (UINT64)total : 0;
    count = end_seq < FLIGHT_RECORDER_DUMP_TAIL ? end_seq : FLIGHT_RECORDER_DUMP_TAIL;
    if (count > FLIGHT_RECORDER_SLOTS) count = FLIGHT_RECORDER_SLOTS;
    start_seq = end_seq - count;

    cur_pid = pid ? pid : (DWORD)wine_cached_unix_pid();
    hdr_len = snprintf( out_buf, sizeof(out_buf),
                        "# Wine Flight Recorder trace (pid=%u total_events=%llu dumped=%llu)\n",
                        (unsigned int)cur_pid, (unsigned long long)end_seq, (unsigned long long)count );
    if (hdr_len > 0)
        out_pos = ((size_t)hdr_len < sizeof(out_buf)) ? (size_t)hdr_len : sizeof(out_buf) - 1;

    for (s = start_seq; s < end_seq; s++)
    {
        struct flight_recorder_entry *slot = &flight_ring[s & (FLIGHT_RECORDER_SLOTS - 1)];
        struct flight_recorder_entry copy;
        UINT64 seq1, seq2;
        UINT64 secs, usecs;
        int n;

        seq1 = __atomic_load_n( &slot->seq, __ATOMIC_ACQUIRE );
        if (seq1 != s + 1) continue;

        memcpy( &copy, slot, sizeof(copy) );
        seq2 = __atomic_load_n( &slot->seq, __ATOMIC_ACQUIRE );
        if (seq2 != seq1) continue;

        copy.text[sizeof(copy.text) - 1] = 0;
        secs = copy.timestamp_ns / 1000000000ULL;
        usecs = (copy.timestamp_ns % 1000000000ULL) / 1000ULL;

        if (sizeof(out_buf) - out_pos < 320)
        {
            flight_write_all( fd, out_buf, out_pos );
            out_pos = 0;
        }

        n = snprintf( out_buf + out_pos, sizeof(out_buf) - out_pos,
                      "[%6llu.%06llu] #%llu pid=%04x tid=%04x %s\n",
                      (unsigned long long)secs, (unsigned long long)usecs,
                      (unsigned long long)seq1,
                      (unsigned int)copy.pid, (unsigned int)copy.tid, copy.text );
        if (n > 0)
        {
            size_t adv = (size_t)n;
            if (adv >= sizeof(out_buf) - out_pos) adv = sizeof(out_buf) - out_pos - 1;
            out_pos += adv;
        }
    }

    if (out_pos > 0) flight_write_all( fd, out_buf, out_pos );
    close( fd );
}

NTSTATUS unixcall_flight_recorder_log( void *args )
{
    struct wine_dbg_write_params *params = args;

    if (params && params->str && params->len)
        flight_recorder_log_raw( params->str, params->len );
    return STATUS_SUCCESS;
}

#ifdef _WIN64
NTSTATUS wow64_flight_recorder_log( void *args )
{
    struct
    {
        ULONG        str;
        unsigned int len;
    } const *params32 = args;

    if (params32 && params32->str && params32->len)
        flight_recorder_log_raw( ULongToPtr(params32->str), params32->len );
    return STATUS_SUCCESS;
}
#endif
