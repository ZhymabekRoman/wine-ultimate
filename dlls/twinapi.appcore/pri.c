/*
 * PRI v2 ("mrm_pri2") reader for twinapi.appcore.
 *
 * Copyright (C) 2026 Wine Developers
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

#include <stdarg.h>
#include <string.h>
#include "windef.h"
#include "winbase.h"
#include "wine/debug.h"

#include "pri.h"

WINE_DEFAULT_DEBUG_CHANNEL(resources);

#include "wine/winrt_pri.h"

pri_index *pri_load( const WCHAR *path )
{
    return winrt_pri_load( path );
}

void pri_release( pri_index *index )
{
    winrt_pri_release( index );
}

UINT32 pri_top_count( pri_index *index )
{
    return winrt_pri_top_count( index );
}

BOOL pri_top_name( pri_index *index, UINT32 index_, WCHAR *buffer, DWORD len )
{
    return winrt_pri_top_name( index, index_, buffer, len );
}

UINT32 pri_name_count( pri_index *index )
{
    return winrt_pri_name_count( index );
}

BOOL pri_name( pri_index *index, UINT32 index_, WCHAR *buffer, DWORD len )
{
    return winrt_pri_name( index, index_, buffer, len );
}

UINT32 pri_map_count( pri_index *index, const WCHAR *prefix )
{
    return winrt_pri_map_count( index, prefix );
}

BOOL pri_map_fullname( pri_index *index, const WCHAR *prefix, UINT32 i, WCHAR *buffer, DWORD len )
{
    return winrt_pri_map_fullname( index, prefix, i, buffer, len );
}

BOOL pri_map_get( pri_index *index, const WCHAR *prefix, UINT32 i, const pri_qual *ctx, UINT32 ctxcount,
                  WCHAR *name_buf, DWORD name_len, WCHAR **value_out, pri_qual **quals_out, UINT32 *qcount_out )
{
    return winrt_pri_map_get( index, prefix, i, ctx, ctxcount, name_buf, name_len, value_out, quals_out, qcount_out );
}

BOOL pri_lookup( pri_index *index, const WCHAR *key, const pri_qual *ctx, UINT32 ctxcount,
                 WCHAR *name_buf, DWORD name_len, WCHAR **value_out, pri_qual **quals_out, UINT32 *qcount_out )
{
    return winrt_pri_lookup( index, key, ctx, ctxcount, name_buf, name_len, value_out, quals_out, qcount_out );
}

void pri_free_result( WCHAR *value, pri_qual *quals )
{
    winrt_pri_free_result( value, quals );
}
