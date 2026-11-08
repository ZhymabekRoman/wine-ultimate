/*
 * Server main function
 *
 * Copyright (C) 1998 Alexandre Julliard
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

#include "config.h"

#include <assert.h>
#include <ctype.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <unistd.h>
#include <errno.h>
#ifdef HAVE_SYS_RESOURCE_H
# include <sys/resource.h>
#endif
#ifdef linux
# include <sched.h>
#endif
#ifdef HAVE_SYS_SYSCTL_H
# include <sys/sysctl.h>
#endif

#include "object.h"
#include "file.h"
#include "thread.h"
#include "request.h"
#include "unicode.h"

/* command-line options */
int debug_level = 0;
int foreground = 0;
timeout_t master_socket_timeout = 3 * -TICKS_PER_SEC;  /* master socket timeout, default is 3 seconds */
const char *server_argv0;

/* parse-line args */

static void usage( FILE *fh )
{
    fprintf(fh, "Usage: %s [options]\n\n", server_argv0);
    fprintf(fh, "Options:\n");
    fprintf(fh, "   -d[n], --debug[=n]       set debug level to n or +1 if n not specified\n");
    fprintf(fh, "   -f,    --foreground      remain in the foreground for debugging\n");
    fprintf(fh, "   -h,    --help            display this help message\n");
    fprintf(fh, "   -k[n], --kill[=n]        kill the current wineserver, optionally with signal n\n");
    fprintf(fh, "   -p[n], --persistent[=n]  make server persistent, optionally for n seconds\n");
    fprintf(fh, "   -v,    --version         display version information and exit\n");
    fprintf(fh, "   -w,    --wait            wait until the current wineserver terminates\n");
    fprintf(fh, "\n");
}

static void option_callback( int optc, char *optarg )
{
    int ret;

    switch (optc)
    {
    case 'd':
        if (optarg && isdigit(*optarg))
            debug_level = atoi( optarg );
        else
            debug_level++;
        break;
    case 'f':
        foreground = 1;
        break;
    case 'h':
        usage(stdout);
        exit(0);
        break;
    case 'k':
        if (optarg && isdigit(*optarg))
            ret = kill_lock_owner( atoi( optarg ) );
        else
            ret = kill_lock_owner(-1);
        exit( !ret );
    case 'p':
        if (optarg && isdigit(*optarg))
            master_socket_timeout = (timeout_t)atoi( optarg ) * -TICKS_PER_SEC;
        else
            master_socket_timeout = TIMEOUT_INFINITE;
        break;
    case 'v':
        fprintf( stderr, "%s\n", PACKAGE_STRING );
        exit(0);
    case 'w':
        wait_for_lock();
        exit(0);
    }
}

/* command-line option parsing */
/* partly based on the GLibc getopt() implementation */

static struct long_option
{
    const char *name;
    int has_arg;
    int val;
} long_options[] =
{
    {"debug",       2, 'd'},
    {"foreground",  0, 'f'},
    {"help",        0, 'h'},
    {"kill",        2, 'k'},
    {"persistent",  2, 'p'},
    {"version",     0, 'v'},
    {"wait",        0, 'w'},
    { NULL }
};

static void parse_options( int argc, char **argv, const char *short_opts,
                           const struct long_option *long_opts, void (*callback)( int, char* ) )
{
    const char *flag;
    char *start, *end;
    int i;

    for (i = 1; i < argc; i++)
    {
        if (argv[i][0] != '-' || !argv[i][1])  /* not an option */
            continue;
        if (!strcmp( argv[i], "--" ))
            break;
        start = argv[i] + 1 + (argv[i][1] == '-');

        if (argv[i][1] == '-')
        {
            /* handle long option */
            const struct long_option *opt, *found = NULL;
            int count = 0;

            if (!(end = strchr( start, '=' ))) end = start + strlen(start);
            for (opt = long_opts; opt && opt->name; opt++)
            {
                if (strncmp( opt->name, start, end - start )) continue;
                if (!opt->name[end - start])  /* exact match */
                {
                    found = opt;
                    count = 1;
                    break;
                }
                if (!found)
                {
                    found = opt;
                    count++;
                }
                else if (found->has_arg != opt->has_arg || found->val != opt->val)
                {
                    count++;
                }
            }

            if (count > 1) goto error;

            if (found)
            {
                if (*end)
                {
                    if (!found->has_arg) goto error;
                    end++;  /* skip '=' */
                }
                else if (found->has_arg == 1)
                {
                    if (i == argc - 1) goto error;
                    end = argv[++i];
                }
                else end = NULL;

                callback( found->val, end );
                continue;
            }
            goto error;
        }

        /* handle short option */
        for ( ; *start; start++)
        {
            if (!(flag = strchr( short_opts, *start ))) goto error;
            if (flag[1] == ':')
            {
                end = start + 1;
                if (!*end) end = NULL;
                if (flag[2] != ':' && !end)
                {
                    if (i == argc - 1) goto error;
                    end = argv[++i];
                }
                callback( *start, end );
                break;
            }
            callback( *start, NULL );
        }
    }
    return;

error:
    usage( stderr );
    exit(1);
}

static void sigterm_handler( int signum )
{
    exit(1);  /* make sure atexit functions get called */
}

static void init_limits(void)
{
#ifdef RLIMIT_NOFILE
    struct rlimit rlimit;

    if (!getrlimit( RLIMIT_NOFILE, &rlimit ))
    {
        rlimit.rlim_cur = rlimit.rlim_max;
        if (!setrlimit( RLIMIT_NOFILE, &rlimit )) return;
#ifdef __APPLE__
        {
            /* macOS before Big Sur fails if rlim_max is larger than maxfilesperproc */
            unsigned int nlimit = 0;
            size_t size = sizeof(nlimit);
            sysctlbyname("kern.maxfilesperproc", &nlimit, &size, NULL, 0);
            rlimit.rlim_cur = max( nlimit, OPEN_MAX );
            setrlimit( RLIMIT_NOFILE, &rlimit );
        }
#endif
    }
#endif
}

/*
 * P5: opt-in CPU scheduling knobs for the server. Nothing is touched unless the
 * matching environment variable is set in the environment that starts wineserver:
 *
 *   WINE_SERVER_NICE=<int>      setpriority(PRIO_PROCESS, 0, n) on self. The
 *                               non-root floor is -RLIMIT_NICE (ulimit -e).
 *   WINE_SERVER_AFFINITY=<hex>  sched_setaffinity(0, mask) on self; no privilege needed.
 *   WINE_SERVER_RT=<1..99>      sched_setscheduler( SCHED_FIFO ) on self; requires
 *                               CAP_SYS_NICE / RLIMIT_RTPRIO. On EPERM log a fixme and
 *                               fall back to setpriority(-19). Never applied when unset.
 *
 * Rationale: the server is single-threaded; under CPU load (parallel builds) the
 * scheduler starves it and every wine client startup stalls behind the hogs. RT is
 * deliberately opt-in and per-process (it affects only this wineserver, which blocks
 * between requests); the system-wide safety net for RT tasks is the RT bandwidth
 * cap in /proc/sys/kernel/sched_rt_runtime_us.
 */
static void set_scheduling_nice( int n )
{
#ifdef HAVE_SYS_RESOURCE_H
    if (setpriority( PRIO_PROCESS, 0, n ) == -1)
        fprintf( stderr, "wine: setpriority(%d) failed, error %d (check RLIMIT_NICE, ulimit -e)\n", n, errno );
#else
    (void)n;
#endif
}

static void set_scheduling_priority(void)
{
    const char *str;

    if ((str = getenv( "WINE_SERVER_NICE" )))
        set_scheduling_nice( atoi( str ) );
#ifdef linux
    if ((str = getenv( "WINE_SERVER_AFFINITY" )))
    {
        cpu_set_t set;
        unsigned long mask = strtoul( str, NULL, 16 ), bit;

        CPU_ZERO( &set );
        for (bit = 0; mask; bit++, mask >>= 1)
            if (mask & 1) CPU_SET( bit, &set );
        if (sched_setaffinity( 0, sizeof(set), &set ) == -1)
            fprintf( stderr, "wine: WINE_SERVER_AFFINITY=%s: sched_setaffinity failed, error %d\n", str, errno );
    }

    if ((str = getenv( "WINE_SERVER_RT" )))
    {
        struct sched_param param;
        param.sched_priority = atoi( str );
        if (param.sched_priority < 1 || param.sched_priority > 99)
            fprintf( stderr, "wine: WINE_SERVER_RT=%s: invalid priority, must be 1..99, ignored\n", str );
        else if (sched_setscheduler( 0, SCHED_FIFO, &param ) == -1)
        {
            fprintf( stderr, "fixme: WINE_SERVER_RT=%d: SCHED_FIFO denied, error %d, falling back to nice -19\n",
                     param.sched_priority, errno );
            set_scheduling_nice( -19 );  /* may fail too without privileges */
        }
    }
#endif
}

int main( int argc, char *argv[] )
{
    setvbuf( stderr, NULL, _IOLBF, 0 );
    extern void init_image_ranges( void );
    init_image_ranges();
    server_argv0 = argv[0];
    parse_options( argc, argv, "d::fhk::p::vw", long_options, option_callback );

    /* setup temporary handlers before the real signal initialization is done */
    signal( SIGPIPE, SIG_IGN );
    signal( SIGHUP, sigterm_handler );
    signal( SIGINT, sigterm_handler );
    signal( SIGQUIT, sigterm_handler );
    signal( SIGTERM, sigterm_handler );
    signal( SIGABRT, sigterm_handler );
    init_limits();
    set_scheduling_priority();

    sock_init();
    open_master_socket();

    if (debug_level) fprintf( stderr, "wineserver: starting (pid=%ld)\n", (long) getpid() );
    set_current_time();
    init_signals();
    init_memory();
    init_directories( load_intl_file() );
    init_threading();
    init_registry();
    main_loop();
    return 0;
}
