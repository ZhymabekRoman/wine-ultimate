/*
 * d2d1 unixlib interface (librsvg + libcairo SVG rasterizer)
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */
#ifndef __WINE_D2D1_UNIXLIB_H
#define __WINE_D2D1_UNIXLIB_H

#include <stdarg.h>

#include "windef.h"
#include "winbase.h"
#include "wine/unixlib.h"

struct d2d1_svg_dimensions_params
{
    const void *xml;
    UINT32 xml_len;
    BOOL has_vb;
    float vb_x;
    float vb_y;
    float vb_w;
    float vb_h;
};

struct d2d1_svg_rasterize_params
{
    const void *xml;
    UINT32 xml_len;
    UINT32 width;
    UINT32 height;
    void *out_bgra;
    UINT32 out_stride;
};

enum d2d1_unix_funcs
{
    unix_svg_get_dimensions,
    unix_svg_rasterize,
    unix_funcs_count
};

#define D2D1_UNIX_CALL( func, params ) WINE_UNIX_CALL( unix_ ## func, params )

#endif /* __WINE_D2D1_UNIXLIB_H */
