/* WinRT Windows.UI.Xaml UWP activation factory dispatch (dynamic factory hash table)
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

#include <stdlib.h>
#include <string.h>
#include "uwp_private.h"

static UINT32 xaml_factory_hash_fnv( const WCHAR *name )
{
    UINT32 h = 2166136261u;   /* FNV-1a 32-bit offset basis */
    for (; *name; name++)
    {
        h ^= (UINT32)*name;
        h *= 16777619u;       /* FNV-1a prime */
    }
    return h;
}

static BOOL WINAPI init_xaml_factory_hash( INIT_ONCE *once, void *param, void **ctx )
{
    size_t i;
    for (i = 0; i < xaml_static_factories_count; i++)
    {
        const struct xaml_static_factory *e = &xaml_static_factories[i];
        UINT32 h = xaml_factory_hash_fnv( e->name ) & (XAML_FACTORY_HASH_SIZE - 1);
        while (xaml_factory_hash[h])   /* linear probe; first insert wins */
            h = (h + 1) & (XAML_FACTORY_HASH_SIZE - 1);
        xaml_factory_hash[h] = e;
    }
    return TRUE;
}

/* O(1) probe of the built index.  Returns the singleton factory for a known static
 * class name, NULL when the class is not in the table (the caller then falls through to
 * the dynamic per-class-name factory). */
static IActivationFactory *xaml_lookup_static_factory( const WCHAR *name )
{
    const struct xaml_static_factory *e;
    UINT32 h;

    InitOnceExecuteOnce( &xaml_factory_hash_once, init_xaml_factory_hash, NULL, NULL );
    h = xaml_factory_hash_fnv( name ) & (XAML_FACTORY_HASH_SIZE - 1);
    while ((e = xaml_factory_hash[h]))
    {
        if (!wcscmp( e->name, name )) return e->factory;
        h = (h + 1) & (XAML_FACTORY_HASH_SIZE - 1);
    }
    return NULL;
}

HRESULT uwp_get_activation_factory( HSTRING classid, IActivationFactory **factory )
{
    const WCHAR *buf = WindowsGetStringRawBuffer( classid, NULL );
    int i;

    if (!buf || !factory) return E_NOINTERFACE;

    /* Fixed singleton factories resolved through the FNV-1a index (one hash probe) in
     * place of the old linear wcscmp chain.  A miss falls through to the dynamic
     * per-class-name factory below. */
    {
        IActivationFactory *sf = xaml_lookup_static_factory( buf );
        if (sf)
        {
            *factory = sf;
            return S_OK;
        }
    }

    /* For UWP apps, serve all Windows.UI.Xaml.* and Microsoft.UI.Xaml.* runtime classes with our UWP multi-iface element factory;
     * also serve core Windows.UI.Xaml.Controls panels/buttons so UWP controls use the layout/pointer engine. */
    if ((!xaml_get_app_caps()->winui3 ||
         (!wcscmp( buf, L"Windows.UI.Xaml.Controls.Grid" ) ||
           !wcscmp( buf, L"Windows.UI.Xaml.Controls.StackPanel" ) ||
           !wcscmp( buf, L"Windows.UI.Xaml.Controls.Panel" ) ||
           !wcscmp( buf, L"Windows.UI.Xaml.Controls.Border" ) ||
           !wcscmp( buf, L"Windows.UI.Xaml.Controls.Canvas" ) ||
           !wcscmp( buf, L"Windows.UI.Xaml.Controls.Button" ) ||
           !wcscmp( buf, L"Windows.UI.Xaml.Controls.TextBlock" ) ||
           !wcscmp( buf, L"Microsoft.UI.Xaml.Controls.Primitives.Popup" ))) &&
        (!wcsncmp( buf, L"Windows.UI.Xaml.", 16 ) || !wcsncmp( buf, L"Microsoft.UI.Xaml.", 18 )) &&
        wcscmp( buf, L"Windows.UI.Xaml.Application" ) != 0 &&
        wcscmp( buf, L"Microsoft.UI.Xaml.Application" ) != 0 &&
        wcscmp( buf, L"Windows.UI.Xaml.Window" ) != 0 &&
        wcscmp( buf, L"Microsoft.UI.Xaml.Window" ) != 0 &&
        wcscmp( buf, L"Windows.UI.Xaml.PropertyMetadata" ) != 0 &&
        wcscmp( buf, L"Windows.UI.Xaml.DependencyProperty" ) != 0 &&
        wcscmp( buf, L"Windows.UI.Xaml.ResourceDictionary" ) != 0 &&
        wcscmp( buf, L"Windows.UI.Xaml.SizeHelper" ) != 0 &&
        wcscmp( buf, L"Windows.UI.Xaml.Data.PropertyChangedEventArgs" ) != 0 &&
        wcscmp( buf, L"Windows.UI.Xaml.Data.CollectionViewSource" ) != 0 &&
        wcscmp( buf, L"Windows.UI.Xaml.PropertyPath" ) != 0 &&
        wcscmp( buf, L"Microsoft.UI.Xaml.PropertyPath" ) != 0 &&
        wcscmp( buf, L"Microsoft.UI.Xaml.XamlTypeInfo.XamlControlsXamlMetaDataProvider" ) != 0)
    {
        static int *g_dyn_hash_next = NULL;
        static int g_dyn_hash_head[512];
        static BOOL g_dyn_hash_init = FALSE;
        UINT32 bucket = xaml_factory_hash_fnv( buf ) & 511;

        AcquireSRWLockShared( &g_dyn_factories_lock );
        if (g_dyn_hash_init)
        {
            for (i = g_dyn_hash_head[bucket]; i >= 0; i = g_dyn_hash_next[i])
            {
                if (!wcscmp( g_dyn_factory_names[i], buf ))
                {
                    *factory = &g_dyn_factories[i]->IActivationFactory_iface;
                    ReleaseSRWLockShared( &g_dyn_factories_lock );
                    return S_OK;
                }
            }
        }
        ReleaseSRWLockShared( &g_dyn_factories_lock );

        AcquireSRWLockExclusive( &g_dyn_factories_lock );
        if (!g_dyn_hash_init)
        {
            memset( g_dyn_hash_head, -1, sizeof(g_dyn_hash_head) );
            for (i = 0; i < g_dyn_factory_cnt; i++)
            {
                UINT32 b = xaml_factory_hash_fnv( g_dyn_factory_names[i] ) & 511;
                g_dyn_hash_next[i] = g_dyn_hash_head[b];
                g_dyn_hash_head[b] = i;
            }
            g_dyn_hash_init = TRUE;
        }
        for (i = g_dyn_hash_head[bucket]; i >= 0; i = g_dyn_hash_next[i])
        {
            if (!wcscmp( g_dyn_factory_names[i], buf ))
            {
                *factory = &g_dyn_factories[i]->IActivationFactory_iface;
                ReleaseSRWLockExclusive( &g_dyn_factories_lock );
                return S_OK;
            }
        }
        if (g_dyn_factory_cnt >= g_dyn_factory_cap)
        {
            int new_cap = g_dyn_factory_cap ? g_dyn_factory_cap * 2 : 64;
            struct uwp_class_factory **new_factories = realloc( g_dyn_factories, (size_t)new_cap * sizeof(*new_factories) );
            WCHAR **new_names = new_factories ? realloc( g_dyn_factory_names, (size_t)new_cap * sizeof(*new_names) ) : NULL;
            int *new_next = new_names ? realloc( g_dyn_hash_next, (size_t)new_cap * sizeof(*new_next) ) : NULL;
            if (new_factories) g_dyn_factories = new_factories;
            if (new_names) g_dyn_factory_names = new_names;
            if (new_next) g_dyn_hash_next = new_next;
            if (new_factories && new_names && new_next)
                g_dyn_factory_cap = new_cap;
        }
        if (g_dyn_factory_cnt < g_dyn_factory_cap)
        {
            struct uwp_class_factory *cf = calloc( 1, sizeof(*cf) );
            size_t len = wcslen( buf ) + 1;
            WCHAR *name_copy = cf ? malloc( len * sizeof(WCHAR) ) : NULL;
            if (cf && name_copy)
            {
                int idx = g_dyn_factory_cnt++;
                memcpy( name_copy, buf, len * sizeof(WCHAR) );
                cf->IActivationFactory_iface.lpVtbl = (const struct IActivationFactoryVtbl *)uwp_cf_act_vtbl;
                cf->IComposableFactory_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_cf_comp_vtbl;
                cf->IStatics_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_cf_statics_vtbl;
                cf->IDpStatics_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_dp_statics_vtbl;
                cf->IBitmapImageFactory_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_bitmap_image_factory_vtbl;
                cf->IFontFamilyFactory_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_font_family_factory_vtbl;
                cf->IButtonBaseStatics_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_buttonbase_statics_vtbl;
                cf->ISelectorStatics_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_selector_statics_vtbl;
                cf->IUIElementStatics_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_uielement_statics_vtbl;
                cf->IUIElementRoutedStatics_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_uielement_routed_statics_vtbl;
                cf->IGridStatics_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_grid_statics_vtbl;
                cf->IFocusManagerStatics_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_focus_manager_statics_vtbl;
                cf->IBindingOpsStatics_iface.lpVtbl = (const struct IInspectableVtbl *)binding_ops_statics_vtbl;
                cf->IThicknessHelperStatics_iface.lpVtbl = (const struct IInspectableVtbl *)thickness_helper_statics_vtbl;
                cf->IXamlMarkupHelperStatics_iface.lpVtbl = (const struct IInspectableVtbl *)xaml_markup_helper_statics_vtbl;
                cf->class_name = name_copy;
                cf->factory_flags = uwp_cf_compute_flags( name_copy );
                g_dyn_factories[idx] = cf;
                g_dyn_factory_names[idx] = name_copy;
                g_dyn_hash_next[idx] = g_dyn_hash_head[bucket];
                g_dyn_hash_head[bucket] = idx;
                *factory = &cf->IActivationFactory_iface;
                ReleaseSRWLockExclusive( &g_dyn_factories_lock );
                return S_OK;
            }
            free( name_copy );
            free( cf );
        }
        ReleaseSRWLockExclusive( &g_dyn_factories_lock );
    }

    return E_NOINTERFACE;
}





const GUID IID_IXamlType_WinUI3_g =
    { 0xd24219df, 0x7ec9, 0x57f1, { 0xa2, 0x7b, 0x6a, 0xf2, 0x51, 0xd9, 0xc5, 0xbc } };
const void *dp_changed_args_vtbl[] =
{
    dp_args_QI,
    dp_args_AddRef,
    dp_args_Release,
    dp_args_GetIids,
    dp_args_GetRuntimeClassName,
    dp_args_GetTrustLevel,
    dp_args_get_Property, /* 6: get_Property */
    dp_args_get_OldValue, /* 7: get_OldValue */
    dp_args_get_NewValue, /* 8: get_NewValue */
};
