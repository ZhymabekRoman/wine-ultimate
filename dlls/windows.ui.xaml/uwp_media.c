/* WinRT Windows.UI.Xaml MediaPlayerElement & Media Foundation Video Decoder
 *
 * Copyright (C) 2026 Wine project
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <excpt.h>
#include "wine/exception.h"
#include "private.h"
#include "weakreference.h"
#include "mfapi.h"
#include "mfidl.h"
#include "mfreadwrite.h"
#define WIDL_using_Windows_ApplicationModel
#define WIDL_using_Windows_Management_Deployment
#include "windows.management.deployment.h"
#define WIDL_using_Windows_UI_Composition
#include "windows.ui.composition.h"
#include "uwp_private.h"

WINE_DEFAULT_DEBUG_CHANNEL(xaml);

static HRESULT WINAPI uwp_mpe_get_Source( void *iface, IInspectable **out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!out) return E_POINTER;
    *out = (impl && impl->media) ? impl->media->media_source : NULL;
    if (*out) IInspectable_AddRef( *out );
    return S_OK;
}

static HRESULT WINAPI uwp_mpe_put_Source( void *iface, IInspectable *val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    struct uwp_media_ext *m = uwp_elem_ensure_media( impl );
    if (!m) return S_OK;
    if (val) IInspectable_AddRef( val );
    if (m->media_source) IInspectable_Release( m->media_source );
    m->media_source = val;
    InterlockedExchange( &m->video_started, 0 );
    uwp_invalidate_window();
    return S_OK;
}

static HRESULT WINAPI uwp_mpe_get_TransportControls( void *iface, IInspectable **out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!out) return E_POINTER;
    *out = (impl && impl->media) ? impl->media->media_transport_controls : NULL;
    if (*out) IInspectable_AddRef( *out );
    return S_OK;
}

static HRESULT WINAPI uwp_mpe_put_TransportControls( void *iface, IInspectable *val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    struct uwp_media_ext *m = uwp_elem_ensure_media( impl );
    if (!m) return S_OK;
    if (val) IInspectable_AddRef( val );
    if (m->media_transport_controls) IInspectable_Release( m->media_transport_controls );
    m->media_transport_controls = val;
    return S_OK;
}

static HRESULT WINAPI uwp_mpe_get_AreTransportControlsEnabled( void *iface, boolean *out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (out) *out = (impl && impl->media) ? impl->media->media_transport_enabled : FALSE;
    return S_OK;
}

static HRESULT WINAPI uwp_mpe_put_AreTransportControlsEnabled( void *iface, boolean val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    struct uwp_media_ext *m = uwp_elem_ensure_media( impl );
    if (m) m->media_transport_enabled = val;
    return S_OK;
}

static HRESULT WINAPI uwp_mpe_get_PosterSource( void *iface, IInspectable **out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!out) return E_POINTER;
    *out = (impl && impl->media) ? impl->media->media_poster_source : NULL;
    if (*out) IInspectable_AddRef( *out );
    return S_OK;
}

static HRESULT WINAPI uwp_mpe_put_PosterSource( void *iface, IInspectable *val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    struct uwp_media_ext *m = uwp_elem_ensure_media( impl );
    if (!m) return S_OK;
    if (val) IInspectable_AddRef( val );
    if (m->media_poster_source) IInspectable_Release( m->media_poster_source );
    m->media_poster_source = val;
    return S_OK;
}

static HRESULT WINAPI uwp_mpe_get_Stretch( void *iface, int *out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (out) *out = uwp_elem_media_stretch( impl );
    return S_OK;
}

static HRESULT WINAPI uwp_mpe_put_Stretch( void *iface, int val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    struct uwp_media_ext *m = uwp_elem_ensure_media( impl );
    if (m) m->media_stretch = val;
    return S_OK;
}

static HRESULT WINAPI uwp_mpe_get_AutoPlay( void *iface, boolean *out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (out) *out = (impl && impl->media) ? impl->media->media_auto_play : FALSE;
    return S_OK;
}

static HRESULT WINAPI uwp_mpe_put_AutoPlay( void *iface, boolean val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    struct uwp_media_ext *m = uwp_elem_ensure_media( impl );
    if (m) m->media_auto_play = val;
    return S_OK;
}

static HRESULT WINAPI uwp_mpe_get_IsFullWindow( void *iface, boolean *out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (out) *out = (impl && impl->media) ? impl->media->media_is_full_window : FALSE;
    return S_OK;
}

static HRESULT WINAPI uwp_mpe_put_IsFullWindow( void *iface, boolean val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    struct uwp_media_ext *m = uwp_elem_ensure_media( impl );
    if (m) m->media_is_full_window = val;
    return S_OK;
}

static HRESULT WINAPI uwp_mpe_get_MediaPlayer( void *iface, IInspectable **out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    struct uwp_media_ext *m;
    if (!out) return E_POINTER;
    m = uwp_elem_ensure_media( impl );
    if (m && !m->media_player)
    {
        HSTRING hcls;
        if (SUCCEEDED(WindowsCreateString( L"Windows.Media.Playback.MediaPlayer", 34, &hcls )))
        {
            RoActivateInstance( hcls, &m->media_player );
            WindowsDeleteString( hcls );
        }
    }
    *out = m ? m->media_player : NULL;
    if (*out) IInspectable_AddRef( *out );
    return S_OK;
}

static HRESULT WINAPI uwp_mpe_SetMediaPlayer( void *iface, IInspectable *val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    struct uwp_media_ext *m = uwp_elem_ensure_media( impl );
    if (!m) return S_OK;
    if (val) IInspectable_AddRef( val );
    if (m->media_player) IInspectable_Release( m->media_player );
    m->media_player = val;
    InterlockedExchange( &m->video_started, 0 );
    uwp_invalidate_window();
    return S_OK;
}

const void *uwp_mpe_vtbl[24] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = uwp_mpe_get_Source,
    [7] = uwp_mpe_put_Source,
    [8] = uwp_mpe_get_TransportControls,
    [9] = uwp_mpe_put_TransportControls,
    [10] = uwp_mpe_get_AreTransportControlsEnabled,
    [11] = uwp_mpe_put_AreTransportControlsEnabled,
    [12] = uwp_mpe_get_PosterSource,
    [13] = uwp_mpe_put_PosterSource,
    [14] = uwp_mpe_get_Stretch,
    [15] = uwp_mpe_put_Stretch,
    [16] = uwp_mpe_get_AutoPlay,
    [17] = uwp_mpe_put_AutoPlay,
    [18] = uwp_mpe_get_IsFullWindow,
    [19] = uwp_mpe_put_IsFullWindow,
    [20] = uwp_mpe_get_MediaPlayer,
    [21] = uwp_mpe_SetMediaPlayer,
    [22 ... 23] = uwp_generic_noop,
};

/* MediaPlayerElement video decoding via Media Foundation (IMFSourceReader) */

static const GUID IID_IMediaPlayerSource2_mpe =
    { 0x82449b9f, 0x7322, 0x4c0b, { 0xb0, 0x3b, 0x3e, 0x69, 0xa4, 0x82, 0x60, 0xc5 } };
static const GUID IID_IMediaSource4_mpe =
    { 0xbdafad57, 0x8eff, 0x4c63, { 0x85, 0xa6, 0x84, 0xde, 0x0a, 0xe3, 0xe4, 0xf2 } };
static const GUID MF_MT_MAJOR_TYPE_local =
    { 0x48eba18e, 0xf8c9, 0x4687, { 0xbf, 0x11, 0x0a, 0x74, 0xc9, 0xf9, 0x6a, 0x8f } };
static const GUID MF_MT_SUBTYPE_local =
    { 0xf7e34c9a, 0x42e8, 0x4714, { 0xb7, 0x4b, 0xcb, 0x29, 0xd7, 0x2c, 0x35, 0xe5 } };
static const GUID MFMediaType_Video_local =
    { 0x73646976, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 } };
static const GUID MFVideoFormat_RGB32_local =
    { 0x00000016, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 } };
static const GUID MFVideoFormat_ARGB32_local =
    { 0x00000015, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 } };
static const GUID MFVideoFormat_NV12_local =
    { 0x3231564e, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 } };
static const GUID MFVideoFormat_I420_local =
    { 0x30323449, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 } };
static const GUID MFVideoFormat_IYUV_local =
    { 0x56555949, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 } };
static const GUID MFVideoFormat_YV12_local =
    { 0x32315659, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 } };
static const GUID MF_MT_FRAME_SIZE_local =
    { 0x1652c33d, 0xd6b2, 0x4012, { 0xb8, 0x34, 0x72, 0x03, 0x08, 0x49, 0xa3, 0x7d } };
static const GUID MF_MT_DEFAULT_STRIDE_local =
    { 0x644b4e48, 0x1e02, 0x4516, { 0xb0, 0xeb, 0xc0, 0x1c, 0xa9, 0xd4, 0x9a, 0xc6 } };
static const GUID MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING_local =
    { 0xfb394f3d, 0xccf1, 0x42ee, { 0xbb, 0xb3, 0xf9, 0xb8, 0x45, 0xd5, 0x68, 0x1d } };

typedef HRESULT (WINAPI *pfn_MFStartup)( ULONG, DWORD );
typedef HRESULT (WINAPI *pfn_MFShutdown)( void );
typedef HRESULT (WINAPI *pfn_MFCreateMediaType)( IMFMediaType ** );
typedef HRESULT (WINAPI *pfn_MFCreateAttributes)( IMFAttributes **, UINT32 );
typedef HRESULT (WINAPI *pfn_MFCreateSourceReaderFromURL)( LPCWSTR, IMFAttributes *, IMFSourceReader ** );

static BOOL uwp_mpe_extract_uri( struct uwp_element *el, WCHAR *out, size_t cap )
{
    IInspectable *src = NULL;
    BOOL release_src = FALSE;

    if (!el || !out || !cap) return FALSE;
    out[0] = 0;

    if (el->media && el->media->media_source)
    {
        src = el->media->media_source;
    }
    else if (el->media && el->media->media_player)
    {
        void *src2 = NULL;
        if (SUCCEEDED(IInspectable_QueryInterface( el->media->media_player, &IID_IMediaPlayerSource2_mpe, &src2 )) && src2)
        {
            const struct abi_media_player_source2 *amps = (const struct abi_media_player_source2 *)src2;
            if (amps->lpVtbl->get_Source && SUCCEEDED(amps->lpVtbl->get_Source( src2, &src )) && src)
                release_src = TRUE;
            IUnknown_Release( (IUnknown *)src2 );
        }
    }

    if (src)
    {
        void *ms4 = NULL;
        if (SUCCEEDED(IInspectable_QueryInterface( src, &IID_IMediaSource4_mpe, &ms4 )) && ms4)
        {
            const struct abi_media_source4 *ams4 = (const struct abi_media_source4 *)ms4;
            IInspectable *uri_obj = NULL;
            if (ams4->lpVtbl->get_Uri && SUCCEEDED(ams4->lpVtbl->get_Uri( ms4, &uri_obj )) && uri_obj)
            {
                uwp_extract_uri_string( uri_obj, out, cap );
                IInspectable_Release( uri_obj );
            }
            IUnknown_Release( (IUnknown *)ms4 );
        }
        if (!out[0])
            uwp_extract_uri_string( src, out, cap );
        if (release_src)
            IInspectable_Release( src );
    }
    return out[0] != 0;
}

static BOOL uwp_resolve_media_path( const WCHAR *uri, WCHAR *out_path, size_t cap )
{
    const WCHAR *p = uri;
    WCHAR rel[MAX_PATH] = {0}, base_dir[MAX_PATH] = {0};
    WCHAR *slash;
    size_t i;

    if (!uri || !uri[0] || !out_path || !cap) return FALSE;
    out_path[0] = 0;

    if (!wcsncmp( p, L"ms-appx:///", 11 ) || !wcsncmp( p, L"ms-appx://", 10 ))
    {
        p += (!wcsncmp( p, L"ms-appx:///", 11 )) ? 11 : 10;
        while (*p == '/' || *p == '\\') p++;
        lstrcpynW( rel, p, ARRAY_SIZE(rel) );
        for (i = 0; rel[i]; i++) if (rel[i] == '/') rel[i] = '\\';
        GetModuleFileNameW( NULL, base_dir, ARRAY_SIZE(base_dir) );
        slash = wcsrchr( base_dir, '\\' );
        if (slash) *(slash + 1) = 0;
        swprintf( out_path, cap, L"%s%s", base_dir, rel );
        return GetFileAttributesW( out_path ) != INVALID_FILE_ATTRIBUTES;
    }
    if (!wcsncmp( p, L"file:///", 8 ))
    {
        p += 8;
        lstrcpynW( out_path, p, cap );
        for (i = 0; out_path[i]; i++) if (out_path[i] == '/') out_path[i] = '\\';
        return GetFileAttributesW( out_path ) != INVALID_FILE_ATTRIBUTES;
    }
    lstrcpynW( out_path, uri, cap );
    return TRUE;
}

static inline BYTE clamp_u8( int v )
{
    if (v < 0) return 0;
    if (v > 255) return 255;
    return (BYTE)v;
}

static BOOL uwp_decode_rgb32_frame( const BYTE *raw, DWORD cur_len, UINT src_w, UINT src_h,
                                    LONG stride, UINT dst_w, UINT dst_h, BYTE *work_buf )
{
    LONG abs_stride = stride < 0 ? -stride : stride;
    BOOL bottom_up = (stride < 0);
    UINT y, x;
    if (abs_stride < (LONG)(src_w * 4))
        abs_stride = (LONG)(src_w * 4);
    if ((DWORD)((size_t)abs_stride * src_h) > cur_len)
        return FALSE;
    for (y = 0; y < dst_h; y++)
    {
        UINT sy = (dst_h == src_h) ? y : (UINT)((UINT64)y * src_h / dst_h);
        UINT row_idx = bottom_up ? (src_h - 1 - sy) : sy;
        const BYTE *srow = raw + (size_t)row_idx * (size_t)abs_stride;
        BYTE *drow = work_buf + (size_t)y * dst_w * 4;
        for (x = 0; x < dst_w; x++)
        {
            UINT sx = (dst_w == src_w) ? x : (UINT)((UINT64)x * src_w / dst_w);
            drow[x * 4 + 0] = srow[sx * 4 + 0];
            drow[x * 4 + 1] = srow[sx * 4 + 1];
            drow[x * 4 + 2] = srow[sx * 4 + 2];
            drow[x * 4 + 3] = 255;
        }
    }
    return TRUE;
}

static BOOL uwp_decode_nv12_frame( const BYTE *raw, DWORD cur_len, UINT src_w, UINT src_h,
                                   LONG stride, UINT dst_w, UINT dst_h, BYTE *work_buf )
{
    UINT y_stride = (stride > 0 && (UINT)stride >= src_w) ? (UINT)stride : src_w;
    const BYTE *y_plane = raw;
    const BYTE *uv_plane = raw + (size_t)y_stride * src_h;
    UINT y, x;
    if ((size_t)y_stride * src_h + (size_t)y_stride * (src_h / 2) > cur_len)
        return FALSE;
    for (y = 0; y < dst_h; y++)
    {
        UINT sy = (UINT)((UINT64)y * src_h / dst_h);
        const BYTE *yrow = y_plane + (size_t)sy * y_stride;
        const BYTE *uvrow = uv_plane + (size_t)(sy >> 1) * y_stride;
        BYTE *drow = work_buf + (size_t)y * dst_w * 4;
        for (x = 0; x < dst_w; x++)
        {
            UINT sx = (UINT)((UINT64)x * src_w / dst_w);
            int Y = (int)yrow[sx] - 16;
            int U = (int)uvrow[(sx & ~1u)] - 128;
            int V = (int)uvrow[(sx & ~1u) + 1] - 128;
            int C = Y < 0 ? 0 : Y * 298;
            drow[x * 4 + 2] = clamp_u8( (C + 409 * V + 128) >> 8 );
            drow[x * 4 + 1] = clamp_u8( (C - 100 * U - 208 * V + 128) >> 8 );
            drow[x * 4 + 0] = clamp_u8( (C + 516 * U + 128) >> 8 );
            drow[x * 4 + 3] = 255;
        }
    }
    return TRUE;
}

static BOOL uwp_decode_planar420_frame( const BYTE *raw, DWORD cur_len, UINT src_w, UINT src_h,
                                        LONG stride, UINT dst_w, UINT dst_h, BOOL swap_uv, BYTE *work_buf )
{
    UINT y_stride = (stride > 0 && (UINT)stride >= src_w) ? (UINT)stride : src_w;
    UINT uv_stride = y_stride / 2;
    const BYTE *y_plane = raw;
    const BYTE *p1 = raw + (size_t)y_stride * src_h;
    const BYTE *p2 = p1 + (size_t)uv_stride * (src_h / 2);
    const BYTE *u_plane = swap_uv ? p2 : p1;
    const BYTE *v_plane = swap_uv ? p1 : p2;
    UINT y, x;
    if ((size_t)y_stride * src_h + (size_t)uv_stride * src_h > cur_len)
        return FALSE;
    for (y = 0; y < dst_h; y++)
    {
        UINT sy = (UINT)((UINT64)y * src_h / dst_h);
        const BYTE *yrow = y_plane + (size_t)sy * y_stride;
        const BYTE *urow = u_plane + (size_t)(sy >> 1) * uv_stride;
        const BYTE *vrow = v_plane + (size_t)(sy >> 1) * uv_stride;
        BYTE *drow = work_buf + (size_t)y * dst_w * 4;
        for (x = 0; x < dst_w; x++)
        {
            UINT sx = (UINT)((UINT64)x * src_w / dst_w);
            int Y = (int)yrow[sx] - 16;
            int U = (int)urow[sx >> 1] - 128;
            int V = (int)vrow[sx >> 1] - 128;
            int C = Y < 0 ? 0 : Y * 298;
            drow[x * 4 + 2] = clamp_u8( (C + 409 * V + 128) >> 8 );
            drow[x * 4 + 1] = clamp_u8( (C - 100 * U - 208 * V + 128) >> 8 );
            drow[x * 4 + 0] = clamp_u8( (C + 516 * U + 128) >> 8 );
            drow[x * 4 + 3] = 255;
        }
    }
    return TRUE;
}

static void uwp_publish_video_frame( struct uwp_element *el, const BYTE *work_buf, UINT dst_w, UINT dst_h )
{
    AcquireSRWLockExclusive( &el->media->video_lock );
    if (!el->media->video_pixels || el->media->video_w != dst_w || el->media->video_h != dst_h)
    {
        free( el->media->video_pixels );
        el->media->video_pixels = malloc( (size_t)dst_w * dst_h * 4 );
        el->media->video_w = dst_w;
        el->media->video_h = dst_h;
    }
    if (el->media->video_pixels)
    {
        memcpy( el->media->video_pixels, work_buf, (size_t)dst_w * dst_h * 4 );
        el->media->video_frame_seq++;
    }
    ReleaseSRWLockExclusive( &el->media->video_lock );
    uwp_invalidate_window();
}

static DWORD WINAPI uwp_media_decode_worker( void *param )
{
    struct uwp_element *el = (struct uwp_element *)param;
    HMODULE h_mfplat = NULL, h_mfrw = NULL;
    pfn_MFStartup pMFStartup;
    pfn_MFShutdown pMFShutdown;
    pfn_MFCreateMediaType pMFCreateMediaType;
    pfn_MFCreateAttributes pMFCreateAttributes;
    pfn_MFCreateSourceReaderFromURL pMFCreateSourceReaderFromURL;
    IMFAttributes *attrs = NULL;
    IMFSourceReader *reader = NULL;
    IMFMediaType *req_type = NULL, *cur_type = NULL;
    WCHAR path[MAX_PATH] = {0};
    GUID out_subtype = { 0 };
    UINT64 frame_sz = 0;
    UINT src_w = 0, src_h = 0, dst_w = 0, dst_h = 0;
    LONG stride = 0;
    UINT32 u32_stride = 0;
    BYTE *work_buf = NULL;
    BOOL mf_started = FALSE, logged_first = FALSE;
    HRESULT hr;

    if (!el || !el->media || !uwp_resolve_media_path( el->media->video_uri, path, ARRAY_SIZE(path) ))
    {
        WARN( "uwp_media_decode_worker: failed to resolve path for %s\n", (el && el->media) ? debugstr_w(el->media->video_uri) : "null" );
        return 0;
    }

    CoInitializeEx( NULL, COINIT_MULTITHREADED );
    h_mfplat = LoadLibraryW( L"mfplat.dll" );
    h_mfrw = LoadLibraryW( L"mfreadwrite.dll" );
    if (!h_mfplat || !h_mfrw)
    {
        WARN( "uwp_media_decode_worker: mfplat/mfreadwrite unavailable\n" );
        goto done;
    }

    pMFStartup = (pfn_MFStartup)GetProcAddress( h_mfplat, "MFStartup" );
    pMFShutdown = (pfn_MFShutdown)GetProcAddress( h_mfplat, "MFShutdown" );
    pMFCreateMediaType = (pfn_MFCreateMediaType)GetProcAddress( h_mfplat, "MFCreateMediaType" );
    pMFCreateAttributes = (pfn_MFCreateAttributes)GetProcAddress( h_mfplat, "MFCreateAttributes" );
    pMFCreateSourceReaderFromURL = (pfn_MFCreateSourceReaderFromURL)GetProcAddress( h_mfrw, "MFCreateSourceReaderFromURL" );
    if (!pMFStartup || !pMFCreateMediaType || !pMFCreateSourceReaderFromURL)
        goto done;

    hr = pMFStartup( MF_VERSION, MFSTARTUP_NOSOCKET );
    if (FAILED(hr))
    {
        WARN( "uwp_media_decode_worker: MFStartup failed %#lx\n", hr );
        goto done;
    }
    mf_started = TRUE;

    if (pMFCreateAttributes && SUCCEEDED(pMFCreateAttributes( &attrs, 1 )) && attrs)
        IMFAttributes_SetUINT32( attrs, &MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING_local, TRUE );

    hr = pMFCreateSourceReaderFromURL( path, attrs, &reader );
    if (FAILED(hr) || !reader)
    {
        WARN( "uwp_media_decode_worker: MFCreateSourceReaderFromURL(%s) failed %#lx\n", debugstr_w(path), hr );
        goto done;
    }

    hr = pMFCreateMediaType( &req_type );
    if (FAILED(hr) || !req_type) goto done;
    IMFMediaType_SetGUID( req_type, &MF_MT_MAJOR_TYPE_local, &MFMediaType_Video_local );
    IMFMediaType_SetGUID( req_type, &MF_MT_SUBTYPE_local, &MFVideoFormat_RGB32_local );
    hr = IMFSourceReader_SetCurrentMediaType( reader, MF_SOURCE_READER_FIRST_VIDEO_STREAM, NULL, req_type );
    if (FAILED(hr))
    {
        IMFMediaType_SetGUID( req_type, &MF_MT_SUBTYPE_local, &MFVideoFormat_NV12_local );
        hr = IMFSourceReader_SetCurrentMediaType( reader, MF_SOURCE_READER_FIRST_VIDEO_STREAM, NULL, req_type );
        if (FAILED(hr))
        {
            WARN( "uwp_media_decode_worker: SetCurrentMediaType(RGB32/NV12) failed %#lx\n", hr );
            goto done;
        }
    }

    hr = IMFSourceReader_GetCurrentMediaType( reader, MF_SOURCE_READER_FIRST_VIDEO_STREAM, &cur_type );
    if (FAILED(hr) || !cur_type) goto done;
    IMFMediaType_GetGUID( cur_type, &MF_MT_SUBTYPE_local, &out_subtype );
    if (FAILED(IMFMediaType_GetUINT64( cur_type, &MF_MT_FRAME_SIZE_local, &frame_sz )))
        goto done;
    src_w = (UINT)(frame_sz >> 32);
    src_h = (UINT)(frame_sz & 0xffffffffu);
    if (!src_w || !src_h || src_w > 4096 || src_h > 4096) goto done;

    if (SUCCEEDED(IMFMediaType_GetUINT32( cur_type, &MF_MT_DEFAULT_STRIDE_local, &u32_stride )))
        stride = (LONG)u32_stride;
    else
        stride = (LONG)(src_w * 4);

    dst_w = src_w;
    dst_h = src_h;
    if (dst_w > 960 || dst_h > 720)
    {
        double sx = 960.0 / (double)dst_w;
        double sy = 720.0 / (double)dst_h;
        double sc = (sx < sy) ? sx : sy;
        dst_w = max( 1u, (UINT)((double)src_w * sc + 0.5) );
        dst_h = max( 1u, (UINT)((double)src_h * sc + 0.5) );
    }

    work_buf = malloc( (size_t)dst_w * dst_h * 4 );
    if (!work_buf) goto done;

    TRACE( "uwp_media_decode_worker: opened %s (%ux%u -> %ux%u, stride=%ld, subtype=%s)\n",
           debugstr_w(path), src_w, src_h, dst_w, dst_h, stride, debugstr_guid(&out_subtype) );

    while (!InterlockedCompareExchange( &el->media->video_stop, 0, 0 ))
    {
        DWORD stream_idx = 0, flags = 0;
        LONGLONG ts = 0;
        IMFSample *sample = NULL;

        if (el->visibility != 0)
        {
            if (el->media->video_wake_event)
                WaitForSingleObject( el->media->video_wake_event, 1000 );
            else
                Sleep( 100 );
            continue;
        }

        hr = IMFSourceReader_ReadSample( reader, MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0,
                                         &stream_idx, &flags, &ts, &sample );
        if (FAILED(hr))
        {
            WARN( "uwp_media_decode_worker: ReadSample failed %#lx\n", hr );
            break;
        }
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM)
        {
            static const GUID guid_null = { 0 };
            PROPVARIANT pos;
            if (sample) IMFSample_Release( sample );
            PropVariantInit( &pos );
            pos.vt = VT_I8;
            pos.hVal.QuadPart = 0;
            hr = IMFSourceReader_SetCurrentPosition( reader, &guid_null, &pos );
            if (FAILED(hr)) break;
            continue;
        }
        if (sample)
        {
            IMFMediaBuffer *buf = NULL;
            if (SUCCEEDED(IMFSample_ConvertToContiguousBuffer( sample, &buf )) && buf)
            {
                BYTE *raw = NULL;
                DWORD max_len = 0, cur_len = 0;
                if (SUCCEEDED(IMFMediaBuffer_Lock( buf, &raw, &max_len, &cur_len )) && raw && cur_len > 0)
                {
                    BOOL is_rgb32 = (cur_len >= (DWORD)((size_t)src_w * src_h * 4)) &&
                                    (IsEqualGUID( &out_subtype, &MFVideoFormat_RGB32_local ) ||
                                     IsEqualGUID( &out_subtype, &MFVideoFormat_ARGB32_local ) ||
                                     (cur_len >= (DWORD)((size_t)src_w * src_h * 4)));
                    BOOL is_nv12 = !is_rgb32 && (cur_len >= (DWORD)((size_t)src_w * src_h * 3 / 2)) &&
                                   IsEqualGUID( &out_subtype, &MFVideoFormat_NV12_local );
                    BOOL is_planar420 = !is_rgb32 && !is_nv12 && (cur_len >= (DWORD)((size_t)src_w * src_h * 3 / 2)) &&
                                        (IsEqualGUID( &out_subtype, &MFVideoFormat_I420_local ) ||
                                         IsEqualGUID( &out_subtype, &MFVideoFormat_IYUV_local ) ||
                                         IsEqualGUID( &out_subtype, &MFVideoFormat_YV12_local ));
                    BOOL decoded_ok = FALSE;

                    if (!logged_first)
                    {
                        logged_first = TRUE;
                        TRACE( "uwp_media_decode_worker: first frame cur_len=%lu rgb32=%d nv12=%d planar420=%d stride=%ld subtype=%s\n",
                               (unsigned long)cur_len, is_rgb32, is_nv12, is_planar420, stride, debugstr_guid(&out_subtype) );
                    }

                    if (is_rgb32)
                        decoded_ok = uwp_decode_rgb32_frame( raw, cur_len, src_w, src_h, stride, dst_w, dst_h, work_buf );
                    else if (is_nv12 || (!is_planar420 && cur_len >= (DWORD)((size_t)src_w * src_h * 3 / 2)))
                        decoded_ok = uwp_decode_nv12_frame( raw, cur_len, src_w, src_h, stride, dst_w, dst_h, work_buf );
                    else if (is_planar420)
                        decoded_ok = uwp_decode_planar420_frame( raw, cur_len, src_w, src_h, stride, dst_w, dst_h,
                                                                  IsEqualGUID( &out_subtype, &MFVideoFormat_YV12_local ),
                                                                  work_buf );

                    if (decoded_ok)
                        uwp_publish_video_frame( el, work_buf, dst_w, dst_h );
                    IMFMediaBuffer_Unlock( buf );
                }
                IMFMediaBuffer_Release( buf );
            }
            IMFSample_Release( sample );
            if (el->media->video_wake_event)
                WaitForSingleObject( el->media->video_wake_event, 33 );
            else
                Sleep( 33 );
        }
    }

done:
    free( work_buf );
    if (cur_type) IMFMediaType_Release( cur_type );
    if (req_type) IMFMediaType_Release( req_type );
    if (reader) IMFSourceReader_Release( reader );
    if (attrs) IMFAttributes_Release( attrs );
    if (mf_started && pMFShutdown) pMFShutdown();
    if (h_mfrw) FreeLibrary( h_mfrw );
    if (h_mfplat) FreeLibrary( h_mfplat );
    CoUninitialize();
    return 0;
}

void uwp_media_ensure_started( struct uwp_element *el )
{
    WCHAR uri[512] = {0};
    struct uwp_media_ext *m;

    if (!el) return;
    m = uwp_elem_ensure_media( el );
    if (!m) return;
    if (InterlockedCompareExchange( &m->video_started, 0, 0 ) != 0)
        return;
    if (!uwp_mpe_extract_uri( el, uri, ARRAY_SIZE(uri) ))
        return;
    if (InterlockedCompareExchange( &m->video_started, 1, 0 ) != 0)
        return;

    lstrcpynW( m->video_uri, uri, ARRAY_SIZE(m->video_uri) );
    InterlockedExchange( &m->video_stop, 0 );
    if (!m->video_wake_event)
        m->video_wake_event = CreateEventW( NULL, FALSE, FALSE, NULL );
    m->video_thread = CreateThread( NULL, 0, uwp_media_decode_worker, el, 0, NULL );
    if (!m->video_thread)
        InterlockedExchange( &m->video_started, 0 );
}
