/*
 * twinapi.appcore unixlib interface (FreeDesktop Notifications bridge)
 *
 * Copyright (c) 2026 Wine project
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */
#ifndef __WINE_TWINAPI_APPCORE_UNIXLIB_H
#define __WINE_TWINAPI_APPCORE_UNIXLIB_H

#include <stdarg.h>

#include "windef.h"
#include "winbase.h"
#include "wine/unixlib.h"

struct fd_notify_params
{
    const WCHAR *app_name;   /*[in]  Notify() app_name argument */
    const WCHAR *icon;       /*[in]  Notify() app_icon argument, may be NULL */
    const WCHAR *summary;    /*[in]  Notify() summary argument */
    const WCHAR *body;       /*[in]  Notify() body argument */
    INT32 badge_count;       /*[in]  badge-count hint, -1 to omit */
    UINT32 replaces_id;      /*[in]  notification id to replace, 0 for none */
    UINT32 id;               /*[out] notification daemon-assigned id */
};

struct fd_close_params
{
    UINT32 id;               /*[in]  daemon id returned by Notify() */
};

struct fd_event_params
{
    UINT32 id;               /*[out] daemon id the event refers to */
    INT32 reason;            /*[out] 1 expired, 2 user (click/action), 3 closed */
    BOOLEAN got_event;       /*[out] TRUE when a queued event was returned */
};

enum unix_funcs
{
    unix_fd_notify,
    unix_fd_close,
    unix_fd_event,
    unix_funcs_count
};

#define TWINAPI_CALL( func, params ) WINE_UNIX_CALL( unix_ ## func, params )

#endif /* __WINE_TWINAPI_APPCORE_UNIXLIB_H */
