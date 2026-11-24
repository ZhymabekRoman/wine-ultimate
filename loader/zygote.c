/*
 * Persistent prefix-wide zygote (fork server) for Wine.
 *
 * A single long-lived process per Wine prefix that has the ELF loader side of
 * Wine (this binary) plus ntdll.so / win32u.so already mapped.  Every Wine
 * process that needs to spawn a child connects to <server_dir>/zygote.sock,
 * hands over argv / envp / cwd / stdio / wineserver socket file descriptors
 * with SCM_RIGHTS, and the zygote fork()s a child that jumps straight into
 * __wine_main() of the preloaded ntdll.so.
 *
 * What the child saves compared to the classic fork()+execv(wineloader):
 *   - 0 execve() (loader + preloader execs)
 *   - 0 ld.so startup and 0 dlopen() of ntdll.so / win32u.so (relocations of
 *     the ELF images are inherited through the fork)
 *   - warm page cache for ntdll.dll / win32u.dll / kernel32.dll / kernelbase.dll
 *
 * Copyright 2026 Wine project
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

#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#ifdef HAVE_SYS_SYSCTL_H
# include <sys/sysctl.h>
#endif
#ifdef __APPLE__
# include <mach-o/dyld.h>
#endif

#define ZYGOTE_MAGIC   0x5a475946  /* 'F','Y','G','Z' */
#define ZYGOTE_VERSION 2

/* request flags, must match dlls/ntdll/unix/process.c */
#define ZYGOTE_FLAG_DETACHED   0x01  /* setsid() + /dev/null stdio */
#define ZYGOTE_FLAG_UNIX       0x02  /* execv() a Unix binary instead of __wine_main() */
#define ZYGOTE_FLAG_HAS_SOCKET 0x04  /* wineserver connection fd attached */
#define ZYGOTE_FLAG_HAS_CWD    0x08  /* unix cwd dir fd attached */
#define ZYGOTE_FLAG_HAS_STDIN  0x10  /* stdin fd attached */
#define ZYGOTE_FLAG_HAS_STDOUT 0x20  /* stdout fd attached */
#define ZYGOTE_FLAG_HAS_STDERR 0x40  /* stderr fd attached */

#define ZYGOTE_STATUS_OK     0
#define ZYGOTE_STATUS_ERR   -1
#define ZYGOTE_STATUS_STALE -2

struct zygote_request
{
    uint32_t magic;            /* ZYGOTE_MAGIC */
    uint32_t version;          /* ZYGOTE_VERSION */
    uint32_t server_protocol;  /* SERVER_PROTOCOL_VERSION */
    uint32_t flags;            /* ZYGOTE_FLAG_* */
    uint64_t bin_fingerprint;  /* FNV-1a hash of wine-zygote + ntdll.so + win32u.so stat */
    uint32_t argv_len;         /* size of NUL separated argv block (terminated by empty string) */
    uint32_t env_len;          /* size of NUL separated envp block (terminated by empty string) */
    uint32_t reserve_len;      /* size of WINEPRELOADRESERVE value, including NUL */
    uint32_t winedebug_len;    /* size of WINEDEBUG value, including NUL */
};

struct zygote_reply
{
    int32_t status;  /* ZYGOTE_STATUS_* */
    int32_t pid;     /* Unix pid of the forked child, -1 on error */
};

#define ZYGOTE_MAX_FDS 5

struct server_watch
{
    int   active;
    char *sock_path;
    char *lock_path;
    dev_t sock_dev;
    ino_t sock_ino;
    pid_t server_pid;
};

struct zygote_state
{
    char               *zygote_bin;
    char               *ntdll_bin;
    char               *win32u_bin;
    uint32_t            server_protocol;
    uint64_t            bin_fingerprint;
    struct server_watch watch;
};

static volatile sig_atomic_t quit_requested;

/* canonicalize path and return its directory name */
static char *realpath_dirname( const char *name )
{
    char *p, *fullpath = realpath( name, NULL );

    if (fullpath)
    {
        p = strrchr( fullpath, '/' );
        if (p == fullpath) p++;
        if (p) *p = 0;
    }
    return fullpath;
}

static char *remove_tail( const char *str, const char *tail )
{
    size_t len = strlen( str );
    size_t tail_len = strlen( tail );
    char *ret;

    if (len < tail_len) return NULL;
    if (strcmp( str + len - tail_len, tail )) return NULL;
    ret = malloc( len - tail_len + 1 );
    memcpy( ret, str, len - tail_len );
    ret[len - tail_len] = 0;
    return ret;
}

static char *build_path( const char *dir, const char *name )
{
    size_t len = strlen( dir );
    char *ret = malloc( len + strlen( name ) + 2 );

    memcpy( ret, dir, len );
    if (len && ret[len - 1] != '/') ret[len++] = '/';
    strcpy( ret + len, name );
    return ret;
}

static const char *get_self_exe( void )
{
#if defined(__linux__) || defined(__FreeBSD_kernel__) || defined(__NetBSD__)
    return "/proc/self/exe";
#elif defined(__FreeBSD__) || defined(__DragonFly__)
    static int pathname[] = { CTL_KERN, KERN_PROC, KERN_PROC_PATHNAME, -1 };
    size_t path_size = PATH_MAX;
    char *path = malloc( path_size );
    if (path && !sysctl( pathname, sizeof(pathname)/sizeof(pathname[0]), path, &path_size, NULL, 0 ))
        return path;
    free( path );
#elif defined(__APPLE__)
    uint32_t path_size = PATH_MAX;
    char *path = malloc( path_size );
    if (path && !_NSGetExecutablePath( path, &path_size ))
        return path;
    free( path );
#endif
    return NULL;
}

/* Must stay in sync with dlls/ntdll/unix/process.c. */
static uint64_t fnv1a_mix( uint64_t h, const void *data, size_t len )
{
    const unsigned char *p = data;
    while (len--)
    {
        h ^= *p++;
        h *= 1099511628211ULL;
    }
    return h;
}

static uint64_t fnv1a_hash_stat( uint64_t h, const char *path )
{
    struct stat st;
    uint64_t fields[7] = { 0 };

    if (path && stat( path, &st ) == 0)
    {
        fields[0] = (uint64_t)st.st_dev;
        fields[1] = (uint64_t)st.st_ino;
        fields[2] = (uint64_t)st.st_size;
        fields[3] = (uint64_t)st.st_mtime;
        fields[5] = (uint64_t)st.st_ctime;
#ifdef HAVE_STRUCT_STAT_ST_MTIM
        fields[4] = (uint64_t)st.st_mtim.tv_nsec;
        fields[6] = (uint64_t)st.st_ctim.tv_nsec;
#elif defined(HAVE_STRUCT_STAT_ST_MTIMESPEC)
        fields[4] = (uint64_t)st.st_mtimespec.tv_nsec;
        fields[6] = (uint64_t)st.st_ctimespec.tv_nsec;
#endif
    }
    return fnv1a_mix( h, fields, sizeof(fields) );
}

static uint64_t compute_binary_fingerprint( const char *zygote_bin, const char *ntdll_bin,
                                            const char *win32u_bin )
{
    uint64_t h = 14695981039346656037ULL;
    h = fnv1a_hash_stat( h, zygote_bin );
    h = fnv1a_hash_stat( h, ntdll_bin );
    h = fnv1a_hash_stat( h, win32u_bin );
    return h;
}

static int resolve_binary_paths( const char *name, char **out_zygote, char **out_ntdll, char **out_win32u )
{
    char *dir, *bdir;

    if (!name) return -1;
    if (!(dir = realpath_dirname( name ))) return -1;

    if ((bdir = remove_tail( dir, "/loader" )))
    {
        *out_zygote = build_path( bdir, "loader/wine-zygote" );
        *out_ntdll  = build_path( bdir, "dlls/ntdll/ntdll.so" );
        *out_win32u = build_path( bdir, "dlls/win32u/win32u.so" );
        free( bdir );
    }
    else
    {
        *out_zygote = build_path( dir, "wine-zygote" );
        *out_ntdll  = build_path( dir, "ntdll.so" );
        *out_win32u = build_path( dir, "win32u.so" );
    }
    free( dir );
    return (*out_zygote && *out_ntdll && *out_win32u) ? 0 : -1;
}

/* dlopen ntdll.so, exactly like loader/main.c does, and report resolved binary paths */
static void *try_dlopen_ntdll( const char *name, struct zygote_state *st )
{
    char *zygote_bin = NULL, *ntdll_bin = NULL, *win32u_bin = NULL;
    void *handle;

    if (resolve_binary_paths( name, &zygote_bin, &ntdll_bin, &win32u_bin ) < 0)
    {
        free( zygote_bin );
        free( ntdll_bin );
        free( win32u_bin );
        return NULL;
    }

    /* Snapshot binary fingerprint before dlopen so any subsequent replacement is detected. */
    st->bin_fingerprint = compute_binary_fingerprint( zygote_bin, ntdll_bin, win32u_bin );

    handle = dlopen( ntdll_bin, RTLD_NOW );
    if (handle)
    {
        st->zygote_bin = zygote_bin;
        st->ntdll_bin  = ntdll_bin;
        st->win32u_bin = win32u_bin;
    }
    else
    {
        free( zygote_bin );
        free( ntdll_bin );
        free( win32u_bin );
    }
    return handle;
}

static int init_server_watch( const char *zygote_sock_path, struct server_watch *w )
{
    const char *slash;
    char *dir;
    struct stat st;
    int fd;

    memset( w, 0, sizeof(*w) );
    if (!(slash = strrchr( zygote_sock_path, '/' ))) return 0;
    if (!(dir = strndup( zygote_sock_path, slash == zygote_sock_path ? 1 : (size_t)(slash - zygote_sock_path) )))
        return 0;

    w->sock_path = build_path( dir, "socket" );
    w->lock_path = build_path( dir, "lock" );
    free( dir );
    if (!w->sock_path || !w->lock_path) return 0;

    if (lstat( w->sock_path, &st ) == 0)
    {
        w->active = 1;
        w->sock_dev = st.st_dev;
        w->sock_ino = st.st_ino;
        if ((fd = open( w->lock_path, O_RDONLY | O_CLOEXEC )) != -1)
        {
            struct flock fl;
            fl.l_type   = F_WRLCK;
            fl.l_whence = SEEK_SET;
            fl.l_start  = 0;
            fl.l_len    = 1;
            if (fcntl( fd, F_GETLK, &fl ) == 0 && fl.l_type == F_WRLCK)
                w->server_pid = fl.l_pid;
            close( fd );
        }
    }
    else if (errno == ENOENT && strstr( zygote_sock_path, "/server-" ))
    {
        /* Wineserver already exited before the zygote finished starting. */
        return -1;
    }
    return 0;
}

static int is_wineserver_alive( const struct server_watch *w )
{
    struct stat st;
    int fd;

    if (!w->active) return 1;
    if (lstat( w->sock_path, &st ) != 0) return 0;
    if (st.st_dev != w->sock_dev || st.st_ino != w->sock_ino) return 0;

    if (w->server_pid > 0)
    {
        struct flock fl;
        int r;
        if ((fd = open( w->lock_path, O_RDONLY | O_CLOEXEC )) == -1) return 0;
        fl.l_type   = F_WRLCK;
        fl.l_whence = SEEK_SET;
        fl.l_start  = 0;
        fl.l_len    = 1;
        r = fcntl( fd, F_GETLK, &fl );
        close( fd );
        if (r == 0 && (fl.l_type != F_WRLCK || fl.l_pid != w->server_pid)) return 0;
    }
    return 1;
}

static int is_binary_current( const struct zygote_state *st )
{
    return compute_binary_fingerprint( st->zygote_bin, st->ntdll_bin, st->win32u_bin ) == st->bin_fingerprint;
}

static int socket_still_owned( const char *path, const struct stat *bound_st )
{
    struct stat st;
    return lstat( path, &st ) == 0 &&
           st.st_dev == bound_st->st_dev &&
           st.st_ino == bound_st->st_ino;
}

static void cleanup_own_socket( const char *path, const struct stat *bound_st )
{
    if (socket_still_owned( path, bound_st )) unlink( path );
}

static int is_daemon_still_valid( const char *sock_path, const struct stat *bound_st,
                                  const struct zygote_state *st )
{
    if (quit_requested) return 0;
    if (!socket_still_owned( sock_path, bound_st )) return 0;
    if (!is_binary_current( st )) return 0;
    if (!is_wineserver_alive( &st->watch )) return 0;
    return 1;
}

static int read_exact( int fd, void *buf, size_t len )
{
    char *p = buf;
    while (len)
    {
        ssize_t r = read( fd, p, len );
        if (r < 0)
        {
            if (errno == EINTR) continue;
            return -1;
        }
        if (r == 0) return -1;
        p += r;
        len -= r;
    }
    return 0;
}

static int write_exact( int fd, const void *buf, size_t len )
{
    const char *p = buf;
    while (len)
    {
        ssize_t r = write( fd, p, len );
        if (r < 0)
        {
            if (errno == EINTR) continue;
            return -1;
        }
        p += r;
        len -= r;
    }
    return 0;
}

/* read a NUL separated, double-NUL terminated block */
static char *read_block( int fd, unsigned int len )
{
    char *block;

    if (!len) return NULL;
    if (!(block = malloc( len + 1 ))) return NULL;
    if (read_exact( fd, block, len ) < 0)
    {
        free( block );
        return NULL;
    }
    block[len] = 0;
    return block;
}

static void set_stdio_fd( int stdin_fd, int stdout_fd )
{
    int fd = -1;

    if (stdin_fd == -1 || stdout_fd == -1)
    {
        fd = open( "/dev/null", O_RDWR );
        if (stdin_fd == -1) stdin_fd = fd;
        if (stdout_fd == -1) stdout_fd = fd;
    }
    if (stdin_fd != 0) dup2( stdin_fd, 0 );
    if (stdout_fd != 1) dup2( stdout_fd, 1 );
    if (fd != -1) close( fd );
}

/* build a char **argv out of a NUL separated block, prepending argv[0] */
static char **build_argv_block( const char *block, const char *argv0 )
{
    unsigned int i, count = 1;
    char **argv;

    if (block) for (i = 0; block[i]; i += strlen( block + i ) + 1) count++;
    argv = malloc( (count + 1) * sizeof(*argv) );
    argv[0] = (char *)argv0;
    if (block)
    {
        count = 1;
        for (i = 0; block[i]; i += strlen( block + i ) + 1) argv[count++] = (char *)(block + i);
    }
    argv[count] = NULL;
    return argv;
}

/* apply an envp block, replacing the inherited environment */
static void apply_env_block( const char *block )
{
    const char *p;

    clearenv();
    if (!block) return;
    for (p = block; *p; p += strlen( p ) + 1)
    {
        const char *eq = strchr( p, '=' );
        char *name;
        if (!eq) continue;
        if (!(name = strndup( p, eq - p ))) continue;
        setenv( name, eq + 1, 1 );
        free( name );
    }
}

static void __attribute__((noreturn)) run_child( const struct zygote_request *req,
                                         const struct pollfd *fds, nfds_t nfds,
                                         int socket_fd, int cwd_fd, int stdin_fd, int stdout_fd, int stderr_fd,
                                         const char *argv_block, const char *env_block,
                                         const char *reserve, const char *winedebug,
                                         const char *self_path,
                                         void (*wine_main)( int, char ** ))
{
    char buffer[64];
    char **argv;
    struct sigaction sa;
    nfds_t i;
    int n;

    for (i = 0; i < nfds; i++) close( fds[i].fd );

    if (req->flags & ZYGOTE_FLAG_DETACHED)
    {
        setsid();
        set_stdio_fd( -1, -1 );
    }
    else set_stdio_fd( stdin_fd, stdout_fd );

    if (stdin_fd != -1 && stdin_fd != 0) close( stdin_fd );
    if (stdout_fd != -1 && stdout_fd != 1) close( stdout_fd );
    if (stderr_fd != -1)
    {
        if (stderr_fd != 2)
        {
            dup2( stderr_fd, 2 );
            close( stderr_fd );
        }
    }

    if (cwd_fd != -1)
    {
        if (fchdir( cwd_fd ) == -1) chdir( "/" );
        close( cwd_fd );
    }

    /* reset the signals the zygote changed, like a fresh loader does;
     * clear sa_flags so SA_NOCLDSTOP / SA_RESTART are not inherited */
    sa.sa_handler = SIG_DFL;
    sa.sa_flags = 0;
    sigemptyset( &sa.sa_mask );
    sigaction( SIGPIPE, &sa, NULL );
    sigaction( SIGCHLD, &sa, NULL );
    sigaction( SIGTERM, &sa, NULL );
    sigaction( SIGINT, &sa, NULL );

    apply_env_block( env_block );
    if (winedebug)
    {
        if (!strncmp( winedebug, "WINEDEBUG=", 10 )) setenv( "WINEDEBUG", winedebug + 10, 1 );
        else setenv( "WINEDEBUG", winedebug, 1 );
    }
    if (reserve) setenv( "WINEPRELOADRESERVE", reserve, 1 );
    if (socket_fd != -1)
    {
        snprintf( buffer, sizeof(buffer), "%u", (unsigned int)socket_fd );
        setenv( "WINESERVERSOCKET", buffer, 1 );
    }

    if (req->flags & ZYGOTE_FLAG_UNIX)
    {
        /* argv[0] is NULL, argv[1] is unix_name, argv[2..] are child argv */
        argv = build_argv_block( argv_block, NULL );
        execv( argv[1], argv + 2 );
        _exit(1);
    }

    setenv( "WINELOADERNOEXEC", "1", 1 );
    argv = build_argv_block( argv_block, self_path );
    for (n = 0; argv[n]; n++)
        ;
    wine_main( n, argv );
    _exit(0);
}

static void reap_children( int signum )
{
    (void)signum;
    while (waitpid( -1, NULL, WNOHANG ) > 0)
        ;
}

static void handle_quit( int signum )
{
    (void)signum;
    quit_requested = 1;
}

/* read a file to bring it into the page cache (best effort) */
static void warm_file( const char *path, size_t max_size )
{
    static char buffer[65536];
    int fd;
    size_t left = max_size;

    if ((fd = open( path, O_RDONLY | O_CLOEXEC )) == -1) return;
    while (left)
    {
        size_t len = left < sizeof(buffer) ? left : sizeof(buffer);
        ssize_t ret = read( fd, buffer, len );
        if (ret <= 0) break;
        left -= ret;
    }
    close( fd );
}

/* Pre-load the win32 shared objects and warm the PE images every child maps. */
static void warm_libraries( const char *ntdll_path, const char *win32u_path )
{
    static const char *pe_names[] = { "ntdll.dll", "win32u.dll", "kernel32.dll", "kernelbase.dll" };
    const char *prefix, *home;
    char *dir, *build_dir, *path, *tmp = NULL;
    unsigned int i;

    /* the ELF side: win32u.so is dlopened by ntdll on startup */
    if (win32u_path) dlopen( win32u_path, RTLD_NOW );

    if (!(dir = realpath_dirname( ntdll_path ))) return;
    if ((build_dir = remove_tail( dir, "/dlls/ntdll" )))
    {
        for (i = 0; i < sizeof(pe_names)/sizeof(*pe_names); i++)
        {
            static const char *pe_dirs[] = { "dlls/ntdll", "dlls/win32u", "dlls/kernel32", "dlls/kernelbase" };
            path = build_path( build_dir, pe_dirs[i] );
            {
                char *file = build_path( path, pe_names[i] );
                warm_file( file, 4 << 20 );
                free( file );
            }
            free( path );
        }
        free( build_dir );
    }
    else
    {
        char *lib_dir = remove_tail( dir, "/x86_64-unix" );
        if (lib_dir)
        {
            char *win_dir = build_path( lib_dir, "x86_64-windows" );
            for (i = 0; i < sizeof(pe_names)/sizeof(*pe_names); i++)
            {
                path = build_path( win_dir, pe_names[i] );
                warm_file( path, 4 << 20 );
                free( path );
            }
            free( win_dir );
            free( lib_dir );
        }
    }

    /* the PE side: system32 copies in the prefix are what gets mapped */
    if (!(prefix = getenv( "WINEPREFIX" )))
    {
        if (!(home = getenv( "HOME" ))) { free( dir ); return; }
        if (!(tmp = build_path( home, ".wine" ))) { free( dir ); return; }
        prefix = tmp;
    }
    for (i = 0; i < sizeof(pe_names)/sizeof(*pe_names); i++)
    {
        if (asprintf( &path, "%s/drive_c/windows/system32/%s", prefix, pe_names[i] ) != -1)
        {
            warm_file( path, 4 << 20 );
            free( path );
        }
    }
    free( tmp );
    free( dir );
}

/* Returns listen fd >= 0, -2 if another live daemon already owns path, or -1 on error. */
static int create_listen_socket( const char *path, struct stat *bound_st )
{
    struct sockaddr_un addr;
    int fd;

    if (strlen( path ) >= sizeof(addr.sun_path))
    {
        errno = ENAMETOOLONG;
        return -1;
    }
    if ((fd = socket( PF_UNIX, SOCK_STREAM, 0 )) == -1) return -1;
    fcntl( fd, F_SETFD, FD_CLOEXEC );
    memset( &addr, 0, sizeof(addr) );
    addr.sun_family = AF_UNIX;
    snprintf( addr.sun_path, sizeof(addr.sun_path), "%s", path );

    if (bind( fd, (struct sockaddr *)&addr, sizeof(addr) ) == -1)
    {
        int probe, probe_err;
        if (errno != EADDRINUSE)
        {
            close( fd );
            return -1;
        }
        /* Check if another live daemon already owns this exact versioned socket
         * before unlinking, preventing duplicate orphaned daemons on startup races. */
        if ((probe = socket( PF_UNIX, SOCK_STREAM, 0 )) != -1)
        {
            fcntl( probe, F_SETFD, FD_CLOEXEC );
            fcntl( probe, F_SETFL, O_NONBLOCK );
            if (connect( probe, (struct sockaddr *)&addr, sizeof(addr) ) == 0 ||
                errno == EINPROGRESS || errno == EAGAIN)
            {
                close( probe );
                close( fd );
                return -2;
            }
            probe_err = errno;
            close( probe );
            if (probe_err != ECONNREFUSED && probe_err != ENOENT)
            {
                close( fd );
                errno = probe_err;
                return -1;
            }
        }
        unlink( path );
        if (bind( fd, (struct sockaddr *)&addr, sizeof(addr) ) == -1)
        {
            int bind_err = errno;
            close( fd );
            if (bind_err == EADDRINUSE) return -2;
            errno = bind_err;
            return -1;
        }
    }
    if (listen( fd, 128 ) == -1)
    {
        close( fd );
        return -1;
    }
    if (lstat( path, bound_st ) == -1)
    {
        close( fd );
        return -1;
    }
    return fd;
}

static void drain_bytes( int fd, size_t len )
{
    char buf[4096];
    while (len)
    {
        size_t chunk = len < sizeof(buf) ? len : sizeof(buf);
        ssize_t r = read( fd, buf, chunk );
        if (r < 0)
        {
            if (errno == EINTR) continue;
            break;
        }
        if (r == 0) break;
        len -= r;
    }
}

/* handle a single request from client; returns 0 on success, -1 to drop client */
static int handle_client_request( int client, const struct pollfd *pfds, nfds_t pfd_count,
                                  const char *self_path, void (*wine_main)( int, char ** ),
                                  const struct zygote_state *st, int *should_exit )
{
    struct zygote_request req;
    struct zygote_reply reply;
    char cbuf[CMSG_SPACE( sizeof(int) * ZYGOTE_MAX_FDS )];
    struct iovec iov = { &req, sizeof(req) };
    struct msghdr msg = { 0 };
    struct cmsghdr *cmsg;
    int fds[ZYGOTE_MAX_FDS] = { -1, -1, -1, -1, -1 };
    unsigned int nfds = 0, idx = 0;
    int socket_fd = -1, cwd_fd = -1, stdin_fd = -1, stdout_fd = -1, stderr_fd = -1;
    char *argv_block = NULL, *env_block = NULL, *reserve = NULL, *winedebug = NULL;
    ssize_t r;
    pid_t pid;
    int binary_stale, server_dead, req_mismatch;

    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = cbuf;
    msg.msg_controllen = sizeof(cbuf);

    do {
        r = recvmsg( client, &msg, 0 );
    } while (r < 0 && errno == EINTR);

    if (r <= 0) return -1;
    if ((size_t)r < sizeof(req) && read_exact( client, (char *)&req + r, sizeof(req) - r ) < 0) return -1;

    for (cmsg = CMSG_FIRSTHDR( &msg ); cmsg; cmsg = CMSG_NXTHDR( &msg, cmsg ))
    {
        if (cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_RIGHTS)
        {
            nfds = (cmsg->cmsg_len - CMSG_LEN(0)) / sizeof(int);
            if (nfds > ZYGOTE_MAX_FDS) nfds = ZYGOTE_MAX_FDS;
            memcpy( fds, CMSG_DATA(cmsg), nfds * sizeof(int) );
            break;
        }
    }
    if (req.magic != ZYGOTE_MAGIC || req.version != ZYGOTE_VERSION)
    {
        unsigned int j;
        for (j = 0; j < nfds; j++) if (fds[j] != -1) close( fds[j] );
        return -1;
    }

    if (req.flags & ZYGOTE_FLAG_HAS_SOCKET) { if (idx < nfds) socket_fd = fds[idx++]; }
    if (req.flags & ZYGOTE_FLAG_HAS_CWD)    { if (idx < nfds) cwd_fd = fds[idx++]; }
    if (req.flags & ZYGOTE_FLAG_HAS_STDIN)  { if (idx < nfds) stdin_fd = fds[idx++]; }
    if (req.flags & ZYGOTE_FLAG_HAS_STDOUT) { if (idx < nfds) stdout_fd = fds[idx++]; }
    if (req.flags & ZYGOTE_FLAG_HAS_STDERR) { if (idx < nfds) stderr_fd = fds[idx++]; }
    while (idx < nfds) { if (fds[idx] != -1) close( fds[idx] ); idx++; }

    binary_stale = !is_binary_current( st );
    server_dead  = !is_wineserver_alive( &st->watch );
    req_mismatch = (req.server_protocol != st->server_protocol ||
                    req.bin_fingerprint != st->bin_fingerprint);

    if (binary_stale || server_dead || req_mismatch)
    {
        size_t total = (size_t)req.argv_len + req.env_len + req.reserve_len + req.winedebug_len;
        if (total <= 16 * 1024 * 1024) drain_bytes( client, total );
        reply.status = ZYGOTE_STATUS_STALE;
        reply.pid = -1;
        write_exact( client, &reply, sizeof(reply) );
        if (socket_fd != -1) close( socket_fd );
        if (cwd_fd != -1) close( cwd_fd );
        if (stdin_fd != -1) close( stdin_fd );
        if (stdout_fd != -1) close( stdout_fd );
        if (stderr_fd != -1) close( stderr_fd );
        if (binary_stale || server_dead) *should_exit = 1;
        return -1;
    }

    if (!(argv_block = read_block( client, req.argv_len ))) goto failed;
    if (req.env_len && !(env_block = read_block( client, req.env_len ))) goto failed;
    if (req.reserve_len && !(reserve = read_block( client, req.reserve_len ))) goto failed;
    if (req.winedebug_len && !(winedebug = read_block( client, req.winedebug_len ))) goto failed;

    fflush( NULL );
    if ((pid = fork()) == -1)
    {
    failed:
        reply.status = ZYGOTE_STATUS_ERR;
        reply.pid = -1;
        write_exact( client, &reply, sizeof(reply) );
    }
    else if (!pid)
    {
        run_child( &req, pfds, pfd_count, socket_fd, cwd_fd, stdin_fd, stdout_fd, stderr_fd,
                   argv_block, env_block, reserve, winedebug, self_path, wine_main );
    }
    else
    {
        reply.status = ZYGOTE_STATUS_OK;
        reply.pid = pid;
        if (write_exact( client, &reply, sizeof(reply) ) < 0)
        {
            free( argv_block );
            free( env_block );
            free( reserve );
            free( winedebug );
            if (socket_fd != -1) close( socket_fd );
            if (cwd_fd != -1) close( cwd_fd );
            if (stdin_fd != -1) close( stdin_fd );
            if (stdout_fd != -1) close( stdout_fd );
            if (stderr_fd != -1) close( stderr_fd );
            return -1;
        }
    }

    free( argv_block );
    free( env_block );
    free( reserve );
    free( winedebug );
    if (socket_fd != -1) close( socket_fd );
    if (cwd_fd != -1) close( cwd_fd );
    if (stdin_fd != -1) close( stdin_fd );
    if (stdout_fd != -1) close( stdout_fd );
    if (stderr_fd != -1) close( stderr_fd );
    return 0;
}

int main( int argc, char *argv[] )
{
    const char *sock_path;
    struct zygote_state state;
    struct stat bound_sock_st;
    void *handle;
    void (*wine_main)( int, char ** );
    unsigned int (*get_protocol_version)( void );
    int listen_fd;
    struct pollfd *pfds;
    struct sigaction sa;
    sigset_t mask;
    nfds_t nfds = 1, cap = 64, i;

    (void)argc;
    memset( &state, 0, sizeof(state) );

    if (!(sock_path = getenv( "WINEZYGOTESOCKET" )))
    {
        fprintf( stderr, "wine-zygote: WINEZYGOTESOCKET environment variable is not set\n" );
        return 1;
    }
    if (init_server_watch( sock_path, &state.watch ) < 0)
        return 0;

    if (!(handle = try_dlopen_ntdll( get_self_exe(), &state )) &&
        !(handle = try_dlopen_ntdll( argv[0], &state )))
    {
        fprintf( stderr, "wine-zygote: could not load ntdll.so: %s\n", dlerror() );
        return 1;
    }
    if (!(wine_main = (void (*)( int, char ** ))dlsym( handle, "__wine_main" )))
    {
        fprintf( stderr, "wine-zygote: __wine_main function not found in ntdll.so\n" );
        return 1;
    }
    get_protocol_version = (unsigned int (*)( void ))dlsym( handle, "__wine_zygote_protocol_version" );
    state.server_protocol = get_protocol_version ? get_protocol_version() : 0;

    warm_libraries( state.ntdll_bin, state.win32u_bin );

    /* ntdll blocks a set of signals before the zygote is exec'd; unblock
     * them so that SIGCHLD and shutdown signals arrive */
    sigemptyset( &mask );
    sigprocmask( SIG_SETMASK, &mask, NULL );

    sa.sa_handler = SIG_IGN;
    sa.sa_flags = 0;
    sigemptyset( &sa.sa_mask );
    sigaction( SIGPIPE, &sa, NULL );

    sa.sa_handler = reap_children;
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    sigaction( SIGCHLD, &sa, NULL );

    sa.sa_handler = handle_quit;
    sa.sa_flags = 0;
    sigaction( SIGTERM, &sa, NULL );
    sigaction( SIGINT, &sa, NULL );

    listen_fd = create_listen_socket( sock_path, &bound_sock_st );
    if (listen_fd == -2) return 0;  /* another live daemon already owns this socket */
    if (listen_fd == -1)
    {
        fprintf( stderr, "wine-zygote: could not bind %s: %s\n", sock_path, strerror( errno ) );
        return 1;
    }

    if (!(pfds = calloc( cap, sizeof(*pfds) )))
    {
        close( listen_fd );
        cleanup_own_socket( sock_path, &bound_sock_st );
        return 1;
    }
    pfds[0].fd = listen_fd;
    pfds[0].events = POLLIN;

    for (;;)
    {
        int ret, timeout_ms, should_exit = 0;

        /* sweep in case a SIGCHLD was delivered while the mask was being reset */
        while (waitpid( -1, NULL, WNOHANG ) > 0)
            ;

        if (!is_daemon_still_valid( sock_path, &bound_sock_st, &state ))
            break;

        timeout_ms = (nfds == 1) ? 500 : 2000;
        ret = poll( pfds, nfds, timeout_ms );
        if (ret < 0)
        {
            if (errno == EINTR) continue;
            break;
        }
        if (ret == 0) continue;

        if (pfds[0].revents & POLLIN)
        {
            int client = accept( listen_fd, NULL, NULL );
            if (client != -1)
            {
                fcntl( client, F_SETFD, FD_CLOEXEC );
                if (nfds == cap)
                {
                    struct pollfd *n = realloc( pfds, cap * 2 * sizeof(*pfds) );
                    if (!n) { close( client ); continue; }
                    pfds = n;
                    cap *= 2;
                }
                pfds[nfds].fd = client;
                pfds[nfds].events = POLLIN;
                pfds[nfds].revents = 0;
                nfds++;
            }
        }
        for (i = 1; i < nfds; )
        {
            if (pfds[i].revents & (POLLIN | POLLHUP | POLLERR))
            {
                if (handle_client_request( pfds[i].fd, pfds, nfds, argv[0], wine_main,
                                           &state, &should_exit ) < 0)
                {
                    close( pfds[i].fd );
                    pfds[i] = pfds[nfds - 1];
                    nfds--;
                    continue;
                }
            }
            i++;
        }
        if (should_exit) break;
    }

    for (i = 0; i < nfds; i++) close( pfds[i].fd );
    free( pfds );
    cleanup_own_socket( sock_path, &bound_sock_st );
    return 0;
}
