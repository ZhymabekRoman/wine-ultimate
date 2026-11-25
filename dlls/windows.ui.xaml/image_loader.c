/* WinRT Windows.UI.Xaml Color/Brush Parsing & WIC/WinHTTP Image Loader
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
#include "private.h"
#include "wincodec.h"
#include "uwp_private.h"
WINE_DEFAULT_DEBUG_CHANNEL(xaml);

COLORREF argb_to_colorref( UINT32 argb )
{
    return RGB( (argb >> 16) & 0xff, (argb >> 8) & 0xff, argb & 0xff );
}

UINT32 apply_opacity_to_argb( UINT32 argb, double opacity )
{
    BYTE a;
    if (opacity >= 0.999) return argb;
    if (opacity <= 0.0) return argb & 0x00ffffffu;
    a = (BYTE)((double)((argb >> 24) & 0xff) * opacity + 0.5);
    return (argb & 0x00ffffffu) | ((UINT32)a << 24);
}

COLORREF blend_argb_over( UINT32 fg_argb, COLORREF bg_cr )
{
    BYTE a = (BYTE)((fg_argb >> 24) & 0xff);
    BYTE r = (BYTE)((fg_argb >> 16) & 0xff);
    BYTE g = (BYTE)((fg_argb >> 8) & 0xff);
    BYTE b = (BYTE)(fg_argb & 0xff);
    BYTE br, bg, bb;
    if (a == 255) return RGB( r, g, b );
    if (a == 0) return bg_cr;
    br = GetRValue( bg_cr );
    bg = GetGValue( bg_cr );
    bb = GetBValue( bg_cr );
    return RGB( (BYTE)((r * a + br * (255 - a)) / 255),
                (BYTE)((g * a + bg * (255 - a)) / 255),
                (BYTE)((b * a + bb * (255 - a)) / 255) );
}

static int hex_nib( WCHAR c )
{
    if (c >= L'0' && c <= L'9') return c - L'0';
    if (c >= L'a' && c <= L'f') return 10 + (c - L'a');
    if (c >= L'A' && c <= L'F') return 10 + (c - L'A');
    return -1;
}

BOOL uwp_parse_color_string( const WCHAR *s, UINT32 *out_argb )
{
    size_t len;
    if (!s || !s[0] || !out_argb) return FALSE;
    while (*s == L' ' || *s == L'\t') s++;
    if (s[0] == L'#')
    {
        UINT32 v = 0;
        size_t i;
        s++;
        len = wcslen( s );
        while (len > 0 && (s[len - 1] == L' ' || s[len - 1] == L'\t')) len--;
        if (len != 3 && len != 4 && len != 6 && len != 8) return FALSE;
        for (i = 0; i < len; i++)
        {
            int n = hex_nib( s[i] );
            if (n < 0) return FALSE;
            v = (v << 4) | (UINT32)n;
        }
        if (len == 3)
        {
            UINT32 r = (v >> 8) & 0xf, g = (v >> 4) & 0xf, b = v & 0xf;
            *out_argb = 0xff000000u | (r * 0x11 << 16) | (g * 0x11 << 8) | (b * 0x11);
        }
        else if (len == 4)
        {
            UINT32 a = (v >> 12) & 0xf, r = (v >> 8) & 0xf, g = (v >> 4) & 0xf, b = v & 0xf;
            *out_argb = (a * 0x11 << 24) | (r * 0x11 << 16) | (g * 0x11 << 8) | (b * 0x11);
        }
        else if (len == 6)
            *out_argb = 0xff000000u | v;
        else
            *out_argb = v;
        return TRUE;
    }
    if (!wcsicmp( s, L"Transparent" )) { *out_argb = 0x00ffffffu; return TRUE; }
    if (!wcsicmp( s, L"Black" ))       { *out_argb = 0xff000000u; return TRUE; }
    if (!wcsicmp( s, L"White" ))       { *out_argb = 0xffffffffu; return TRUE; }
    if (!wcsicmp( s, L"Red" ))         { *out_argb = 0xffff0000u; return TRUE; }
    if (!wcsicmp( s, L"Green" ))       { *out_argb = 0xff008000u; return TRUE; }
    if (!wcsicmp( s, L"Blue" ))        { *out_argb = 0xff0000ffu; return TRUE; }
    if (!wcsicmp( s, L"Gray" ))        { *out_argb = 0xff808080u; return TRUE; }
    if (!wcsicmp( s, L"DarkGray" ))    { *out_argb = 0xffa9a9a9u; return TRUE; }
    if (!wcsicmp( s, L"LightGray" ))   { *out_argb = 0xffd3d3d3u; return TRUE; }
    if (!wcsicmp( s, L"Yellow" ))      { *out_argb = 0xffffff00u; return TRUE; }
    if (!wcsicmp( s, L"Orange" ))      { *out_argb = 0xffffa500u; return TRUE; }
    return FALSE;
}

void uwp_el_set_bg_argb( struct uwp_element *el, UINT32 argb )
{
    if (!el) return;
    el->bg_argb = argb;
    el->bg_color = argb_to_colorref( argb );
    el->bg_alpha = (BYTE)((argb >> 24) & 0xff);
    el->has_bg = TRUE;
}

void uwp_el_set_fg_argb( struct uwp_element *el, UINT32 argb )
{
    if (!el) return;
    el->fg_argb = argb;
    el->fg_color = argb_to_colorref( argb );
    el->fg_alpha = (BYTE)((argb >> 24) & 0xff);
    el->has_fg = TRUE;
}

void uwp_el_set_border_argb( struct uwp_element *el, UINT32 argb )
{
    if (!el) return;
    el->border_argb = argb;
    el->border_color = argb_to_colorref( argb );
    el->border_alpha = (BYTE)((argb >> 24) & 0xff);
    el->has_border = TRUE;
}

/* Format Segoe MDL2 Assets / private use glyphs to readable symbols if font lacks them */
const WCHAR *uwp_format_display_label( const struct uwp_element *el, WCHAR *tmp, size_t tmp_len )
{
    const WCHAR *s = el->text;
    if (!s || !s[0]) return L"";
    if (!wcscmp( s, L"SettingsButtonName" ) || !wcscmp( el->name, L"SettingsItem" )) return L"";
    if (s[1] == 0)
    {
        WCHAR ff_buf[64];
        const WCHAR *face = uwp_resolve_elem_font_family( el, ff_buf, ARRAY_SIZE(ff_buf) );
        if (face && wcsicmp( face, L"Segoe UI" ) != 0 && uwp_is_bundled_font_face( face ) &&
            xaml_gpu_font_has_glyphs( face, s ))
            return s;
        switch (s[0])
        {
        case 0xe94c: return L"%";
        case 0xe94f: return L"\x232b";
        case 0xe94a: return L"\x00f7";
        case 0xe947: return L"\x00d7";
        case 0xe949: return L"-";
        case 0xe948: return L"+";
        case 0xe94e: return L"=";
        case 0xe94d: return L"+/-";
        case 0xf7c9: return L"1/x";
        case 0xf7c8: return L"x\x00b2";
        case 0xf899: return L"\x221a";
        case 0xe700: return L"\x2630";
        case 0xe81c: return L"H";
        case 0xee49: return L"\x29c9";
        }
    }
    (void)tmp; (void)tmp_len;
    return s;
}

COLORREF g_paint_page_bg = RGB( 31, 31, 31 );

static SRWLOCK g_http_img_cache_lock = SRWLOCK_INIT;
static volatile LONG g_active_http_workers = 0;
static struct
{
    WCHAR url[512];
    BYTE *pixels;
    UINT w, h;
    BOOL done;
} g_http_img_cache[256];
static int g_http_img_cache_cnt = 0;

static BOOL uwp_wic_decode_memory( const BYTE *data, DWORD len, UINT max_dim, BYTE **out_pixels, UINT *out_w, UINT *out_h )
{
    IWICImagingFactory *wic = NULL;
    IWICStream *stream = NULL;
    IWICBitmapDecoder *dec = NULL;
    IWICBitmapFrameDecode *frame = NULL;
    IWICFormatConverter *conv = NULL;
    UINT w = 0, h = 0;
    BOOL ok = FALSE;

    if (!data || !len) return FALSE;
    if (SUCCEEDED(CoCreateInstance( &CLSID_WICImagingFactory_local, NULL, CLSCTX_INPROC_SERVER,
                                    &IID_IWICImagingFactory_local, (void **)&wic )) && wic)
    {
        if (SUCCEEDED(IWICImagingFactory_CreateStream( wic, &stream )) && stream)
        {
            if (SUCCEEDED(IWICStream_InitializeFromMemory( stream, (BYTE *)data, len )) &&
                SUCCEEDED(IWICImagingFactory_CreateDecoderFromStream( wic, (IStream *)stream, NULL,
                                                                      WICDecodeMetadataCacheOnLoad, &dec )) && dec)
            {
                if (SUCCEEDED(IWICBitmapDecoder_GetFrame( dec, 0, &frame )) && frame)
                {
                    IWICBitmapSource *src_bmp = (IWICBitmapSource *)frame;
                    IWICBitmapScaler *scaler = NULL;
                    if (SUCCEEDED(IWICBitmapFrameDecode_GetSize( frame, &w, &h )) &&
                        max_dim > 0 && (w > max_dim || h > max_dim) && w > 0 && h > 0)
                    {
                        UINT nw = w >= h ? max_dim : max( 1, (UINT)((double)max_dim * w / h) );
                        UINT nh = h >= w ? max_dim : max( 1, (UINT)((double)max_dim * h / w) );
                        if (SUCCEEDED(IWICImagingFactory_CreateBitmapScaler( wic, &scaler )) && scaler)
                        {
                            if (SUCCEEDED(IWICBitmapScaler_Initialize( scaler, src_bmp, nw, nh, WICBitmapInterpolationModeFant )))
                                src_bmp = (IWICBitmapSource *)scaler;
                            else
                            {
                                IWICBitmapScaler_Release( scaler );
                                scaler = NULL;
                            }
                        }
                    }
                    if (SUCCEEDED(IWICImagingFactory_CreateFormatConverter( wic, &conv )) && conv)
                    {
                        if (SUCCEEDED(IWICFormatConverter_Initialize( conv, src_bmp,
                                                                      &GUID_WICPixelFormat32bppBGRA_local,
                                                                      WICBitmapDitherTypeNone, NULL, 0.0,
                                                                      WICBitmapPaletteTypeCustom )) &&
                            SUCCEEDED(IWICFormatConverter_GetSize( conv, &w, &h )) &&
                            w > 0 && h > 0 && w <= 4096 && h <= 4096)
                        {
                            BYTE *buf = malloc( (size_t)w * h * 4 );
                            if (buf)
                            {
                                if (SUCCEEDED(IWICFormatConverter_CopyPixels( conv, NULL, w * 4, w * h * 4, buf )))
                                {
                                    size_t npix = (size_t)w * h, px;
                                    BYTE pbr = GetRValue( g_paint_page_bg );
                                    BYTE pbg = GetGValue( g_paint_page_bg );
                                    BYTE pbb = GetBValue( g_paint_page_bg );
                                    for (px = 0; px < npix; px++)
                                    {
                                        BYTE b = buf[px * 4 + 0];
                                        BYTE g = buf[px * 4 + 1];
                                        BYTE r = buf[px * 4 + 2];
                                        BYTE a = buf[px * 4 + 3];
                                        buf[px * 4 + 0] = (BYTE)((b * a + pbb * (255 - a)) / 255);
                                        buf[px * 4 + 1] = (BYTE)((g * a + pbg * (255 - a)) / 255);
                                        buf[px * 4 + 2] = (BYTE)((r * a + pbr * (255 - a)) / 255);
                                        buf[px * 4 + 3] = 255;
                                    }
                                    *out_pixels = buf;
                                    *out_w = w;
                                    *out_h = h;
                                    ok = TRUE;
                                }
                                else free( buf );
                            }
                        }
                        IWICFormatConverter_Release( conv );
                    }
                    if (scaler) IWICBitmapScaler_Release( scaler );
                    IWICBitmapFrameDecode_Release( frame );
                }
                IWICBitmapDecoder_Release( dec );
            }
            IWICStream_Release( stream );
        }
        IWICImagingFactory_Release( wic );
    }
    return ok;
}

static void uwp_on_image_loaded( struct uwp_element *el )
{
    struct uwp_element *anc;
    int hop;
    if (!el) return;
    for (anc = el, hop = 0; anc && hop < 4; anc = anc->parent, hop++)
    {
        if (anc->opacity <= 0.005)
            anc->opacity = 1.0;
    }
}

static DWORD WINAPI uwp_http_image_worker( LPVOID param )
{
    struct uwp_element *el = (struct uwp_element *)param;
    HMODULE hwh;
    WCHAR url[512];
    typedef void *HINTERNET_t;
    HINTERNET_t (WINAPI *pOpen)( LPCWSTR, DWORD, LPCWSTR, LPCWSTR, DWORD );
    HINTERNET_t (WINAPI *pConnect)( HINTERNET_t, LPCWSTR, WORD, DWORD );
    HINTERNET_t (WINAPI *pOpenReq)( HINTERNET_t, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR *, DWORD );
    BOOL (WINAPI *pSendReq)( HINTERNET_t, LPCWSTR, DWORD, LPVOID, DWORD, DWORD, DWORD_PTR );
    BOOL (WINAPI *pRecvResp)( HINTERNET_t, LPVOID );
    BOOL (WINAPI *pReadData)( HINTERNET_t, LPVOID, DWORD, LPDWORD );
    BOOL (WINAPI *pClose)( HINTERNET_t );
    BOOL (WINAPI *pCrackUrl)( LPCWSTR, DWORD, DWORD, void * );
    struct
    {
        DWORD dwStructSize;
        LPWSTR lpszScheme;
        DWORD dwSchemeLength;
        int nScheme;
        LPWSTR lpszHostName;
        DWORD dwHostNameLength;
        WORD nPort;
        LPWSTR lpszUserName;
        DWORD dwUserNameLength;
        LPWSTR lpszPassword;
        DWORD dwPasswordLength;
        LPWSTR lpszUrlPath;
        DWORD dwUrlPathLength;
        LPWSTR lpszExtraInfo;
        DWORD dwExtraInfoLength;
    } uc = {0};
    WCHAR host[256] = {0}, path[512] = {0};
    BYTE *buf = NULL;
    DWORD cap = 0, len = 0;

    if (!el) return 0;
    lstrcpynW( url, el->image_src, ARRAY_SIZE(url) );
    CoInitializeEx( NULL, COINIT_MULTITHREADED );

    hwh = LoadLibraryW( L"winhttp.dll" );
    if (hwh)
    {
        pOpen = (void *)GetProcAddress( hwh, "WinHttpOpen" );
        pConnect = (void *)GetProcAddress( hwh, "WinHttpConnect" );
        pOpenReq = (void *)GetProcAddress( hwh, "WinHttpOpenRequest" );
        pSendReq = (void *)GetProcAddress( hwh, "WinHttpSendRequest" );
        pRecvResp = (void *)GetProcAddress( hwh, "WinHttpReceiveResponse" );
        pReadData = (void *)GetProcAddress( hwh, "WinHttpReadData" );
        pClose = (void *)GetProcAddress( hwh, "WinHttpCloseHandle" );
        pCrackUrl = (void *)GetProcAddress( hwh, "WinHttpCrackUrl" );
        if (pOpen && pConnect && pOpenReq && pSendReq && pRecvResp && pReadData && pClose && pCrackUrl)
        {
            uc.dwStructSize = sizeof(uc);
            uc.lpszHostName = host; uc.dwHostNameLength = ARRAY_SIZE(host);
            uc.lpszUrlPath = path; uc.dwUrlPathLength = ARRAY_SIZE(path);
            if (pCrackUrl( url, 0, 0, &uc ) && host[0])
            {
                HINTERNET_t ses = pOpen( L"WineStoreXAML/1.0", 0, NULL, NULL, 0 );
                if (ses)
                {
                    HINTERNET_t con = pConnect( ses, host, uc.nPort ? uc.nPort : (uc.nScheme == 2 ? 443 : 80), 0 );
                    if (con)
                    {
                        DWORD flags = (uc.nScheme == 2) ? 0x00800000 : 0; /* WINHTTP_FLAG_SECURE */
                        HINTERNET_t req = pOpenReq( con, L"GET", path[0] ? path : L"/", NULL, NULL, NULL, flags );
                        if (req)
                        {
                            if (pSendReq( req, NULL, 0, NULL, 0, 0, 0 ) && pRecvResp( req, NULL ))
                            {
                                BYTE chunk[16384];
                                DWORD got = 0;
                                while (pReadData( req, chunk, sizeof(chunk), &got ) && got > 0)
                                {
                                    if (len + got > 8 * 1024 * 1024) break;
                                    if (len + got > cap)
                                    {
                                        DWORD ncap = cap ? cap * 2 : 65536;
                                        BYTE *nb;
                                        while (ncap < len + got) ncap *= 2;
                                        nb = realloc( buf, ncap );
                                        if (!nb) break;
                                        buf = nb; cap = ncap;
                                    }
                                    memcpy( buf + len, chunk, got );
                                    len += got;
                                }
                            }
                            pClose( req );
                        }
                        pClose( con );
                    }
                    pClose( ses );
                }
            }
        }
    }

    if (buf && len > 0)
    {
        BYTE *px = NULL;
        UINT pw = 0, ph = 0;
        UINT max_dim = 384;
        if (wcsstr( el->class_name, L"LoadedImageSurface" ) || wcsstr( el->name, L"Hero" ) ||
            (el->has_bounds && ((el->bounds.right - el->bounds.left) > 320 || (el->bounds.bottom - el->bounds.top) > 320)))
            max_dim = 1024;
        if (uwp_wic_decode_memory( buf, len, max_dim, &px, &pw, &ph ) && px)
        {
            AcquireSRWLockExclusive( &g_http_img_cache_lock );
            if (g_http_img_cache_cnt < (int)ARRAY_SIZE(g_http_img_cache))
            {
                int idx = g_http_img_cache_cnt++;
                lstrcpynW( g_http_img_cache[idx].url, url, ARRAY_SIZE(g_http_img_cache[idx].url) );
                g_http_img_cache[idx].pixels = px;
                g_http_img_cache[idx].w = pw;
                g_http_img_cache[idx].h = ph;
                g_http_img_cache[idx].done = TRUE;
            }
            ReleaseSRWLockExclusive( &g_http_img_cache_lock );
            if (!wcscmp( el->image_src, url ))
            {
                el->img_pixels = px;
                el->img_w = pw;
                el->img_h = ph;
                el->img_loaded = TRUE;
                uwp_on_image_loaded( el );
                uwp_invalidate_window();
            }
        }
    }
    free( buf );
    el->img_loaded = TRUE;
    CoUninitialize();
    InterlockedDecrement( &g_active_http_workers );
    uwp_invalidate_window();
    return 0;
}

void uwp_load_image_asset( struct uwp_element *el )
{
    WCHAR rel[MAX_PATH] = {0}, base_dir[MAX_PATH] = {0}, candidate[MAX_PATH] = {0};
    const WCHAR *p = el->image_src;
    static const WCHAR *const scales_small[] = {
        L"", L".targetsize-24_altform-unplated", L".targetsize-24",
        L".targetsize-32_altform-unplated", L".targetsize-32",
        L".scale-100", L".scale-125", L".scale-150", L".scale-200", L".scale-400"
    };
    static const WCHAR *const scales_default[] = {
        L"", L".scale-100", L".scale-125", L".scale-150", L".scale-200", L".scale-400",
        L".targetsize-24_altform-unplated", L".targetsize-24",
        L".targetsize-32_altform-unplated", L".targetsize-32"
    };
    BOOL prefer_small;
    const WCHAR *const *scales;
    size_t scale_cnt;
    WCHAR *slash, *dot;
    size_t i;
    IWICImagingFactory *wic = NULL;
    IWICBitmapDecoder *dec = NULL;
    IWICBitmapFrameDecode *frame = NULL;
    IWICFormatConverter *conv = NULL;
    UINT w = 0, h = 0;

    if (!el || el->img_loaded || !el->image_src[0]) return;
    prefer_small = (el->width > 0.0 && el->width <= 32.0) ||
                   (el->has_bounds && (el->bounds.right - el->bounds.left) > 0 && (el->bounds.right - el->bounds.left) <= 32);
    scales = prefer_small ? scales_small : scales_default;
    scale_cnt = prefer_small ? ARRAY_SIZE(scales_small) : ARRAY_SIZE(scales_default);
    if (!wcsncmp( p, L"http://", 7 ) || !wcsncmp( p, L"https://", 8 ))
    {
        int ci;
        AcquireSRWLockShared( &g_http_img_cache_lock );
        for (ci = 0; ci < g_http_img_cache_cnt; ci++)
        {
            if (g_http_img_cache[ci].done && !wcscmp( g_http_img_cache[ci].url, p ))
            {
                el->img_pixels = g_http_img_cache[ci].pixels;
                el->img_w = g_http_img_cache[ci].w;
                el->img_h = g_http_img_cache[ci].h;
                el->img_loaded = TRUE;
                ReleaseSRWLockShared( &g_http_img_cache_lock );
                uwp_on_image_loaded( el );
                return;
            }
        }
        ReleaseSRWLockShared( &g_http_img_cache_lock );
        if (el->has_bounds && (el->bounds.bottom <= 0 || el->bounds.top >= 1150))
            return;
        if (InterlockedCompareExchange( &g_active_http_workers, 0, 0 ) >= 10)
            return;
        if (InterlockedCompareExchange( &el->img_loading, 1, 0 ) == 0)
        {
            HANDLE th;
            InterlockedIncrement( &g_active_http_workers );
            th = CreateThread( NULL, 0, uwp_http_image_worker, el, 0, NULL );
            if (th) CloseHandle( th );
            else
            {
                InterlockedDecrement( &g_active_http_workers );
                el->img_loaded = TRUE;
            }
        }
        return;
    }
    el->img_loaded = TRUE;

    if (!wcsncmp( p, L"ms-appx:///", 11 )) p += 11;
    else if (!wcsncmp( p, L"ms-appx://", 10 )) p += 10;
    while (*p == '/' || *p == '\\') p++;
    lstrcpynW( rel, p, ARRAY_SIZE(rel) );
    for (i = 0; rel[i]; i++) if (rel[i] == '/') rel[i] = '\\';

    GetModuleFileNameW( NULL, base_dir, ARRAY_SIZE(base_dir) );
    slash = wcsrchr( base_dir, '\\' );
    if (slash) *(slash + 1) = 0;

    dot = wcsrchr( rel, '.' );
    for (i = 0; i < scale_cnt; i++)
    {
        if (i > 0 && !dot) break;
        if (i == 0)
            swprintf( candidate, ARRAY_SIZE(candidate), L"%s%s", base_dir, rel );
        else
        {
            WCHAR stem[MAX_PATH] = {0};
            size_t slen = min( (size_t)(dot - rel), ARRAY_SIZE(stem) - 1 );
            memcpy( stem, rel, slen * sizeof(WCHAR) );
            stem[slen] = 0;
            swprintf( candidate, ARRAY_SIZE(candidate), L"%s%s%s%s", base_dir, stem, scales[i], dot );
        }
        if (GetFileAttributesW( candidate ) != INVALID_FILE_ATTRIBUTES) break;
        candidate[0] = 0;
    }
    if (!candidate[0]) return;

    if (SUCCEEDED(CoCreateInstance( &CLSID_WICImagingFactory_local, NULL, CLSCTX_INPROC_SERVER,
                                    &IID_IWICImagingFactory_local, (void **)&wic )) && wic)
    {
        if (SUCCEEDED(IWICImagingFactory_CreateDecoderFromFilename( wic, candidate, NULL, GENERIC_READ,
                                                                    WICDecodeMetadataCacheOnLoad, &dec )) && dec)
        {
            if (SUCCEEDED(IWICBitmapDecoder_GetFrame( dec, 0, &frame )) && frame)
            {
                if (SUCCEEDED(IWICImagingFactory_CreateFormatConverter( wic, &conv )) && conv)
                {
                    if (SUCCEEDED(IWICFormatConverter_Initialize( conv, (IWICBitmapSource *)frame,
                                                                  &GUID_WICPixelFormat32bppBGRA_local,
                                                                  WICBitmapDitherTypeNone, NULL, 0.0,
                                                                  WICBitmapPaletteTypeCustom )) &&
                        SUCCEEDED(IWICFormatConverter_GetSize( conv, &w, &h )) &&
                        w > 0 && h > 0 && w <= 4096 && h <= 4096)
                    {
                        BYTE *buf = malloc( (size_t)w * h * 4 );
                        if (buf)
                        {
                            if (SUCCEEDED(IWICFormatConverter_CopyPixels( conv, NULL, w * 4, w * h * 4, buf )))
                            {
                                el->img_pixels = buf;
                                el->img_w = w;
                                el->img_h = h;
                                uwp_on_image_loaded( el );
                                TRACE( "uwp_load_image_asset: loaded %s (%ux%u)\n", debugstr_w(candidate), w, h );
                            }
                            else free( buf );
                        }
                    }
                    IWICFormatConverter_Release( conv );
                }
                IWICBitmapFrameDecode_Release( frame );
            }
            IWICBitmapDecoder_Release( dec );
        }
        IWICImagingFactory_Release( wic );
    }
}

COLORREF uwp_paint_resolve_color( struct uwp_element *el, const WCHAR *key,
                                  COLORREF fallback, COLORREF surface_bg )
{
    UINT32 argb = 0;
    if (key && uwp_resolve_brush_or_color( NULL, el, key, 0, &argb ))
        return blend_argb_over( argb, surface_bg );
    return fallback;
}
