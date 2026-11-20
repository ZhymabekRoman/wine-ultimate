/*
 * windows.media.mediacontrol unixlib interface (MPRIS2 / D-Bus bridge)
 *
 * Copyright (c) 2026 Wine project
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */
#ifndef __WINE_WINDOWS_MEDIA_MEDIACONTROL_UNIXLIB_H
#define __WINE_WINDOWS_MEDIA_MEDIACONTROL_UNIXLIB_H

#include <stdarg.h>

#include "windef.h"
#include "winbase.h"
#include "wine/unixlib.h"

struct mpris_claim_params
{
    const WCHAR *identity;   /*[in]  MPRIS Identity property, NULL for default */
};

struct mpris_status_params
{
    INT32 status;            /*[in]  MediaPlaybackStatus_Closed..Paused */
};

struct mpris_metadata_params
{
    const WCHAR *title;      /*[in]  xesam:title, NULL or empty to omit */
    const WCHAR *artist;     /*[in]  xesam:artist, NULL or empty to omit */
    const WCHAR *album;      /*[in]  xesam:album, NULL or empty to omit */
    const WCHAR *album_artist; /*[in] xesam:albumArtist, NULL or empty to omit */
};

struct mpris_caps_params
{
    BOOLEAN can_play;        /*[in]  CanPlay */
    BOOLEAN can_pause;       /*[in]  CanPause */
    BOOLEAN can_next;        /*[in]  CanGoNext */
    BOOLEAN can_prev;        /*[in]  CanGoPrevious */
    BOOLEAN can_control;     /*[in]  CanControl */
};

struct mpris_event_params
{
    INT32 timeout_ms;        /*[in]  max time to pump the D-Bus connection */
    INT32 button;            /*[out] SystemMediaTransportControlsButton_* value */
    BOOLEAN got_event;       /*[out] TRUE when a queued button press was returned */
};

enum unix_funcs
{
    unix_mpris_claim,
    unix_mpris_release,
    unix_mpris_set_status,
    unix_mpris_set_metadata,
    unix_mpris_set_caps,
    unix_mpris_poll,
    unix_funcs_count
};

#define MEDIACONTROL_CALL( func, params ) WINE_UNIX_CALL( unix_ ## func, params )

#endif /* __WINE_WINDOWS_MEDIA_MEDIACONTROL_UNIXLIB_H */
