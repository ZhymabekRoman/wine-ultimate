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
#include <excpt.h>
#include "wine/exception.h"
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
    const WCHAR *s = uwp_elem_text( el );
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

#define HTTP_IMG_CACHE_SLOTS 512
#define HTTP_IMG_HASH_SIZE   1024
#define HTTP_IMG_MAX_WAITERS 32

enum http_img_state
{
    HTTP_IMG_EMPTY = 0,
    HTTP_IMG_IN_FLIGHT,
    HTTP_IMG_DONE,
    HTTP_IMG_FAILED,
};

struct http_img_entry
{
    WCHAR url[512];
    BYTE *pixels;
    UINT w, h;
    UINT max_dim;
    enum http_img_state state;
    UINT64 lru_tick;
    struct uwp_element *waiters[HTTP_IMG_MAX_WAITERS];
    int waiter_cnt;
};

static struct http_img_entry g_http_img_cache[HTTP_IMG_CACHE_SLOTS];
static int g_http_img_hash[HTTP_IMG_HASH_SIZE]; /* slot index + 1, 0 = empty */
static int g_http_img_cache_cnt = 0;
static UINT64 g_http_img_lru_clock = 1;

#define LOCAL_IMG_CACHE_SLOTS 256
#define LOCAL_IMG_HASH_SIZE   512

struct local_img_entry
{
    WCHAR key[520]; /* "S:" or "D:" + image_src */
    BYTE *pixels;
    UINT w, h;
    BOOL valid;
    BOOL found;
    UINT64 lru_tick;
};

static SRWLOCK g_local_img_cache_lock = SRWLOCK_INIT;
static struct local_img_entry g_local_img_cache[LOCAL_IMG_CACHE_SLOTS];
static int g_local_img_hash[LOCAL_IMG_HASH_SIZE]; /* slot index + 1, 0 = empty */
static int g_local_img_cache_cnt = 0;
static UINT64 g_local_img_lru_clock = 1;

static inline UINT32 img_hash_wstr( const WCHAR *s )
{
    UINT32 h = 2166136261u;
    for (; *s; s++)
    {
        h ^= (UINT32)*s;
        h *= 16777619u;
    }
    return h;
}

static inline UINT32 img_hash_wstr_ci( const WCHAR *s )
{
    UINT32 h = 2166136261u;
    for (; *s; s++)
    {
        WCHAR c = *s;
        if (c >= L'A' && c <= L'Z') c += 32;
        h ^= (UINT32)c;
        h *= 16777619u;
    }
    return h;
}

static void http_img_rebuild_hash_locked( void )
{
    int i;
    memset( g_http_img_hash, 0, sizeof(g_http_img_hash) );
    for (i = 0; i < g_http_img_cache_cnt; i++)
    {
        UINT32 idx;
        if (g_http_img_cache[i].state == HTTP_IMG_EMPTY) continue;
        idx = img_hash_wstr( g_http_img_cache[i].url ) & (HTTP_IMG_HASH_SIZE - 1);
        while (g_http_img_hash[idx]) idx = (idx + 1) & (HTTP_IMG_HASH_SIZE - 1);
        g_http_img_hash[idx] = i + 1;
    }
}

static int http_img_find_locked( const WCHAR *url )
{
    UINT32 idx = img_hash_wstr( url ) & (HTTP_IMG_HASH_SIZE - 1);
    int v;
    while ((v = g_http_img_hash[idx]) != 0)
    {
        if (!wcscmp( g_http_img_cache[v - 1].url, url ))
            return v - 1;
        idx = (idx + 1) & (HTTP_IMG_HASH_SIZE - 1);
    }
    return -1;
}

static int http_img_alloc_slot_locked( const WCHAR *url )
{
    int slot = -1;
    if (g_http_img_cache_cnt < HTTP_IMG_CACHE_SLOTS)
    {
        UINT32 idx;
        slot = g_http_img_cache_cnt++;
        memset( &g_http_img_cache[slot], 0, sizeof(g_http_img_cache[slot]) );
        lstrcpynW( g_http_img_cache[slot].url, url, ARRAY_SIZE(g_http_img_cache[slot].url) );
        idx = img_hash_wstr( g_http_img_cache[slot].url ) & (HTTP_IMG_HASH_SIZE - 1);
        while (g_http_img_hash[idx]) idx = (idx + 1) & (HTTP_IMG_HASH_SIZE - 1);
        g_http_img_hash[idx] = slot + 1;
        return slot;
    }
    /* Evict least-recently-used completed slot (never evict IN_FLIGHT; leave pixels intact for live elements) */
    {
        int i;
        UINT64 best_tick = ~(UINT64)0;
        for (i = 0; i < HTTP_IMG_CACHE_SLOTS; i++)
        {
            if (g_http_img_cache[i].state != HTTP_IMG_IN_FLIGHT && g_http_img_cache[i].lru_tick < best_tick)
            {
                best_tick = g_http_img_cache[i].lru_tick;
                slot = i;
            }
        }
        if (slot < 0) return -1;
        memset( &g_http_img_cache[slot], 0, sizeof(g_http_img_cache[slot]) );
        lstrcpynW( g_http_img_cache[slot].url, url, ARRAY_SIZE(g_http_img_cache[slot].url) );
        http_img_rebuild_hash_locked();
        return slot;
    }
}

typedef void *HINTERNET_t;

static struct
{
    BOOL ready;
    HMODULE hwh;
    HINTERNET_t session;
    HINTERNET_t (WINAPI *pOpen)( LPCWSTR, DWORD, LPCWSTR, LPCWSTR, DWORD );
    HINTERNET_t (WINAPI *pConnect)( HINTERNET_t, LPCWSTR, WORD, DWORD );
    HINTERNET_t (WINAPI *pOpenReq)( HINTERNET_t, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR *, DWORD );
    BOOL (WINAPI *pSendReq)( HINTERNET_t, LPCWSTR, DWORD, LPVOID, DWORD, DWORD, DWORD_PTR );
    BOOL (WINAPI *pRecvResp)( HINTERNET_t, LPVOID );
    BOOL (WINAPI *pReadData)( HINTERNET_t, LPVOID, DWORD, LPDWORD );
    BOOL (WINAPI *pClose)( HINTERNET_t );
    BOOL (WINAPI *pCrackUrl)( LPCWSTR, DWORD, DWORD, void * );
} g_winhttp;

static INIT_ONCE g_winhttp_once = INIT_ONCE_STATIC_INIT;

static BOOL CALLBACK init_winhttp_session( INIT_ONCE *once, void *param, void **ctx )
{
    g_winhttp.hwh = LoadLibraryW( L"winhttp.dll" );
    if (!g_winhttp.hwh) return TRUE;
    g_winhttp.pOpen     = (void *)GetProcAddress( g_winhttp.hwh, "WinHttpOpen" );
    g_winhttp.pConnect  = (void *)GetProcAddress( g_winhttp.hwh, "WinHttpConnect" );
    g_winhttp.pOpenReq  = (void *)GetProcAddress( g_winhttp.hwh, "WinHttpOpenRequest" );
    g_winhttp.pSendReq  = (void *)GetProcAddress( g_winhttp.hwh, "WinHttpSendRequest" );
    g_winhttp.pRecvResp = (void *)GetProcAddress( g_winhttp.hwh, "WinHttpReceiveResponse" );
    g_winhttp.pReadData = (void *)GetProcAddress( g_winhttp.hwh, "WinHttpReadData" );
    g_winhttp.pClose    = (void *)GetProcAddress( g_winhttp.hwh, "WinHttpCloseHandle" );
    g_winhttp.pCrackUrl = (void *)GetProcAddress( g_winhttp.hwh, "WinHttpCrackUrl" );
    if (g_winhttp.pOpen && g_winhttp.pConnect && g_winhttp.pOpenReq && g_winhttp.pSendReq &&
        g_winhttp.pRecvResp && g_winhttp.pReadData && g_winhttp.pClose && g_winhttp.pCrackUrl)
    {
        g_winhttp.session = g_winhttp.pOpen( L"WineStoreXAML/1.0", 0, NULL, NULL, 0 );
        if (g_winhttp.session)
            g_winhttp.ready = TRUE;
    }
    return TRUE;
}

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

static void uwp_fire_img_opened_reg( struct uwp_element *target )
{
    void **handlers = NULL;
    unsigned int cnt, i;
    IInspectable *sender, *args;

    if (!UWP_EV_ANY( target, img_opened_reg )) return;
    cnt = xaml_event_registry_snapshot_alloc( &target->events->img_opened_reg, &handlers );
    xaml_event_registry_clear( &target->events->img_opened_reg );
    target->events->img_opened_reg.next_token = 0;
    if (!cnt) return;
    sender = target->outer ? target->outer : &target->IInspectable_iface;
    args = (IInspectable *)g_routed_args();
    for (i = 0; i < cnt; i++)
    {
        struct xaml_event_handler *h = (struct xaml_event_handler *)handlers[i];
        if (h)
        {
            __TRY { h->lpVtbl->Invoke( h, sender, args ); }
            __EXCEPT_ALL { WARN( "%s: exception 0x%08lx\n", __func__, (unsigned long)GetExceptionCode() ); }
            __ENDTRY;
            IUnknown_Release( (IUnknown *)h );
        }
    }
    if (args) IInspectable_Release( args );
    free( handlers );
}

void uwp_on_image_loaded( struct uwp_element *el )
{
    struct uwp_element *anc;
    int hop;
    if (!el) return;
    uwp_fire_img_opened_reg( el );
    if (el->media && el->media->img_source)
    {
        struct uwp_element *src_el = uwp_elem_from_any( el->media->img_source );
        if (src_el && src_el != el)
        {
            uwp_fire_img_opened_reg( src_el );
            if (src_el->media && src_el->media->img_source)
            {
                struct uwp_element *src2_el = uwp_elem_from_any( src_el->media->img_source );
                if (src2_el && src2_el != src_el && src2_el != el)
                    uwp_fire_img_opened_reg( src2_el );
            }
        }
    }
    if (el->bg_brush)
    {
        struct uwp_element *br_el = uwp_elem_from_any( el->bg_brush );
        if (br_el && br_el != el)
        {
            uwp_fire_img_opened_reg( br_el );
            if (br_el->media && br_el->media->img_source)
            {
                struct uwp_element *bi_el = uwp_elem_from_any( br_el->media->img_source );
                if (bi_el && bi_el != br_el && bi_el != el)
                    uwp_fire_img_opened_reg( bi_el );
            }
        }
    }
    for (anc = el, hop = 0; anc && hop < 4; anc = anc->parent, hop++)
    {
        if (anc->opacity <= 0.005)
            anc->opacity = 1.0;
    }
}

struct http_worker_ctx
{
    int cache_slot;
    UINT max_dim;
    WCHAR url[512];
};

static DWORD WINAPI uwp_http_image_worker( LPVOID param )
{
    struct http_worker_ctx *wctx = (struct http_worker_ctx *)param;
    WCHAR url[512];
    int cache_slot;
    UINT max_dim;
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
    BYTE *px = NULL;
    UINT pw = 0, ph = 0;
    struct uwp_element *waiters[HTTP_IMG_MAX_WAITERS];
    int waiter_cnt = 0, wi;
    BOOL need_layout = FALSE;

    if (!wctx) return 0;
    lstrcpynW( url, wctx->url, ARRAY_SIZE(url) );
    cache_slot = wctx->cache_slot;
    max_dim = wctx->max_dim;
    free( wctx );

    CoInitializeEx( NULL, COINIT_MULTITHREADED );
    InitOnceExecuteOnce( &g_winhttp_once, init_winhttp_session, NULL, NULL );

    if (g_winhttp.ready)
    {
        uc.dwStructSize = sizeof(uc);
        uc.lpszHostName = host; uc.dwHostNameLength = ARRAY_SIZE(host);
        uc.lpszUrlPath = path; uc.dwUrlPathLength = ARRAY_SIZE(path);
        if (g_winhttp.pCrackUrl( url, 0, 0, &uc ) && host[0])
        {
            HINTERNET_t con = g_winhttp.pConnect( g_winhttp.session, host,
                                                  uc.nPort ? uc.nPort : (uc.nScheme == 2 ? 443 : 80), 0 );
            if (con)
            {
                DWORD flags = (uc.nScheme == 2) ? 0x00800000 : 0; /* WINHTTP_FLAG_SECURE */
                HINTERNET_t req = g_winhttp.pOpenReq( con, L"GET", path[0] ? path : L"/", NULL, NULL, NULL, flags );
                if (req)
                {
                    if (g_winhttp.pSendReq( req, NULL, 0, NULL, 0, 0, 0 ) && g_winhttp.pRecvResp( req, NULL ))
                    {
                        BYTE chunk[16384];
                        DWORD got = 0;
                        while (g_winhttp.pReadData( req, chunk, sizeof(chunk), &got ) && got > 0)
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
                    g_winhttp.pClose( req );
                }
                g_winhttp.pClose( con );
            }
        }
    }

    if (buf && len > 0)
        uwp_wic_decode_memory( buf, len, max_dim, &px, &pw, &ph );
    free( buf );

    AcquireSRWLockExclusive( &g_http_img_cache_lock );
    if (cache_slot >= 0 && cache_slot < HTTP_IMG_CACHE_SLOTS && !wcscmp( g_http_img_cache[cache_slot].url, url ))
    {
        struct http_img_entry *e = &g_http_img_cache[cache_slot];
        e->pixels = px;
        e->w = pw;
        e->h = ph;
        e->state = px ? HTTP_IMG_DONE : HTTP_IMG_FAILED;
        e->lru_tick = ++g_http_img_lru_clock;
        waiter_cnt = min( e->waiter_cnt, HTTP_IMG_MAX_WAITERS );
        for (wi = 0; wi < waiter_cnt; wi++)
            waiters[wi] = e->waiters[wi];
        e->waiter_cnt = 0;
    }
    ReleaseSRWLockExclusive( &g_http_img_cache_lock );

    for (wi = 0; wi < waiter_cnt; wi++)
    {
        struct uwp_element *wel = waiters[wi];
        struct uwp_media_ext *wm;
        if (!wel || wel->magic != UWP_ELEM_MAGIC || !wel->media) continue;
        wm = wel->media;
        if (px && !wcscmp( wm->image_src, url ))
        {
            struct uwp_element *anc;
            int hop;
            wm->img_pixels = px;
            wm->img_w = pw;
            wm->img_h = ph;
            for (anc = wel, hop = 0; anc && hop < 4; anc = anc->parent, hop++)
            {
                if (anc->opacity <= 0.005)
                    anc->opacity = 1.0;
            }
            if (!wel->has_bounds || wel->bounds.right <= wel->bounds.left || wel->bounds.bottom <= wel->bounds.top)
                need_layout = TRUE;
        }
        wm->img_loaded = TRUE;
        InterlockedExchange( &wm->img_loading, 0 );
    }

    CoUninitialize();
    InterlockedDecrement( &g_active_http_workers );
    if (need_layout)
        uwp_invalidate_window();
    else
        uwp_invalidate_paint_only();
    return 0;
}

static int local_img_find_locked( const WCHAR *key )
{
    UINT32 idx = img_hash_wstr_ci( key ) & (LOCAL_IMG_HASH_SIZE - 1);
    int v;
    while ((v = g_local_img_hash[idx]) != 0)
    {
        if (!wcsicmp( g_local_img_cache[v - 1].key, key ))
            return v - 1;
        idx = (idx + 1) & (LOCAL_IMG_HASH_SIZE - 1);
    }
    return -1;
}

static void local_img_rebuild_hash_locked( void )
{
    int i;
    memset( g_local_img_hash, 0, sizeof(g_local_img_hash) );
    for (i = 0; i < g_local_img_cache_cnt; i++)
    {
        UINT32 idx;
        if (!g_local_img_cache[i].valid) continue;
        idx = img_hash_wstr_ci( g_local_img_cache[i].key ) & (LOCAL_IMG_HASH_SIZE - 1);
        while (g_local_img_hash[idx]) idx = (idx + 1) & (LOCAL_IMG_HASH_SIZE - 1);
        g_local_img_hash[idx] = i + 1;
    }
}

static void local_img_store( const WCHAR *key, BYTE *pixels, UINT w, UINT h, BOOL found )
{
    int slot;
    AcquireSRWLockExclusive( &g_local_img_cache_lock );
    slot = local_img_find_locked( key );
    if (slot < 0)
    {
        if (g_local_img_cache_cnt < LOCAL_IMG_CACHE_SLOTS)
        {
            UINT32 idx;
            slot = g_local_img_cache_cnt++;
            lstrcpynW( g_local_img_cache[slot].key, key, ARRAY_SIZE(g_local_img_cache[slot].key) );
            idx = img_hash_wstr_ci( g_local_img_cache[slot].key ) & (LOCAL_IMG_HASH_SIZE - 1);
            while (g_local_img_hash[idx]) idx = (idx + 1) & (LOCAL_IMG_HASH_SIZE - 1);
            g_local_img_hash[idx] = slot + 1;
        }
        else
        {
            int i;
            UINT64 best_tick = ~(UINT64)0;
            slot = 0;
            for (i = 0; i < LOCAL_IMG_CACHE_SLOTS; i++)
            {
                if (g_local_img_cache[i].lru_tick < best_tick)
                {
                    best_tick = g_local_img_cache[i].lru_tick;
                    slot = i;
                }
            }
            lstrcpynW( g_local_img_cache[slot].key, key, ARRAY_SIZE(g_local_img_cache[slot].key) );
            local_img_rebuild_hash_locked();
        }
    }
    g_local_img_cache[slot].pixels = pixels;
    g_local_img_cache[slot].w = w;
    g_local_img_cache[slot].h = h;
    g_local_img_cache[slot].found = found;
    g_local_img_cache[slot].valid = TRUE;
    g_local_img_cache[slot].lru_tick = ++g_local_img_lru_clock;
    ReleaseSRWLockExclusive( &g_local_img_cache_lock );
}

void uwp_load_image_asset( struct uwp_element *el )
{
    WCHAR rel[MAX_PATH] = {0}, base_dir[MAX_PATH] = {0}, candidate[MAX_PATH] = {0};
    WCHAR local_key[520];
    const WCHAR *p;
    struct uwp_media_ext *m;
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

    if (!el || !el->media) return;
    m = el->media;
    p = m->image_src;
    if (m->img_loaded || !p[0]) return;
    prefer_small = (el->width > 0.0 && el->width <= 32.0) ||
                   (el->has_bounds && (el->bounds.right - el->bounds.left) > 0 && (el->bounds.right - el->bounds.left) <= 32);
    scales = prefer_small ? scales_small : scales_default;
    scale_cnt = prefer_small ? ARRAY_SIZE(scales_small) : ARRAY_SIZE(scales_default);
    if (!wcsncmp( p, L"http://", 7 ) || !wcsncmp( p, L"https://", 8 ))
    {
        int ci;
        UINT max_dim = 384;
        if ((el->class_flags2 & UWP_CF2_LOADED_IMAGE_SURF) || wcsstr( el->name, L"Hero" ) ||
            (el->has_bounds && ((el->bounds.right - el->bounds.left) > 320 || (el->bounds.bottom - el->bounds.top) > 320)))
            max_dim = 1024;

        AcquireSRWLockExclusive( &g_http_img_cache_lock );
        ci = http_img_find_locked( p );
        if (ci >= 0)
        {
            struct http_img_entry *e = &g_http_img_cache[ci];
            e->lru_tick = ++g_http_img_lru_clock;
            if (e->state == HTTP_IMG_DONE)
            {
                m->img_pixels = e->pixels;
                m->img_w = e->w;
                m->img_h = e->h;
                m->img_loaded = TRUE;
                ReleaseSRWLockExclusive( &g_http_img_cache_lock );
                uwp_on_image_loaded( el );
                return;
            }
            if (e->state == HTTP_IMG_FAILED)
            {
                m->img_loaded = TRUE;
                ReleaseSRWLockExclusive( &g_http_img_cache_lock );
                return;
            }
            if (e->state == HTTP_IMG_IN_FLIGHT)
            {
                /* Deduplicate in-flight download: register el as waiter instead of spawning a new worker */
                int wi;
                BOOL present = FALSE;
                for (wi = 0; wi < e->waiter_cnt; wi++)
                    if (e->waiters[wi] == el) { present = TRUE; break; }
                if (!present && e->waiter_cnt < HTTP_IMG_MAX_WAITERS)
                    e->waiters[e->waiter_cnt++] = el;
                InterlockedExchange( &m->img_loading, 1 );
                ReleaseSRWLockExclusive( &g_http_img_cache_lock );
                return;
            }
        }
        if (el->has_bounds && (el->bounds.bottom <= 0 || el->bounds.top >= 1150))
        {
            ReleaseSRWLockExclusive( &g_http_img_cache_lock );
            return;
        }
        if (InterlockedCompareExchange( &g_active_http_workers, 0, 0 ) >= 10)
        {
            ReleaseSRWLockExclusive( &g_http_img_cache_lock );
            return;
        }
        if (InterlockedCompareExchange( &m->img_loading, 1, 0 ) == 0)
        {
            int slot = http_img_alloc_slot_locked( p );
            struct http_worker_ctx *wctx = (slot >= 0) ? malloc( sizeof(*wctx) ) : NULL;
            if (slot >= 0 && wctx)
            {
                HANDLE th;
                struct http_img_entry *e = &g_http_img_cache[slot];
                e->state = HTTP_IMG_IN_FLIGHT;
                e->max_dim = max_dim;
                e->lru_tick = ++g_http_img_lru_clock;
                e->waiters[0] = el;
                e->waiter_cnt = 1;
                wctx->cache_slot = slot;
                wctx->max_dim = max_dim;
                lstrcpynW( wctx->url, p, ARRAY_SIZE(wctx->url) );
                ReleaseSRWLockExclusive( &g_http_img_cache_lock );

                InterlockedIncrement( &g_active_http_workers );
                th = CreateThread( NULL, 0, uwp_http_image_worker, wctx, 0, NULL );
                if (th) CloseHandle( th );
                else
                {
                    InterlockedDecrement( &g_active_http_workers );
                    free( wctx );
                    AcquireSRWLockExclusive( &g_http_img_cache_lock );
                    if (!wcscmp( g_http_img_cache[slot].url, p ))
                    {
                        g_http_img_cache[slot].state = HTTP_IMG_FAILED;
                        g_http_img_cache[slot].waiter_cnt = 0;
                    }
                    ReleaseSRWLockExclusive( &g_http_img_cache_lock );
                    m->img_loaded = TRUE;
                    InterlockedExchange( &m->img_loading, 0 );
                }
                return;
            }
            free( wctx );
            InterlockedExchange( &m->img_loading, 0 );
        }
        ReleaseSRWLockExclusive( &g_http_img_cache_lock );
        return;
    }

    m->img_loaded = TRUE;

    /* Check local ms-appx:// decoded image cache */
    local_key[0] = prefer_small ? L'S' : L'D';
    local_key[1] = L':';
    lstrcpynW( local_key + 2, p, ARRAY_SIZE(local_key) - 2 );
    AcquireSRWLockShared( &g_local_img_cache_lock );
    {
        int lci = local_img_find_locked( local_key );
        if (lci >= 0 && g_local_img_cache[lci].valid)
        {
            struct local_img_entry *le = &g_local_img_cache[lci];
            le->lru_tick = ++g_local_img_lru_clock;
            if (le->found && le->pixels)
            {
                m->img_pixels = le->pixels;
                m->img_w = le->w;
                m->img_h = le->h;
                ReleaseSRWLockShared( &g_local_img_cache_lock );
                uwp_on_image_loaded( el );
                return;
            }
            ReleaseSRWLockShared( &g_local_img_cache_lock );
            return;
        }
    }
    ReleaseSRWLockShared( &g_local_img_cache_lock );

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
    if (!candidate[0])
    {
        local_img_store( local_key, NULL, 0, 0, FALSE );
        return;
    }

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
                                local_img_store( local_key, buf, w, h, TRUE );
                                m->img_pixels = buf;
                                m->img_w = w;
                                m->img_h = h;
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
