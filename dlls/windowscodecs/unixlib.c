/*
 * WebP decoder Unix library implementation
 *
 * Copyright 2026 Wine Project
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
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <webp/decode.h>
#include <webp/demux.h>

#include "ntstatus.h"
#include "windef.h"
#include "winternl.h"
#include "wine/debug.h"
#include "unixlib.h"

WINE_DEFAULT_DEBUG_CHANNEL(wincodecs);

static NTSTATUS webp_get_info( void *args )
{
    struct webp_get_info_params *params = args;
    WebPBitstreamFeatures features;
    VP8StatusCode status;

    if (!params->data || !params->data_size) return STATUS_INVALID_PARAMETER;

    status = WebPGetFeatures( params->data, params->data_size, &features );
    if (status != VP8_STATUS_OK) return STATUS_UNSUCCESSFUL;

    if (params->width) *params->width = features.width;
    if (params->height) *params->height = features.height;
    if (params->has_alpha) *params->has_alpha = features.has_alpha;
    if (params->has_animation) *params->has_animation = features.has_animation;
    if (params->frame_count)
    {
        if (features.has_animation)
        {
            WebPData webp_data = { params->data, params->data_size };
            WebPDemuxer *demux = WebPDemux( &webp_data );
            if (demux)
            {
                *params->frame_count = WebPDemuxGetI( demux, WEBP_FF_FRAME_COUNT );
                WebPDemuxDelete( demux );
            }
            else *params->frame_count = 1;
        }
        else *params->frame_count = 1;
    }
    return STATUS_SUCCESS;
}

static NTSTATUS webp_decode( void *args )
{
    struct webp_decode_params *params = args;
    uint8_t *ret;

    if (!params->data || !params->data_size || !params->output)
        return STATUS_INVALID_PARAMETER;

    ret = WebPDecodeBGRAInto( params->data, params->data_size,
                              params->output, params->output_size, params->stride );
    if (!ret) return STATUS_UNSUCCESSFUL;

    return STATUS_SUCCESS;
}

const unixlib_entry_t __wine_unix_call_funcs[] =
{
    webp_get_info,
    webp_decode,
};

C_ASSERT( ARRAYSIZE(__wine_unix_call_funcs) == unix_funcs_count );

#ifdef _WIN64
const unixlib_entry_t __wine_unix_call_wow64_funcs[] =
{
    webp_get_info,
    webp_decode,
};
#endif
