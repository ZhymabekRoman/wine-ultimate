/* WinRT Windows.UI.Xaml FontFamily / FontIcon & Bundled Font Cache
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
#define WIDL_using_Windows_ApplicationModel
#define WIDL_using_Windows_Management_Deployment
#include "windows.management.deployment.h"
#define WIDL_using_Windows_UI_Composition
#include "windows.ui.composition.h"
#include "uwp_private.h"

WINE_DEFAULT_DEBUG_CHANNEL(xaml);

static SRWLOCK g_bundled_fonts_lock = SRWLOCK_INIT;
static struct
{
    WCHAR path[MAX_PATH];
    WCHAR face[64];
} g_bundled_fonts[64];
static int g_bundled_font_cnt = 0;

static void uwp_register_bundled_font_file( const WCHAR *rel_or_uri, const WCHAR *face )
{
    WCHAR rel[MAX_PATH] = {0}, full[MAX_PATH] = {0}, base_dir[MAX_PATH] = {0};
    const WCHAR *p = rel_or_uri;
    WCHAR *slash;
    size_t i;
    int k;

    if (!p || !p[0]) return;
    while (*p == L' ' || *p == L'\t') p++;
    if (!wcsnicmp( p, L"ms-appx:///", 11 )) p += 11;
    else if (!wcsnicmp( p, L"ms-appx://", 10 )) p += 10;
    while (*p == L'/' || *p == L'\\') p++;
    if (!*p) return;

    lstrcpynW( rel, p, ARRAY_SIZE(rel) );
    for (i = 0; rel[i]; i++) if (rel[i] == L'/') rel[i] = L'\\';
    i = wcslen( rel );
    while (i > 0 && (rel[i - 1] == L' ' || rel[i - 1] == L'\t')) rel[--i] = 0;
    if (!rel[0]) return;

    if (GetModuleFileNameW( NULL, base_dir, ARRAY_SIZE(base_dir) ) && (slash = wcsrchr( base_dir, L'\\' )))
    {
        *(slash + 1) = 0;
        swprintf( full, ARRAY_SIZE(full), L"%s%s", base_dir, rel );
    }
    else
    {
        lstrcpynW( full, rel, ARRAY_SIZE(full) );
    }

    AcquireSRWLockShared( &g_bundled_fonts_lock );
    for (k = 0; k < g_bundled_font_cnt; k++)
    {
        if (!wcsicmp( g_bundled_fonts[k].path, full ))
        {
            ReleaseSRWLockShared( &g_bundled_fonts_lock );
            return;
        }
    }
    ReleaseSRWLockShared( &g_bundled_fonts_lock );

    if (GetFileAttributesW( full ) == INVALID_FILE_ATTRIBUTES)
    {
        WARN( "uwp_register_bundled_font_file: font file not found: %s\n", debugstr_w(full) );
        return;
    }

    AcquireSRWLockExclusive( &g_bundled_fonts_lock );
    for (k = 0; k < g_bundled_font_cnt; k++)
    {
        if (!wcsicmp( g_bundled_fonts[k].path, full ))
        {
            ReleaseSRWLockExclusive( &g_bundled_fonts_lock );
            return;
        }
    }
    if (g_bundled_font_cnt < (int)ARRAY_SIZE(g_bundled_fonts))
    {
        int added = AddFontResourceExW( full, FR_PRIVATE, 0 );
        TRACE( "uwp_register_bundled_font_file: AddFontResourceExW(%s) face=%s -> %d\n",
               debugstr_w(full), debugstr_w(face ? face : L""), added );
        lstrcpynW( g_bundled_fonts[g_bundled_font_cnt].path, full, ARRAY_SIZE(g_bundled_fonts[0].path) );
        if (face && face[0])
            lstrcpynW( g_bundled_fonts[g_bundled_font_cnt].face, face, ARRAY_SIZE(g_bundled_fonts[0].face) );
        g_bundled_font_cnt++;
    }
    ReleaseSRWLockExclusive( &g_bundled_fonts_lock );
}

BOOL uwp_is_bundled_font_face( const WCHAR *face )
{
    int k;
    BOOL found = FALSE;
    if (!face || !face[0]) return FALSE;
    AcquireSRWLockShared( &g_bundled_fonts_lock );
    for (k = 0; k < g_bundled_font_cnt; k++)
    {
        if (g_bundled_fonts[k].face[0] && !wcsicmp( g_bundled_fonts[k].face, face ))
        {
            found = TRUE;
            break;
        }
    }
    ReleaseSRWLockShared( &g_bundled_fonts_lock );
    return found;
}

void uwp_resolve_font_family( const WCHAR *raw_spec, WCHAR *out_face, size_t cap )
{
    WCHAR seg[256] = {0};
    const WCHAR *p, *comma, *hash;
    size_t len;

    if (!out_face || !cap) return;
    lstrcpynW( out_face, L"Segoe UI", cap );
    if (!raw_spec || !raw_spec[0]) return;

    p = raw_spec;
    while (*p == L' ' || *p == L'\t') p++;
    if (!*p || !wcsicmp( p, L"XamlAutoFontFamily" )) return;

    comma = wcschr( p, L',' );
    len = comma ? (size_t)(comma - p) : wcslen( p );
    if (len >= ARRAY_SIZE(seg)) len = ARRAY_SIZE(seg) - 1;
    memcpy( seg, p, len * sizeof(WCHAR) );
    seg[len] = 0;
    while (len > 0 && (seg[len - 1] == L' ' || seg[len - 1] == L'\t')) seg[--len] = 0;
    if (!seg[0] || !wcsicmp( seg, L"XamlAutoFontFamily" )) return;

    hash = wcschr( seg, L'#' );
    if (hash)
    {
        WCHAR font_path[256] = {0};
        const WCHAR *fam = hash + 1;
        size_t plen = (size_t)(hash - seg);
        while (*fam == L' ' || *fam == L'\t') fam++;
        if (plen >= ARRAY_SIZE(font_path)) plen = ARRAY_SIZE(font_path) - 1;
        memcpy( font_path, seg, plen * sizeof(WCHAR) );
        font_path[plen] = 0;
        if (fam[0]) lstrcpynW( out_face, fam, cap );
        if (font_path[0]) uwp_register_bundled_font_file( font_path, fam );
        return;
    }

    lstrcpynW( out_face, seg, cap );
}

const WCHAR *uwp_resolve_elem_font_family( const struct uwp_element *el, WCHAR *buf, size_t cap )
{
    const struct uwp_element *cur;
    int hops = 0;

    if (!buf || !cap) return L"Segoe UI";
    lstrcpynW( buf, L"Segoe UI", cap );
    for (cur = el; cur && hops < 6; cur = cur->parent, hops++)
    {
        const WCHAR *cur_ff = uwp_elem_font_family( cur );
        if (cur_ff[0] && wcsicmp( cur_ff, L"XamlAutoFontFamily" ))
        {
            if (!wcsicmp( cur_ff, L"SymbolThemeFontFamily" ))
                uwp_resolve_font_family( L"Segoe MDL2 Assets", buf, cap );
            else
                uwp_resolve_font_family( cur_ff, buf, cap );
            return buf;
        }
        if (cur->kind == UWP_KIND_PAGE || cur->kind == UWP_KIND_FRAME) break;
    }
    if (el && ((el->class_flags & (UWP_CF_FONT_ICON | UWP_CF_SYMBOL_ICON)) ||
               (uwp_has_text( el ) && el->text[0] >= 0xE700 && el->text[0] <= 0xF8FF && el->text[1] == 0 &&
                xaml_gpu_font_has_glyphs( L"Segoe MDL2 Assets", uwp_elem_text( el ) ))))
    {
        uwp_resolve_font_family( L"Segoe MDL2 Assets", buf, cap );
    }
    return buf;
}

static HRESULT WINAPI uwp_ff_get_Source( void *iface, HSTRING *out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    const WCHAR *ff = uwp_elem_font_family( impl );
    const WCHAR *src = ff[0] ? ff : L"Segoe UI";
    if (!out) return E_POINTER;
    return WindowsCreateString( src, wcslen(src), out );
}

const void *uwp_font_family_vtbl[16] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = uwp_ff_get_Source,
    [7 ... 15] = uwp_generic_noop,
};

static HRESULT WINAPI uwp_ff_factory_CreateInstanceWithName( void *iface, HSTRING family_name,
                                                             IInspectable *outer, IInspectable **inner, void **instance )
{
    HRESULT hr;
    if (!instance) return E_POINTER;
    *instance = NULL;
    hr = uwp_element_create( L"Windows.UI.Xaml.Media.FontFamily", outer, inner, instance );
    if (SUCCEEDED(hr) && *instance && family_name)
    {
        struct uwp_element *ff_el = uwp_elem_from_any( *instance );
        const WCHAR *raw = WindowsGetStringRawBuffer( family_name, NULL );
        if (ff_el && raw && raw[0])
        {
            WCHAR face[64] = {0};
            uwp_elem_set_font_family( ff_el, raw );
            uwp_resolve_font_family( raw, face, ARRAY_SIZE(face) );
            TRACE( "FontFamily::CreateInstanceWithName(%s) -> face=%s\n", debugstr_w(raw), debugstr_w(face) );
        }
    }
    return hr;
}

const void *uwp_font_family_factory_vtbl[16] =
{
    [0] = uwp_cf_QI,
    [1] = uwp_cf_AddRef,
    [2] = uwp_cf_Release,
    [3] = uwp_cf_GetIids,
    [4] = uwp_cf_GetRuntimeClassName,
    [5] = uwp_cf_GetTrustLevel,
    [6] = uwp_ff_factory_CreateInstanceWithName,
    [7 ... 15] = uwp_generic_noop,
};

static HRESULT WINAPI uwp_fi_get_FontWeight( void *iface, WORD *out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (out) *out = (impl && impl->font_weight > 0) ? (WORD)impl->font_weight : 400;
    return S_OK;
}

static HRESULT WINAPI uwp_fi_put_FontWeight( void *iface, WORD val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (impl) impl->font_weight = val;
    return S_OK;
}

const void *uwp_font_icon_vtbl[24] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = uwp_tb_get_Text,           /* get_Glyph */
    [7] = uwp_tb_put_Text,           /* put_Glyph */
    [8] = uwp_ctrl_get_FontSize,     /* get_FontSize */
    [9] = uwp_ctrl_put_FontSize,     /* put_FontSize */
    [10] = uwp_ctrl_get_FontFamily,  /* get_FontFamily */
    [11] = uwp_ctrl_put_FontFamily,  /* put_FontFamily */
    [12] = uwp_fi_get_FontWeight,    /* get_FontWeight */
    [13] = uwp_fi_put_FontWeight,    /* put_FontWeight */
    [14 ... 23] = uwp_generic_noop,
};
