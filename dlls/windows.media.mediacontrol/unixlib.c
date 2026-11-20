/*
 * windows.media.mediacontrol unix implementation: MPRIS2 media player over D-Bus
 *
 * Copyright (c) 2026 Wine project
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
 */

#if 0
#pragma makedep unix
#endif

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <pthread.h>
#include <unistd.h>
#include <poll.h>
#include <sched.h>
#include <time.h>
#include <dbus/dbus.h>

#include "windef.h"
#include "winbase.h"
#include "ntstatus.h"
#include "wine/debug.h"
#include "unixlib.h"

WINE_DEFAULT_DEBUG_CHANNEL(mediacontrol);

/* libdbus-1 is loaded dynamically so the DLL works without D-Bus present */
#define DBUS_FUNCS(X) \
    X(dbus_threads_init_default) \
    X(dbus_bus_get) \
    X(dbus_bus_request_name) \
    X(dbus_bus_release_name) \
    X(dbus_connection_set_exit_on_disconnect) \
    X(dbus_connection_register_object_path) \
    X(dbus_connection_add_filter) \
    X(dbus_connection_read_write_dispatch) \
    X(dbus_connection_read_write) \
    X(dbus_connection_get_unix_fd) \
    X(dbus_connection_dispatch) \
    X(dbus_connection_send) \
    X(dbus_connection_flush) \
    X(dbus_connection_get_is_connected) \
    X(dbus_connection_get_dispatch_status) \
    X(dbus_connection_unref) \
    X(dbus_error_init) \
    X(dbus_error_free) \
    X(dbus_error_is_set) \
    X(dbus_message_new_method_call) \
    X(dbus_message_new_method_return) \
    X(dbus_message_new_error) \
    X(dbus_message_new_signal) \
    X(dbus_message_ref) \
    X(dbus_message_unref) \
    X(dbus_message_get_interface) \
    X(dbus_message_get_member) \
    X(dbus_message_get_type) \
    X(dbus_message_append_args) \
    X(dbus_message_iter_init) \
    X(dbus_message_iter_init_append) \
    X(dbus_message_iter_next) \
    X(dbus_message_iter_recurse) \
    X(dbus_message_iter_get_arg_type) \
    X(dbus_message_iter_get_basic) \
    X(dbus_message_iter_append_basic) \
    X(dbus_message_iter_open_container) \
    X(dbus_message_iter_close_container)

#define DBUS_DECLARE( fn ) static typeof(&fn) p_##fn;
DBUS_FUNCS( DBUS_DECLARE )
#undef DBUS_DECLARE

#define dbus_threads_init_default p_dbus_threads_init_default
#define dbus_bus_get p_dbus_bus_get
#define dbus_bus_request_name p_dbus_bus_request_name
#define dbus_bus_release_name p_dbus_bus_release_name
#define dbus_connection_set_exit_on_disconnect p_dbus_connection_set_exit_on_disconnect
#define dbus_connection_register_object_path p_dbus_connection_register_object_path
#define dbus_connection_add_filter p_dbus_connection_add_filter
#define dbus_connection_read_write_dispatch p_dbus_connection_read_write_dispatch
#define dbus_connection_read_write p_dbus_connection_read_write
#define dbus_connection_get_unix_fd p_dbus_connection_get_unix_fd
#define dbus_connection_dispatch p_dbus_connection_dispatch
#define dbus_connection_send p_dbus_connection_send
#define dbus_connection_flush p_dbus_connection_flush
#define dbus_connection_get_is_connected p_dbus_connection_get_is_connected
#define dbus_connection_get_dispatch_status p_dbus_connection_get_dispatch_status
#define dbus_connection_unref p_dbus_connection_unref
#define dbus_error_init p_dbus_error_init
#define dbus_error_free p_dbus_error_free
#define dbus_error_is_set p_dbus_error_is_set
#define dbus_message_new_method_call p_dbus_message_new_method_call
#define dbus_message_new_method_return p_dbus_message_new_method_return
#define dbus_message_new_error p_dbus_message_new_error
#define dbus_message_new_signal p_dbus_message_new_signal
#define dbus_message_ref p_dbus_message_ref
#define dbus_message_unref p_dbus_message_unref
#define dbus_message_get_interface p_dbus_message_get_interface
#define dbus_message_get_member p_dbus_message_get_member
#define dbus_message_get_type p_dbus_message_get_type
#define dbus_message_append_args p_dbus_message_append_args
#define dbus_message_iter_init p_dbus_message_iter_init
#define dbus_message_iter_init_append p_dbus_message_iter_init_append
#define dbus_message_iter_next p_dbus_message_iter_next
#define dbus_message_iter_recurse p_dbus_message_iter_recurse
#define dbus_message_iter_get_arg_type p_dbus_message_iter_get_arg_type
#define dbus_message_iter_get_basic p_dbus_message_iter_get_basic
#define dbus_message_iter_append_basic p_dbus_message_iter_append_basic
#define dbus_message_iter_open_container p_dbus_message_iter_open_container
#define dbus_message_iter_close_container p_dbus_message_iter_close_container

static dbus_bool_t load_dbus_funcs( void )
{
    static void *lib;
    static dbus_bool_t loaded;

    if (loaded) return lib != NULL;
    loaded = TRUE;

    if (!(lib = dlopen( "libdbus-1.so.3", RTLD_LAZY )))
    {
        TRACE( "no libdbus-1.so.3: %s\n", dlerror() );
        return FALSE;
    }
#define DBUS_LOAD( fn ) \
    if (!(*(void **)&p_##fn = dlsym( lib, #fn ))) { TRACE( "missing symbol " #fn "\n" ); return FALSE; }
    DBUS_FUNCS( DBUS_LOAD )
#undef DBUS_LOAD
    dbus_threads_init_default();
    return TRUE;
}

/* standalone UTF-16 -> UTF-8; the unix .so links only against ntdll, no kernel32 */
static char *wcstoutf8( const WCHAR *str )
{
    unsigned int count = 0, cp;
    const WCHAR *p;
    char *ret, *q;

    if (!str) return NULL;
    for (p = str; *p; p++) count++;
    if (!(ret = malloc( 3 * count + 1 ))) return NULL;

    q = ret;
    p = str;
    while (*p)
    {
        cp = *p;
        if (cp >= 0xd800 && cp <= 0xdbff && p[1] >= 0xdc00 && p[1] <= 0xdfff)
        {
            cp = 0x10000 + ((cp - 0xd800) << 10) + (p[1] - 0xdc00);
            p += 2;
        }
        else p++;

        if (cp < 0x80) *q++ = cp;
        else if (cp < 0x800) { *q++ = 0xc0 | (cp >> 6); *q++ = 0x80 | (cp & 0x3f); }
        else if (cp < 0x10000) { *q++ = 0xe0 | (cp >> 12); *q++ = 0x80 | ((cp >> 6) & 0x3f); *q++ = 0x80 | (cp & 0x3f); }
        else { *q++ = 0xf0 | (cp >> 18); *q++ = 0x80 | ((cp >> 12) & 0x3f); *q++ = 0x80 | ((cp >> 6) & 0x3f); *q++ = 0x80 | (cp & 0x3f); }
    }
    *q = 0;
    return ret;
}

#define MPRIS_PATH "/org/mpris/MediaPlayer2"
#define MPRIS_PLAYER_IFACE "org.mpris.MediaPlayer2.Player"
#define MPRIS_ROOT_IFACE "org.mpris.MediaPlayer2"
#define DBUS_PROPS_IFACE "org.freedesktop.DBus.Properties"
#define DBUS_INTROSPECT_IFACE "org.freedesktop.DBus.Introspectable"

#define MAX_BUTTON_EVENTS 64

static pthread_mutex_t dbus_lock;
static pthread_once_t dbus_lock_once = PTHREAD_ONCE_INIT;

static void dbus_lock_init( void )
{
    pthread_mutexattr_t attr;
    pthread_mutexattr_init( &attr );
    pthread_mutexattr_settype( &attr, PTHREAD_MUTEX_RECURSIVE );
    pthread_mutex_init( &dbus_lock, &attr );
    pthread_mutexattr_destroy( &attr );
}

static void mpris_lock_dbus( void )
{
    pthread_once( &dbus_lock_once, dbus_lock_init );
    pthread_mutex_lock( &dbus_lock );
}

static void mpris_unlock_dbus( void )
{
    pthread_mutex_unlock( &dbus_lock );
}

static pthread_mutex_t event_lock = PTHREAD_MUTEX_INITIALIZER;

static DBusConnection *bus;
static dbus_bool_t path_registered;
static dbus_bool_t name_claimed;
static char *bus_name;

static char *identity;
static char *status_str;
static char *md_title;
static char *md_artist;
static char *md_album;
static char *md_album_artist;
static dbus_bool_t can_play;
static dbus_bool_t can_pause;
static dbus_bool_t can_next;
static dbus_bool_t can_prev;
static dbus_bool_t can_control;
static const double rate_one = 1.0;
static const double volume_one = 1.0;
static unsigned int track_seq;
static char track_id[96] = MPRIS_PATH "/TrackList/NoTrack";

static INT32 event_ring[MAX_BUTTON_EVENTS];
static unsigned int event_head, event_tail;

static void set_string( char **dst, const WCHAR *src )
{
    free( *dst );
    *dst = wcstoutf8( src );
}

static void enqueue_button( INT32 button )
{
    pthread_mutex_lock( &event_lock );
    event_ring[event_head] = button;
    event_head = (event_head + 1) % MAX_BUTTON_EVENTS;
    if (event_head == event_tail) event_tail = (event_tail + 1) % MAX_BUTTON_EVENTS;
    pthread_mutex_unlock( &event_lock );
}

static dbus_bool_t dequeue_button( INT32 *button )
{
    dbus_bool_t got = FALSE;

    pthread_mutex_lock( &event_lock );
    if (event_head != event_tail)
    {
        *button = event_ring[event_tail];
        event_tail = (event_tail + 1) % MAX_BUTTON_EVENTS;
        got = TRUE;
    }
    pthread_mutex_unlock( &event_lock );
    return got;
}

static dbus_bool_t ring_empty( void )
{
    dbus_bool_t empty;

    pthread_mutex_lock( &event_lock );
    empty = event_head == event_tail;
    pthread_mutex_unlock( &event_lock );
    return empty;
}

static DBusConnection *get_bus( void )
{
    DBusError error;

    if (bus)
    {
        if (dbus_connection_get_is_connected( bus )) return bus;
        TRACE( "session bus disconnected, resetting\n" );
        dbus_connection_unref( bus );
        bus = NULL;
        path_registered = FALSE;
        name_claimed = FALSE;
    }
    if (!load_dbus_funcs()) return NULL;
    if (!getenv( "DBUS_SESSION_BUS_ADDRESS" ))
    {
        TRACE( "no session D-Bus address, MPRIS bridge disabled\n" );
        return NULL;
    }

    dbus_error_init( &error );
    bus = dbus_bus_get( DBUS_BUS_SESSION, &error );
    if (dbus_error_is_set( &error ))
    {
        TRACE( "no session bus: %s\n", error.message );
        dbus_error_free( &error );
        return NULL;
    }
    dbus_connection_set_exit_on_disconnect( bus, FALSE );
    return bus;
}

/* all property state above is protected by dbus_lock */
static const char *status_to_string( INT32 status )
{
    switch (status)
    {
    case 3: return "Playing";  /* MediaPlaybackStatus_Playing */
    case 4: return "Paused";   /* MediaPlaybackStatus_Paused */
    default: return "Stopped"; /* MediaPlaybackStatus_Closed (0), _Changing (1), _Stopped (2) */
    }
}

static void append_string_array_entry( DBusMessageIter *dict, const char *key, const char *value )
{
    DBusMessageIter entry, variant, array;

    if (!value || !*value) return;
    dbus_message_iter_open_container( dict, DBUS_TYPE_DICT_ENTRY, NULL, &entry );
    dbus_message_iter_append_basic( &entry, DBUS_TYPE_STRING, &key );
    dbus_message_iter_open_container( &entry, DBUS_TYPE_VARIANT, "as", &variant );
    dbus_message_iter_open_container( &variant, DBUS_TYPE_ARRAY, "s", &array );
    dbus_message_iter_append_basic( &array, DBUS_TYPE_STRING, &value );
    dbus_message_iter_close_container( &variant, &array );
    dbus_message_iter_close_container( &entry, &variant );
    dbus_message_iter_close_container( dict, &entry );
}

static void append_string_entry( DBusMessageIter *dict, const char *key, const char *value )
{
    DBusMessageIter entry, variant;

    if (!value || !*value) return;
    dbus_message_iter_open_container( dict, DBUS_TYPE_DICT_ENTRY, NULL, &entry );
    dbus_message_iter_append_basic( &entry, DBUS_TYPE_STRING, &key );
    dbus_message_iter_open_container( &entry, DBUS_TYPE_VARIANT, "s", &variant );
    dbus_message_iter_append_basic( &variant, DBUS_TYPE_STRING, &value );
    dbus_message_iter_close_container( &entry, &variant );
    dbus_message_iter_close_container( dict, &entry );
}

static void append_metadata( DBusMessageIter *iter )
{
    DBusMessageIter dict, entry, variant;

    dbus_message_iter_open_container( iter, DBUS_TYPE_ARRAY, "{sv}", &dict );
    if (md_title && *md_title)
    {
        const char *key = "mpris:trackid";
        const char *tid = track_id;
        dbus_message_iter_open_container( &dict, DBUS_TYPE_DICT_ENTRY, NULL, &entry );
        dbus_message_iter_append_basic( &entry, DBUS_TYPE_STRING, &key );
        dbus_message_iter_open_container( &entry, DBUS_TYPE_VARIANT, "o", &variant );
        dbus_message_iter_append_basic( &variant, DBUS_TYPE_OBJECT_PATH, &tid );
        dbus_message_iter_close_container( &entry, &variant );
        dbus_message_iter_close_container( &dict, &entry );
    }
    append_string_entry( &dict, "xesam:title", md_title );
    append_string_array_entry( &dict, "xesam:artist", md_artist );
    append_string_entry( &dict, "xesam:album", md_album );
    append_string_array_entry( &dict, "xesam:albumArtist", md_album_artist );
    dbus_message_iter_close_container( iter, &dict );
}

static const char *property_signature( const char *iface, const char *name )
{
    if (!strcmp( iface, MPRIS_PLAYER_IFACE ))
    {
        if (!strcmp( name, "PlaybackStatus" )) return "s";
        if (!strcmp( name, "Metadata" )) return "a{sv}";
        if (!strcmp( name, "CanPlay" ) || !strcmp( name, "CanPause" ) ||
            !strcmp( name, "CanGoNext" ) || !strcmp( name, "CanGoPrevious" ) ||
            !strcmp( name, "CanControl" ) || !strcmp( name, "CanSeek" )) return "b";
        if (!strcmp( name, "Rate" ) || !strcmp( name, "Volume" )) return "d";
    }
    else if (!strcmp( iface, MPRIS_ROOT_IFACE ))
    {
        if (!strcmp( name, "Identity" )) return "s";
        if (!strcmp( name, "CanQuit" ) || !strcmp( name, "CanRaise" ) ||
            !strcmp( name, "HasTrackList" )) return "b";
        if (!strcmp( name, "SupportedUriSchemes" ) || !strcmp( name, "SupportedMimeTypes" )) return "as";
    }
    return NULL;
}

static void append_bool( DBusMessageIter *iter, dbus_bool_t value )
{
    dbus_message_iter_append_basic( iter, DBUS_TYPE_BOOLEAN, &value );
}

static void append_property( DBusMessageIter *iter, const char *iface, const char *name )
{
    static const dbus_bool_t is_false = FALSE;
    DBusMessageIter array;

    if (!strcmp( iface, MPRIS_PLAYER_IFACE ))
    {
        if (!strcmp( name, "PlaybackStatus" ))
        {
            const char *status = status_str ? status_str : "Stopped";
            dbus_message_iter_append_basic( iter, DBUS_TYPE_STRING, &status );
            return;
        }
        if (!strcmp( name, "Metadata" )) { append_metadata( iter ); return; }
        if (!strcmp( name, "CanPlay" )) { append_bool( iter, can_play ); return; }
        if (!strcmp( name, "CanPause" )) { append_bool( iter, can_pause ); return; }
        if (!strcmp( name, "CanGoNext" )) { append_bool( iter, can_next ); return; }
        if (!strcmp( name, "CanGoPrevious" )) { append_bool( iter, can_prev ); return; }
        if (!strcmp( name, "CanControl" )) { append_bool( iter, can_control ); return; }
        if (!strcmp( name, "CanSeek" )) { append_bool( iter, is_false ); return; }
        if (!strcmp( name, "Rate" )) { dbus_message_iter_append_basic( iter, DBUS_TYPE_DOUBLE, &rate_one ); return; }
        if (!strcmp( name, "Volume" )) { dbus_message_iter_append_basic( iter, DBUS_TYPE_DOUBLE, &volume_one ); return; }
        return;
    }

    if (!strcmp( name, "Identity" ))
    {
        const char *id = identity ? identity : "Wine Application";
        dbus_message_iter_append_basic( iter, DBUS_TYPE_STRING, &id );
        return;
    }
    if (!strcmp( name, "CanQuit" )) { append_bool( iter, is_false ); return; }
    if (!strcmp( name, "CanRaise" )) { append_bool( iter, is_false ); return; }
    if (!strcmp( name, "HasTrackList" )) { append_bool( iter, is_false ); return; }
    if (!strcmp( name, "SupportedUriSchemes" ) || !strcmp( name, "SupportedMimeTypes" ))
    {
        dbus_message_iter_open_container( iter, DBUS_TYPE_ARRAY, "s", &array );
        dbus_message_iter_close_container( iter, &array );
    }
}

static void send_properties_changed( const char *iface, const char *const *names, unsigned int count )
{
    DBusMessage *signal;
    DBusMessageIter iter, dict, array;
    unsigned int i;

    if (!bus || !name_claimed) return;
    if (!(signal = dbus_message_new_signal( MPRIS_PATH, DBUS_PROPS_IFACE, "PropertiesChanged" ))) return;

    dbus_message_iter_init_append( signal, &iter );
    dbus_message_iter_append_basic( &iter, DBUS_TYPE_STRING, &iface );
    dbus_message_iter_open_container( &iter, DBUS_TYPE_ARRAY, "{sv}", &dict );
    for (i = 0; i < count; i++)
    {
        DBusMessageIter entry, variant;
        const char *name = names[i];
        const char *sig = property_signature( iface, name );
        if (!sig) continue;
        dbus_message_iter_open_container( &dict, DBUS_TYPE_DICT_ENTRY, NULL, &entry );
        dbus_message_iter_append_basic( &entry, DBUS_TYPE_STRING, &name );
        dbus_message_iter_open_container( &entry, DBUS_TYPE_VARIANT, sig, &variant );
        append_property( &variant, iface, name );
        dbus_message_iter_close_container( &entry, &variant );
        dbus_message_iter_close_container( &dict, &entry );
    }
    dbus_message_iter_close_container( &iter, &dict );
    dbus_message_iter_open_container( &iter, DBUS_TYPE_ARRAY, "s", &array );
    dbus_message_iter_close_container( &iter, &array );

    dbus_connection_send( bus, signal, NULL );
    dbus_message_unref( signal );
}

struct pending_signal
{
    struct pending_signal *next;
    unsigned int count;
    char *iface;
    char **names;
};

static struct pending_signal *sig_head, *sig_tail;

static void emit_properties_changed( const char *iface, const char *const *names, unsigned int count )
{
    struct pending_signal *sig;
    unsigned int i, total = strlen( iface ) + 1;

    for (i = 0; i < count; i++) total += strlen( names[i] ) + 1;
    if (!(sig = malloc( sizeof(*sig) + total ))) return;
    if (!(sig->names = malloc( count * sizeof(*sig->names) ))) { free( sig ); return; }
    sig->next = NULL;
    sig->count = count;
    sig->iface = (char *)(sig + 1);
    strcpy( sig->iface, iface );
    {
        char *dst = sig->iface + strlen( iface ) + 1;
        for (i = 0; i < count; i++)
        {
            sig->names[i] = dst;
            strcpy( dst, names[i] );
            dst += strlen( names[i] ) + 1;
        }
    }
    if (sig_tail) sig_tail->next = sig;
    else sig_head = sig;
    sig_tail = sig;
}

/* caller holds dbus_lock */
static void flush_pending_signals( void )
{
    while (sig_head)
    {
        struct pending_signal *sig = sig_head;
        sig_head = sig->next;
        if (!sig_head) sig_tail = NULL;
        send_properties_changed( sig->iface, (const char *const *)sig->names, sig->count );
        free( sig->names );
        free( sig );
    }
}

static void handle_get( DBusConnection *conn, DBusMessage *msg )
{
    const char *iface, *name;
    const char *sig;
    DBusMessage *reply;
    DBusMessageIter iter, variant;

    if (!dbus_message_iter_init( msg, &iter )) goto invalid;
    if (dbus_message_iter_get_arg_type( &iter ) != DBUS_TYPE_STRING ) goto invalid;
    dbus_message_iter_get_basic( &iter, &iface );
    dbus_message_iter_next( &iter );
    if (dbus_message_iter_get_arg_type( &iter ) != DBUS_TYPE_STRING ) goto invalid;
    dbus_message_iter_get_basic( &iter, &name );

    if (!(sig = property_signature( iface, name ))) goto invalid;
    if (!(reply = dbus_message_new_method_return( msg ))) return;
    dbus_message_iter_init_append( reply, &iter );
    dbus_message_iter_open_container( &iter, DBUS_TYPE_VARIANT, sig, &variant );
    append_property( &variant, iface, name );
    dbus_message_iter_close_container( &iter, &variant );
    dbus_connection_send( conn, reply, NULL );
    dbus_message_unref( reply );
    return;

invalid:
    reply = dbus_message_new_error( msg, "org.freedesktop.DBus.Error.InvalidArgs", "Unknown property" );
    if (reply)
    {
        dbus_connection_send( conn, reply, NULL );
        dbus_message_unref( reply );
    }
}

static void handle_get_all( DBusConnection *conn, DBusMessage *msg )
{
    static const char *const player_props[] =
    {
        "PlaybackStatus", "Metadata", "CanPlay", "CanPause", "CanGoNext", "CanGoPrevious",
        "CanControl", "CanSeek", "Rate", "Volume",
    };
    static const char *const root_props[] =
    {
        "Identity", "CanQuit", "CanRaise", "HasTrackList", "SupportedUriSchemes", "SupportedMimeTypes",
    };
    const char *const *names;
    const char *iface;
    unsigned int i, count;
    DBusMessage *reply;
    DBusMessageIter iter, dict;

    if (!dbus_message_iter_init( msg, &iter )) return;
    if (dbus_message_iter_get_arg_type( &iter ) != DBUS_TYPE_STRING ) return;
    dbus_message_iter_get_basic( &iter, &iface );

    if (!strcmp( iface, MPRIS_PLAYER_IFACE )) { names = player_props; count = ARRAY_SIZE( player_props ); }
    else if (!strcmp( iface, MPRIS_ROOT_IFACE )) { names = root_props; count = ARRAY_SIZE( root_props ); }
    else { names = NULL; count = 0; }

    if (!(reply = dbus_message_new_method_return( msg ))) return;
    dbus_message_iter_init_append( reply, &iter );
    dbus_message_iter_open_container( &iter, DBUS_TYPE_ARRAY, "{sv}", &dict );
    for (i = 0; i < count; i++)
    {
        DBusMessageIter entry, variant;
        const char *name = names[i];
        const char *sig = property_signature( iface, name );
        if (!sig) continue;
        dbus_message_iter_open_container( &dict, DBUS_TYPE_DICT_ENTRY, NULL, &entry );
        dbus_message_iter_append_basic( &entry, DBUS_TYPE_STRING, &name );
        dbus_message_iter_open_container( &entry, DBUS_TYPE_VARIANT, sig, &variant );
        append_property( &variant, iface, name );
        dbus_message_iter_close_container( &entry, &variant );
        dbus_message_iter_close_container( &dict, &entry );
    }
    dbus_message_iter_close_container( &iter, &dict );
    dbus_connection_send( conn, reply, NULL );
    dbus_message_unref( reply );
}

static void handle_method( DBusConnection *conn, DBusMessage *msg )
{
    const char *member = dbus_message_get_member( msg );
    DBusMessage *reply;
    INT32 button = -1;

    if (!strcmp( member, "Play" )) button = 0;
    else if (!strcmp( member, "Pause" )) button = 1;
    else if (!strcmp( member, "Next" )) button = 3;
    else if (!strcmp( member, "Previous" )) button = 4;
    else if (!strcmp( member, "PlayPause" )) button = 8;
    if (button >= 0) enqueue_button( button );

    if ((reply = dbus_message_new_method_return( msg )))
    {
        dbus_connection_send( conn, reply, NULL );
        dbus_message_unref( reply );
    }
}

static DBusHandlerResult filter_cb( DBusConnection *conn, DBusMessage *msg, void *data )
{
    (void)conn;
    (void)data;
    TRACE( "filter type %d iface %s member %s\n", dbus_message_get_type( msg ),
           debugstr_a( dbus_message_get_interface( msg ) ), debugstr_a( dbus_message_get_member( msg ) ) );
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static DBusHandlerResult object_path_function( DBusConnection *conn, DBusMessage *msg, void *data )
{
    const char *iface = dbus_message_get_interface( msg );
    const char *member = dbus_message_get_member( msg );

    (void)data;
    if (dbus_message_get_type( msg ) != DBUS_MESSAGE_TYPE_METHOD_CALL ) return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    if (iface && !strcmp( iface, DBUS_PROPS_IFACE ))
    {
        if (!strcmp( member, "Get" )) handle_get( conn, msg );
        else if (!strcmp( member, "GetAll" )) handle_get_all( conn, msg );
        else return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
        return DBUS_HANDLER_RESULT_HANDLED;
    }
    handle_method( conn, msg );
    return DBUS_HANDLER_RESULT_HANDLED;
}

static const DBusObjectPathVTable object_vtable =
{
    NULL,
    object_path_function,
};

static DBusConnection *ensure_active( void )
{
    DBusError error;

    if (!(bus = get_bus())) return NULL;
    if (!dbus_connection_get_is_connected( bus ))
    {
        TRACE( "reconnecting to session bus\n" );
        dbus_connection_unref( bus );
        bus = NULL;
        path_registered = FALSE;
        name_claimed = FALSE;
        if (!(bus = get_bus())) return NULL;
    }

    if (!path_registered)
    {
        dbus_connection_add_filter( bus, filter_cb, NULL, NULL );
        dbus_error_init( &error );
        if (!dbus_connection_register_object_path( bus, MPRIS_PATH, &object_vtable, NULL ))
        {
            TRACE( "failed to register " MPRIS_PATH "\n" );
            return bus;
        }
        path_registered = TRUE;
    }

    if (!name_claimed)
    {
        char name[96];
        snprintf( name, sizeof(name), "org.mpris.MediaPlayer2.wine.instance%u", (unsigned int)getpid() );
        free( bus_name );
        if (!(bus_name = strdup( name ))) return bus;
        dbus_error_init( &error );
        dbus_bus_request_name( bus, bus_name, DBUS_NAME_FLAG_DO_NOT_QUEUE, &error );
        if (dbus_error_is_set( &error ))
        {
            TRACE( "failed to claim %s: %s\n", bus_name, error.message );
            dbus_error_free( &error );
            return bus;
        }
        name_claimed = TRUE;
        TRACE( "claimed D-Bus name %s\n", bus_name );
    }
    return bus;
}

static NTSTATUS mpris_claim( void *args )
{
    struct mpris_claim_params *params = args;

    mpris_lock_dbus();
    if (params && params->identity) set_string( &identity, params->identity );
    else if (!identity) identity = strdup( "Wine Application" );
    if (!ensure_active())
    {
        mpris_unlock_dbus();
        return STATUS_NO_SUCH_DEVICE;
    }
    mpris_unlock_dbus();
    return STATUS_SUCCESS;
}

static NTSTATUS mpris_release( void *args )
{
    mpris_lock_dbus();
    while (sig_head)
    {
        struct pending_signal *sig = sig_head;
        sig_head = sig->next;
        free( sig->names );
        free( sig );
    }
    sig_tail = NULL;
    if (bus && name_claimed)
    {
        dbus_bus_release_name( bus, bus_name, NULL );
        name_claimed = FALSE;
        TRACE( "released D-Bus name %s\n", debugstr_a( bus_name ) );
    }
    mpris_unlock_dbus();
    return STATUS_SUCCESS;
}

static NTSTATUS mpris_set_status( void *args )
{
    struct mpris_status_params *params = args;
    static const char *const status_changed[] = { "PlaybackStatus" };

    mpris_lock_dbus();
    if (!ensure_active())
    {
        mpris_unlock_dbus();
        return STATUS_NO_SUCH_DEVICE;
    }
    free( status_str );
    status_str = strdup( status_to_string( params->status ) );
    if (name_claimed) emit_properties_changed( MPRIS_PLAYER_IFACE, status_changed, 1 );
    mpris_unlock_dbus();
    return STATUS_SUCCESS;
}

static NTSTATUS mpris_set_metadata( void *args )
{
    struct mpris_metadata_params *params = args;
    static const char *const metadata_changed[] = { "Metadata" };

    mpris_lock_dbus();
    if (!ensure_active())
    {
        mpris_unlock_dbus();
        return STATUS_NO_SUCH_DEVICE;
    }
    set_string( &md_title, params->title );
    set_string( &md_artist, params->artist );
    set_string( &md_album, params->album );
    set_string( &md_album_artist, params->album_artist );
    track_seq++;
    snprintf( track_id, sizeof(track_id), MPRIS_PATH "/TrackList/Track%u", track_seq );
    if (name_claimed) emit_properties_changed( MPRIS_PLAYER_IFACE, metadata_changed, 1 );
    mpris_unlock_dbus();
    return STATUS_SUCCESS;
}

static NTSTATUS mpris_set_caps( void *args )
{
    struct mpris_caps_params *params = args;
    static const char *const caps_changed[] =
    {
        "CanPlay", "CanPause", "CanGoNext", "CanGoPrevious", "CanControl",
    };
    dbus_bool_t changed;

    mpris_lock_dbus();
    if (!ensure_active())
    {
        mpris_unlock_dbus();
        return STATUS_NO_SUCH_DEVICE;
    }
    changed = can_play != (dbus_bool_t)params->can_play || can_pause != (dbus_bool_t)params->can_pause
              || can_next != (dbus_bool_t)params->can_next || can_prev != (dbus_bool_t)params->can_prev
              || can_control != (dbus_bool_t)params->can_control;
    can_play = params->can_play;
    can_pause = params->can_pause;
    can_next = params->can_next;
    can_prev = params->can_prev;
    can_control = params->can_control;
    if (changed && name_claimed) emit_properties_changed( MPRIS_PLAYER_IFACE, caps_changed, ARRAY_SIZE( caps_changed ) );
    mpris_unlock_dbus();
    return STATUS_SUCCESS;
}

static NTSTATUS mpris_poll( void *args )
{
    struct mpris_event_params *params = args;
    INT32 remain = params->timeout_ms;

    params->got_event = FALSE;
    params->button = 0;

    if (dequeue_button( &params->button ))
    {
        params->got_event = TRUE;
        return STATUS_SUCCESS;
    }

    while (remain > 0 && ring_empty())
    {
        INT chunk = remain > 50 ? 50 : remain;
        int fd = -1;

        mpris_lock_dbus();
        if (!get_bus())
        {
            mpris_unlock_dbus();
            return STATUS_NO_SUCH_DEVICE;
        }
        flush_pending_signals();
        dbus_connection_flush( bus );
        dbus_connection_read_write_dispatch( bus, 0 );
        while (dbus_connection_dispatch( bus ) == DBUS_DISPATCH_DATA_REMAINS) ;
        dbus_connection_get_unix_fd( bus, &fd );
        mpris_unlock_dbus();

        if (!ring_empty()) break;

        if (fd >= 0)
        {
            struct pollfd pfd = { fd, POLLIN, 0 };
            if (poll( &pfd, 1, chunk ) > 0)
            {
                mpris_lock_dbus();
                if (bus)
                {
                    dbus_connection_read_write_dispatch( bus, 0 );
                    while (dbus_connection_dispatch( bus ) == DBUS_DISPATCH_DATA_REMAINS) ;
                }
                mpris_unlock_dbus();
            }
        }
        else
        {
            struct timespec ts = { 0, chunk * 1000000L };
            nanosleep( &ts, NULL );
        }
        remain -= chunk;
    }

    if (dequeue_button( &params->button )) params->got_event = TRUE;
    return STATUS_SUCCESS;
}

const unixlib_entry_t __wine_unix_call_funcs[] =
{
    mpris_claim,
    mpris_release,
    mpris_set_status,
    mpris_set_metadata,
    mpris_set_caps,
    mpris_poll,
};

C_ASSERT( ARRAYSIZE(__wine_unix_call_funcs) == unix_funcs_count );
