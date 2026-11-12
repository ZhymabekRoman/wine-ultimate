/*
 * twinapi.appcore unix implementation: FreeDesktop Notifications over D-Bus
 *
 * Copyright (c) 2026 Wine project
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#if 0
#pragma makedep unix
#endif

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <dbus/dbus.h>

#include "windef.h"
#include "winbase.h"
#include "winnls.h"
#include "ntstatus.h"
#include "wine/debug.h"
#include "unixlib.h"

WINE_DEFAULT_DEBUG_CHANNEL(twinapi);

/* standalone UTF-16 -> UTF-8; the unix .so links only against ntdll, no kernel32 */
static char *wcstoutf8( const WCHAR *str, int size )
{
    unsigned int count = 0, cp;
    const WCHAR *p;
    char *ret, *q;

    (void)size; /* all callers pass NUL-terminated strings */
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

#define MAX_EVENTS 64
#define NOTIFY_TIMEOUT_MS 2000

struct fd_event
{
    UINT32 id;
    INT32 reason;
};

static DBusConnection *session_bus;
static struct fd_event event_ring[MAX_EVENTS];
static unsigned int event_head, event_tail;
static pthread_mutex_t event_lock = PTHREAD_MUTEX_INITIALIZER;

static dbus_bool_t notifications_filter( DBusConnection *conn, DBusMessage *msg, void *data )
{
    const char *iface = dbus_message_get_interface( msg );
    const char *member = dbus_message_get_member( msg );
    DBusMessageIter iter;
    struct fd_event event;
    UINT32 id;
    INT32 reason;

    if (dbus_message_get_type( msg ) != DBUS_MESSAGE_TYPE_SIGNAL ) return FALSE;
    if (strcmp( iface, "org.freedesktop.Notifications" )) return FALSE;

    if (!strcmp( member, "NotificationClosed" ))
    {
        if (!dbus_message_iter_init( msg, &iter )) return FALSE;
        if (dbus_message_iter_get_arg_type( &iter ) != DBUS_TYPE_UINT32 ) return FALSE;
        dbus_message_iter_get_basic( &iter, &id );
        dbus_message_iter_next( &iter );
        reason = 3;
        if (dbus_message_iter_get_arg_type( &iter ) == DBUS_TYPE_UINT32 )
        {
            UINT32 r;
            dbus_message_iter_get_basic( &iter, &r );
            reason = r == 1 ? 1 : 3; /* 1 expired, 2 user dismissed, 3 closed by app */
        }
    }
    else if (!strcmp( member, "ActionInvoked" ))
    {
        if (!dbus_message_iter_init( msg, &iter )) return FALSE;
        if (dbus_message_iter_get_arg_type( &iter ) != DBUS_TYPE_UINT32 ) return FALSE;
        dbus_message_iter_get_basic( &iter, &id );
        reason = 2;
    }
    else return FALSE;

    event.id = id;
    event.reason = reason;
    pthread_mutex_lock( &event_lock );
    event_ring[event_head] = event;
    event_head = (event_head + 1) % MAX_EVENTS;
    if (event_head == event_tail) event_tail = (event_tail + 1) % MAX_EVENTS;
    pthread_mutex_unlock( &event_lock );
    return TRUE;
}

static DBusConnection *get_session_bus( void )
{
    DBusError error;

    if (session_bus) return session_bus;

    dbus_error_init( &error );
    session_bus = dbus_bus_get( DBUS_BUS_SESSION, &error );
    if (dbus_error_is_set( &error ))
    {
        TRACE( "no session bus: %s\n", error.message );
        dbus_error_free( &error );
        return NULL;
    }
    dbus_connection_set_exit_on_disconnect( session_bus, FALSE );
    dbus_bus_add_match( session_bus,
                        "type='signal',interface='org.freedesktop.Notifications'", NULL );
    dbus_connection_add_filter( session_bus, notifications_filter, NULL, NULL );
    return session_bus;
}

static NTSTATUS fd_notify( void *args )
{
    struct fd_notify_params *params = args;
    DBusConnection *bus = get_session_bus();
    DBusMessage *msg, *reply;
    DBusMessageIter iter, array, dict;
    DBusError error;
    char *app_name = NULL, *icon = NULL, *summary = NULL, *body = NULL;
    const char *default_action = "default";
    const char *badge_key = "badge-count";
    dbus_int32_t timeout = -1;

    if (!bus) return STATUS_NO_SUCH_DEVICE;

    app_name = wcstoutf8( params->app_name ? params->app_name : (const WCHAR *)L"Wine Application", -1 );
    icon = wcstoutf8( params->icon ? params->icon : (const WCHAR *)L"", -1 );
    summary = wcstoutf8( params->summary ? params->summary : (const WCHAR *)L"", -1 );
    body = wcstoutf8( params->body ? params->body : (const WCHAR *)L"", -1 );

    dbus_error_init( &error );
    if (!(msg = dbus_message_new_method_call( "org.freedesktop.Notifications", "/org/freedesktop/Notifications",
                                              "org.freedesktop.Notifications", "Notify" )))
    {
        free( app_name ); free( icon ); free( summary ); free( body );
        return STATUS_NO_MEMORY;
    }

    dbus_message_iter_init_append( msg, &iter );
    dbus_message_iter_append_basic( &iter, DBUS_TYPE_STRING, &app_name );
    dbus_message_iter_append_basic( &iter, DBUS_TYPE_UINT32, &params->replaces_id );
    dbus_message_iter_append_basic( &iter, DBUS_TYPE_STRING, &icon );
    dbus_message_iter_append_basic( &iter, DBUS_TYPE_STRING, &summary );
    dbus_message_iter_append_basic( &iter, DBUS_TYPE_STRING, &body );

    dbus_message_iter_open_container( &iter, DBUS_TYPE_ARRAY, "s", &array );
    dbus_message_iter_append_basic( &array, DBUS_TYPE_STRING, &default_action );
    dbus_message_iter_close_container( &iter, &array );

    dbus_message_iter_open_container( &iter, DBUS_TYPE_ARRAY, "{sv}", &dict );
    if (params->badge_count >= 0)
    {
        DBusMessageIter entry, variant;
        dbus_uint32_t count = params->badge_count;
        dbus_message_iter_open_container( &dict, DBUS_TYPE_DICT_ENTRY, NULL, &entry );
        dbus_message_iter_append_basic( &entry, DBUS_TYPE_STRING, &badge_key );
        dbus_message_iter_open_container( &entry, DBUS_TYPE_VARIANT, "u", &variant );
        dbus_message_iter_append_basic( &variant, DBUS_TYPE_UINT32, &count );
        dbus_message_iter_close_container( &entry, &variant );
        dbus_message_iter_close_container( &dict, &entry );
    }
    dbus_message_iter_close_container( &iter, &dict );

    dbus_message_iter_append_basic( &iter, DBUS_TYPE_INT32, &timeout );

    reply = dbus_connection_send_with_reply_and_block( bus, msg, NOTIFY_TIMEOUT_MS, &error );
    dbus_message_unref( msg );
    free( app_name ); free( icon ); free( summary ); free( body );

    if (!reply)
    {
        TRACE( "Notify failed: %s\n", error.message );
        dbus_error_free( &error );
        return STATUS_NO_SUCH_DEVICE;
    }

    params->id = 0;
    if (dbus_message_get_type( reply ) == DBUS_MESSAGE_TYPE_METHOD_RETURN )
    {
        DBusMessageIter riter;
        if (dbus_message_iter_init( reply, &riter )
                && dbus_message_iter_get_arg_type( &riter ) == DBUS_TYPE_UINT32 )
            dbus_message_iter_get_basic( &riter, &params->id );
    }
    dbus_message_unref( reply );
    return STATUS_SUCCESS;
}

static NTSTATUS fd_close( void *args )
{
    struct fd_close_params *params = args;
    DBusConnection *bus = get_session_bus();
    DBusMessage *msg, *reply;
    DBusError error;

    if (!bus || !params->id) return STATUS_NO_SUCH_DEVICE;

    dbus_error_init( &error );
    if (!(msg = dbus_message_new_method_call( "org.freedesktop.Notifications", "/org/freedesktop/Notifications",
                                              "org.freedesktop.Notifications", "CloseNotification" )))
        return STATUS_NO_MEMORY;

    dbus_message_append_args( msg, DBUS_TYPE_UINT32, &params->id, DBUS_TYPE_INVALID );
    reply = dbus_connection_send_with_reply_and_block( bus, msg, NOTIFY_TIMEOUT_MS, &error );
    dbus_message_unref( msg );
    if (!reply)
    {
        dbus_error_free( &error );
        return STATUS_NO_SUCH_DEVICE;
    }
    dbus_message_unref( reply );
    return STATUS_SUCCESS;
}

static NTSTATUS fd_event( void *args )
{
    struct fd_event_params *params = args;

    params->got_event = FALSE;
    pthread_mutex_lock( &event_lock );
    if (event_head != event_tail)
    {
        params->id = event_ring[event_tail].id;
        params->reason = event_ring[event_tail].reason;
        params->got_event = TRUE;
        event_tail = (event_tail + 1) % MAX_EVENTS;
    }
    pthread_mutex_unlock( &event_lock );

    if (params->got_event && session_bus)
        dbus_connection_flush( session_bus ); /* pump the pipe so queued signals reach the filter */

    return STATUS_SUCCESS;
}

NTSTATUS twinapi_appcore_unix_call( unixlib_handle_t handle, unsigned int func, void *args )
{
    switch (func)
    {
    case unix_fd_notify: return fd_notify( args );
    case unix_fd_close:  return fd_close( args );
    case unix_fd_event:  return fd_event( args );
    default: return STATUS_INVALID_PARAMETER;
    }
}
