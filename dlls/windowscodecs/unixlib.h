/*
 * WebP decoder Unix interface
 *
 * Copyright 2026 Wine Project
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#ifndef __WINE_WINDOWSCODECS_UNIXLIB_H
#define __WINE_WINDOWSCODECS_UNIXLIB_H

#include "windef.h"
#include "wine/unixlib.h"

struct webp_get_info_params
{
    const BYTE *data;
    UINT data_size;
    UINT *width;
    UINT *height;
    BOOL *has_alpha;
    BOOL *has_animation;
    UINT *frame_count;
};

struct webp_decode_params
{
    const BYTE *data;
    UINT data_size;
    BYTE *output;
    UINT output_size;
    UINT stride;
};

enum windowscodecs_funcs
{
    unix_webp_get_info,
    unix_webp_decode,
    unix_funcs_count
};

#endif /* __WINE_WINDOWSCODECS_UNIXLIB_H */
