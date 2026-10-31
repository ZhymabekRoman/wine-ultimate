/*
 * WinRT Windows.ApplicationModel.Resources.Core (PRM/MRT) implementation
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

/*
 * Surface required by .NET Native MRT (Calculator: mrt100_app/SharedLibrary
 * ResourceManager.Current -> RoGetActivationFactory(ResourceContext) x8 ->
 * unhandled exception -> FailFast c0000602).
 *
 * Interface IIDs and vtable slot ordering are taken from the official
 * Windows.WinRT metadata (19041 generation).  IResourceContextStatics2
 * {41f752ef-12af-41b9-ab36-b1eb4b512460} was confirmed live: its GUID is
 * embedded in Calculator's failfast stack (qword 41b912af41f752ef).
 *
 * Values come from the app's own resources.pri via the minimal reader in
 * pri.c (name index + value blob scan; documented stub tier when the
 * qualifier-aware res_map2 index tables are not parsed - see pri.h).
 *
 * Windows.System.MemoryManager is co-hosted as a no-op activation stub
 * (Removes the one-shot "Failed to find library" warning on Calculator).
 */

#include <stdarg.h>
#define COBJMACROS
#include "windef.h"
#include "winbase.h"
#include "winstring.h"
#include "initguid.h"
#include "activation.h"
#include "wine/debug.h"

#include "pri.h"

WINE_DEFAULT_DEBUG_CHANNEL(resources);

/* IIDs from Windows.WinRT metadata (19041). Factory IIDs are the standard
 * {00000035-*}. Named/Factory interfaces predate 19041 metadata; marked
 * (unverified). */
static const GUID IID_IResourceContext_g          = {0x2fa22f4b, 0x707e, 0x4b27, {0xad, 0x0d, 0xd0, 0xd8, 0xcd, 0x46, 0x8f, 0xd2}};
static const GUID IID_IResourceContextStatics_g   = {0x98be9d6c, 0x6338, 0x4b31, {0x99, 0xdf, 0xb2, 0xb4, 0x42, 0xf1, 0x71, 0x49}};
static const GUID IID_IResourceContextStatics2_g  = {0x41f752ef, 0x12af, 0x41b9, {0xab, 0x36, 0xb1, 0xeb, 0x4b, 0x51, 0x24, 0x60}};
static const GUID IID_IResourceContextStatics3_g  = {0x20cf492c, 0xaf0f, 0x450b, {0x9d, 0xa6, 0x10, 0x6d, 0xd0, 0xc2, 0x9a, 0x39}};
static const GUID IID_IResourceContextStatics4_g  = {0x22eb9ccd, 0xfb31, 0x4bfa, {0xb8, 0x6b, 0xdf, 0x9d, 0x9d, 0x7b, 0xdc, 0x39}};
static const GUID IID_IResourceManager_g          = {0xf744d97b, 0x9988, 0x44fb, {0xab, 0xd6, 0x53, 0x78, 0x84, 0x4c, 0xfa, 0x8b}};
static const GUID IID_IResourceManager2_g         = {0x9d66fe6c, 0xa4d7, 0x4c23, {0x9e, 0x85, 0x67, 0x5f, 0x30, 0x4c, 0x25, 0x2d}};
static const GUID IID_IResourceManagerStatics_g   = {0x1cc0fdfc, 0x69ee, 0x4e43, {0x99, 0x01, 0x47, 0xf1, 0x26, 0x87, 0xba, 0xf7}};
static const GUID IID_IResourceMap_g              = {0x72284824, 0xdb8c, 0x42f8, {0xb0, 0x8c, 0x53, 0xff, 0x35, 0x7d, 0xad, 0x82}};
static const GUID IID_IResourceCandidate_g        = {0xaf5207d9, 0xc433, 0x4764, {0xb3, 0xfd, 0x8f, 0xa6, 0xbf, 0xbc, 0xba, 0xdc}};
static const GUID IID_IResourceCandidate2_g       = {0x69e5b468, 0xf6fc, 0x4013, {0xaa, 0xa2, 0xd5, 0x3f, 0x17, 0x57, 0xd3, 0xb5}};
static const GUID IID_IResourceCandidate3_g       = {0x08ae97f8, 0x517a, 0x4674, {0x95, 0x8c, 0x4a, 0x3c, 0x7c, 0xd2, 0xcc, 0x6b}};
static const GUID IID_INamedResource_g            = {0x1c98c219, 0x0b13, 0x4240, {0x89, 0xa5, 0xd4, 0x95, 0xdc, 0x18, 0x9a, 0x00}};
static const GUID IID_IResourceQualifier_g        = {0x785da5b2, 0x4afd, 0x4376, {0xa8, 0x88, 0xc5, 0xf9, 0xa6, 0xb7, 0xa0, 0x5c}};
static const GUID IID_IActivationFactory_g_       = {0x00000035, 0x0000, 0x0000, {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
/* Parameterized collection IIDs (IMapView<,>/IVectorView<>) are computed at runtime
 * by the RoGetParameterizedIID machinery; the plain vtable is what MRT callers use,
 * so we do not register static IIDs for them. */
#define ELEMENT_NOT_FOUND_E HRESULT_FROM_WIN32(0x490L)
/* Live-captured on Calculator (wbappgate run 2026-10-31): .NET Native RCW
 * queries this IID on both the ResourceContext factory and the activated
 * instance right after activation; answering E_NOINTERFACE leads to an
 * unhandled exception and FailFast c0000602 in SharedLibrary.dll.  This is
 * the Windows 8.1 generation identity for the ResourceContext class
 * (factory semantics: Create/CreateWithCurrentView). */
static const GUID IID_IResourceContext_v81_g      = {0x11d3b13a, 0x180e, 0x4789, {0xa8, 0xbe, 0x77, 0x12, 0x88, 0x28, 0x93, 0xe6}};
static const GUID IID_IResourceContextNamed_g     = {0x12f5efaf, 0x45fb, 0x4669, {0x93, 0x5c, 0x3f, 0x38, 0xa9, 0x0d, 0xab, 0x26}}; /* (unverified, 8.1 metadata) */

#define RTCLASS_RESOURCECONTEXT   L"Windows.ApplicationModel.Resources.Core.ResourceContext"
#define RTCLASS_RESOURCEMANAGER   L"Windows.ApplicationModel.Resources.Core.ResourceManager"
#define RTCLASS_RESOURCEMAP       L"Windows.ApplicationModel.Resources.Core.ResourceMap"
#define RTCLASS_RESOURCECANDIDATE L"Windows.ApplicationModel.Resources.Core.ResourceCandidate"
#define RTCLASS_NAMEDRESOURCE     L"Windows.ApplicationModel.Resources.Core.NamedResource"
#define RTCLASS_RESOURCEQUALIFIER L"Windows.ApplicationModel.Resources.Core.ResourceQualifier"
#define RTCLASS_MEMORYMANAGER     L"Windows.System.MemoryManager"

/* ------------------------------------------------------------------ */
/* pri state                                                           */
/* ------------------------------------------------------------------ */

static pri_index *the_index;
static volatile LONG pri_init_done;

static void init_pri(void)
{
    WCHAR path[4096];
    DWORD len;

    if (InterlockedCompareExchange(&pri_init_done, 1, 0)) return;

    len = GetEnvironmentVariableW( L"WINE_MRT_PRI_PATH", path, ARRAYSIZE( path ));
    if (len && len < ARRAYSIZE( path ) && (the_index = pri_load( path ))) return;

    GetModuleFileNameW( NULL, path, ARRAYSIZE( path ));
    {
        WCHAR *slash = wcsrchr( path, '\\' );
        if (slash) lstrcpyW( slash + 1, L"resources.pri" );
        if ((the_index = pri_load( path ))) return;
    }
    GetModuleFileNameW( GetModuleHandleW( L"windows.applicationmodel.resources.core.dll" ), path, ARRAYSIZE( path ));
    {
        WCHAR *slash = wcsrchr( path, '\\' );
        if (slash) lstrcpyW( slash + 1, L"resources.pri" );
        if ((the_index = pri_load( path ))) return;
    }
    lstrcpyW( path, L"resources.pri" );
    if ((the_index = pri_load( path ))) return;

    TRACE( "no resources.pri found, name-as-value stub tier only\n" );
}

/* ------------------------------------------------------------------ */
/* helpers                                                             */
/* ------------------------------------------------------------------ */

static inline LONG incr( LONG *ref ) { return InterlockedIncrement( ref ); }
static inline LONG decr( LONG *ref ) { return InterlockedDecrement( ref ); }

static WCHAR *lstrdupW( const WCHAR *str )
{
    SIZE_T bytes = (lstrlenW( str ) + 1) * sizeof(*str);
    WCHAR *out = HeapAlloc( GetProcessHeap(), 0, bytes );
    if (out) memcpy( out, str, bytes );
    return out;
}

static HRESULT simple_get_iids( void *iface, ULONG *count, IID **iids )
{
    *count = 0;
    *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI get_trust_base( void *iface, INT32 *trust )
{
    *trust = 0; /* BaseTrust */
    return S_OK;
}

static HRESULT className( const WCHAR *name, HSTRING *out )
{
    return WindowsCreateString( name, (UINT32)lstrlenW( name ), out );
}

#include "roparameterizediid.h"

/* implemented in dlls/combase/roapi.c, exported by combase; not declared by
 * the generated rparameterizediid.h */
HRESULT WINAPI RoGetParameterizedTypeInstanceIID( UINT32 name_element_count, const WCHAR **name_elements,
                                                  const IRoMetaDataLocator *locator, GUID *iid,
                                                  ROPARAMIIDHANDLE *handle );

#define ELEM_STRING       L"Windows.Foundation.String"
#define ELEM_KEYVALUEPAIR L"Windows.Foundation.Collections.IKeyValuePair"
#define ELEM_MAPVIEW      L"Windows.Foundation.Collections.IMapView"
#define ELEM_VECTORVIEW   L"Windows.Foundation.Collections.IVectorView"
#define ELEM_ITERABLE     L"Windows.Foundation.Collections.IIterable"
#define ELEM_ITERATOR     L"Windows.Foundation.Collections.IIterator"
#define ELEM_RESOURCEMAP  L"Windows.ApplicationModel.Resources.Core.ResourceMap"
#define ELEM_RESCANDIDATE L"Windows.ApplicationModel.Resources.Core.ResourceCandidate"
#define ELEM_RESQUALIFIER L"Windows.ApplicationModel.Resources.Core.ResourceQualifier"
#define ELEM_NAMEDRES     L"Windows.ApplicationModel.Resources.Core.NamedResource"


static BOOL param_iid_eq( REFIID iid, UINT32 count, ... );
#define IT_STRING 0
#define IT_KVPAIR 1
#define IT_RMPAIR 2
#define IT_IFACE  3
static const void *iterator_string_vtbl[];
static const void *iterator_pair_vtbl[];
static struct iterator *iter_new( UINT8 kind, void *src, UINT32 count, const void *vtbl );
static HRESULT WINAPI svv_First( void *iface, void **first );
static HRESULT WINAPI ivv_First( void *iface, void **first );
static HRESULT WINAPI mv_First( void *iface, void **first );
static HRESULT WINAPI rmv_First( void *iface, void **first );

/* forward */
struct res_map;
struct res_candidate;


/* ------------------------------------------------------------------ */
/* IVectorView<HSTRING> (Languages) / IVectorView<T*> generic          */
/* slots: [II 5][GetAt 6][Size 7][GetView 8][First 9]                  */
/* ------------------------------------------------------------------ */

struct str_vector
{
    const void *vt_vv;
    LONG ref;
    HSTRING *items;
    UINT32 count;
};

static inline struct str_vector *svv_from( void *iface ) { return (struct str_vector *)iface; }

static HRESULT WINAPI svv_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct str_vector *impl = svv_from( iface );
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        param_iid_eq( iid, 2, ELEM_VECTORVIEW L"`1", ELEM_STRING ) ||
        param_iid_eq( iid, 2, ELEM_ITERABLE L"`1", ELEM_STRING ))
    {
        *out = &impl->vt_vv;
        incr( &impl->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI svv_AddRef( void *iface ) { return incr( &svv_from( iface )->ref ); }
static ULONG WINAPI svv_Release( void *iface ) { return decr( &svv_from( iface )->ref ); }
static HRESULT WINAPI svv_GetIids( void *iface, ULONG *c, IID **i ) { return simple_get_iids( iface, c, i ); }
static HRESULT WINAPI svv_GetRuntimeClassName( void *iface, HSTRING *n )
{
    return WindowsCreateString( L"Windows.Foundation.Collections.IVectorView`1<Windows.Foundation.String>", 78, n );
}
static HRESULT WINAPI svv_GetTrustLevel( void *iface, INT32 *t ) { return get_trust_base( iface, t ); }
static HRESULT WINAPI svv_GetAt( void *iface, UINT32 index, HSTRING *item )
{
    struct str_vector *impl = svv_from( iface );
    if (index >= impl->count) return E_BOUNDS;
    *item = impl->items[index];
    return S_OK;
}
static HRESULT WINAPI svv_Size( void *iface, UINT32 *size )
{
    *size = svv_from( iface )->count;
    return S_OK;
}
static HRESULT WINAPI svv_GetView( void *iface, void **view )
{
    *view = iface;
    incr( &svv_from( iface )->ref );
    return S_OK;
}
static const void *str_vector_vtbl[] =
{
    svv_QueryInterface, svv_AddRef, svv_Release, svv_GetIids, svv_GetRuntimeClassName, svv_GetTrustLevel,
    svv_GetAt, svv_Size, svv_GetView, svv_First,
};

/* IVectorView<T*> for interface pointers (candidates, qualifiers, maps, named resources)
 * slots identical: GetAt, Size, GetView */
struct iface_vector
{
    const void *vt_vv;
    LONG ref;
    void **items;
    UINT32 count;
    const WCHAR *elem;
};
static inline struct iface_vector *ivv_from( void *iface ) { return (struct iface_vector *)iface; }
static HRESULT WINAPI ivv_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct iface_vector *impl = ivv_from( iface );
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        (impl->elem && (param_iid_eq( iid, 2, ELEM_VECTORVIEW L"`1", impl->elem ) ||
                        param_iid_eq( iid, 2, ELEM_ITERABLE L"`1", impl->elem ))))
    {
        *out = &impl->vt_vv;
        incr( &impl->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI ivv_AddRef( void *iface ) { return incr( &ivv_from( iface )->ref ); }
static ULONG WINAPI ivv_Release( void *iface ) { return decr( &ivv_from( iface )->ref ); }
static HRESULT WINAPI ivv_GetIids( void *iface, ULONG *c, IID **i ) { return simple_get_iids( iface, c, i ); }
static HRESULT WINAPI ivv_GetRuntimeClassName( void *iface, HSTRING *n ) { return WindowsCreateString( L"Foundation.Collections.IVectorView", 37, n ); }
static HRESULT WINAPI ivv_GetTrustLevel( void *iface, INT32 *t ) { return get_trust_base( iface, t ); }
static HRESULT WINAPI ivv_GetAt( void *iface, UINT32 index, void **item )
{
    struct iface_vector *impl = ivv_from( iface );
    if (index >= impl->count) return E_BOUNDS;
    *item = impl->items[index];
    return S_OK;
}
static HRESULT WINAPI ivv_Size( void *iface, UINT32 *size )
{
    *size = ivv_from( iface )->count;
    return S_OK;
}
static HRESULT WINAPI ivv_GetView( void *iface, void **view )
{
    *view = iface;
    incr( &ivv_from( iface )->ref );
    return S_OK;
}
static const void *iface_vector_vtbl[] =
{
    ivv_QueryInterface, ivv_AddRef, ivv_Release, ivv_GetIids, ivv_GetRuntimeClassName, ivv_GetTrustLevel,
    ivv_GetAt, ivv_Size, ivv_GetView, ivv_First,
};

/* ------------------------------------------------------------------ */
/* IMapView<HSTRING,HSTRING> (QualifierValues)                         */
/* slots: [II 5][Get 6][HasKey 7][Size 8][GetView 9]                   */
/* ------------------------------------------------------------------ */

struct kv_pair
{
    const WCHAR *key;
    const WCHAR *value;
};

struct map_view
{
    const void *vt_mv;
    LONG ref;
    const struct kv_pair *pairs;
    UINT32 count;
};
static inline struct map_view *mv_from( void *iface ) { return (struct map_view *)iface; }

static HRESULT WINAPI mv_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct map_view *impl = mv_from( iface );
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        param_iid_eq( iid, 3, ELEM_MAPVIEW L"`2", ELEM_STRING, ELEM_STRING ))
    {
        *out = &impl->vt_mv;
        incr( &impl->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI mv_AddRef( void *iface ) { return incr( &mv_from( iface )->ref ); }
static ULONG WINAPI mv_Release( void *iface ) { return decr( &mv_from( iface )->ref ); }
static HRESULT WINAPI mv_GetIids( void *iface, ULONG *c, IID **i ) { return simple_get_iids( iface, c, i ); }
static HRESULT WINAPI mv_GetRuntimeClassName( void *iface, HSTRING *n ) { return WindowsCreateString( L"Foundation.Collections.IMapView", 35, n ); }
static HRESULT WINAPI mv_GetTrustLevel( void *iface, INT32 *t ) { return get_trust_base( iface, t ); }
static HRESULT WINAPI mv_Get( void *iface, HSTRING key, HSTRING *value )
{
    struct map_view *impl = mv_from( iface );
    const WCHAR *raw = WindowsGetStringRawBuffer( key, NULL );
    UINT32 i;
    for (i = 0; i < impl->count; i++)
    {
        if (!lstrcmpiW( raw, impl->pairs[i].key ))
            return WindowsCreateString( impl->pairs[i].value, (UINT32)lstrlenW( impl->pairs[i].value ), value );
    }
    return ELEMENT_NOT_FOUND_E; /* ELEMENT_NOT_FOUND */
}
static HRESULT WINAPI mv_HasKey( void *iface, HSTRING key, INT32 *has )
{
    struct map_view *impl = mv_from( iface );
    const WCHAR *raw = WindowsGetStringRawBuffer( key, NULL );
    UINT32 i;
    *has = FALSE;
    for (i = 0; i < impl->count; i++)
        if (!lstrcmpiW( raw, impl->pairs[i].key )) { *has = TRUE; break; }
    return S_OK;
}
static HRESULT WINAPI mv_Size( void *iface, UINT32 *size )
{
    *size = mv_from( iface )->count;
    return S_OK;
}
static HRESULT WINAPI mv_GetView( void *iface, void **view )
{
    *view = iface;
    incr( &mv_from( iface )->ref );
    return S_OK;
}
static const void *map_view_vtbl[] =
{
    mv_QueryInterface, mv_AddRef, mv_Release, mv_GetIids, mv_GetRuntimeClassName, mv_GetTrustLevel,
    mv_Get, mv_HasKey, mv_Size, mv_GetView, mv_First,
};

/* IMapView<HSTRING, IResourceMap*> for AllResourceMaps: same slots, Get
 * returns the map pointer. */
struct rm_map_entry { WCHAR *key; void *map; };
struct rm_map_view
{
    const void *vt_mv;
    LONG ref;
    struct rm_map_entry *entries;
    UINT32 count;
};
static inline struct rm_map_view *rmv_from( void *iface ) { return (struct rm_map_view *)iface; }
static HRESULT WINAPI rmv_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct rm_map_view *impl = rmv_from( iface );
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        param_iid_eq( iid, 3, ELEM_MAPVIEW L"`2", ELEM_STRING, ELEM_RESOURCEMAP ))
    {
        *out = &impl->vt_mv;
        incr( &impl->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI rmv_AddRef( void *iface ) { return incr( &rmv_from( iface )->ref ); }
static ULONG WINAPI rmv_Release( void *iface ) { return decr( &rmv_from( iface )->ref ); }
static HRESULT WINAPI rmv_GetIids( void *iface, ULONG *c, IID **i ) { return simple_get_iids( iface, c, i ); }
static HRESULT WINAPI rmv_GetRuntimeClassName( void *iface, HSTRING *n ) { return WindowsCreateString( L"Foundation.Collections.IMapView", 35, n ); }
static HRESULT WINAPI rmv_GetTrustLevel( void *iface, INT32 *t ) { return get_trust_base( iface, t ); }
static HRESULT WINAPI rmv_Get( void *iface, HSTRING key, void **map )
{
    struct rm_map_view *impl = rmv_from( iface );
    const WCHAR *raw = WindowsGetStringRawBuffer( key, NULL );
    UINT32 i;
    for (i = 0; i < impl->count; i++)
        if (!lstrcmpiW( raw, impl->entries[i].key ))
        {
            *map = impl->entries[i].map;
            return S_OK;
        }
    return ELEMENT_NOT_FOUND_E;
}
static HRESULT WINAPI rmv_HasKey( void *iface, HSTRING key, INT32 *has )
{
    struct rm_map_view *impl = rmv_from( iface );
    const WCHAR *raw = WindowsGetStringRawBuffer( key, NULL );
    UINT32 i;
    *has = FALSE;
    for (i = 0; i < impl->count; i++)
        if (!lstrcmpiW( raw, impl->entries[i].key )) { *has = TRUE; break; }
    return S_OK;
}
static HRESULT WINAPI rmv_Size( void *iface, UINT32 *size )
{
    *size = rmv_from( iface )->count;
    return S_OK;
}
static HRESULT WINAPI rmv_GetView( void *iface, void **view )
{
    *view = iface;
    incr( &rmv_from( iface )->ref );
    return S_OK;
}
static const void *rm_map_view_vtbl[] =
{
    rmv_QueryInterface, rmv_AddRef, rmv_Release, rmv_GetIids, rmv_GetRuntimeClassName, rmv_GetTrustLevel,
    rmv_Get, rmv_HasKey, rmv_Size, rmv_GetView, rmv_First,
};

/* ------------------------------------------------------------------ */
/* parameterized-IID matching + IIterable/IIterator support            */
/* The .NET Native RCW computes the pinterface IID from the generic    */
/* name and QIs collections for it (IMapView/IVectorView/IIterable and */
/* IKeyValuePair/IIterator).  Missing support => NullReference loop =>  */
/* stack overflow on the managed thread (observed on Calculator).      */
/* ------------------------------------------------------------------ */

static BOOL param_iid_eq( REFIID iid, UINT32 count, ... )
{
    const WCHAR *elems[4];
    va_list ap;
    UINT32 i;
    GUID g;
    ROPARAMIIDHANDLE h;

    va_start( ap, count );
    for (i = 0; i < count && i < 4; i++) elems[i] = va_arg( ap, const WCHAR * );
    va_end( ap );
    if (FAILED( RoGetParameterizedTypeInstanceIID( count, elems, NULL, &g, &h ) )) return FALSE;
    return IsEqualGUID( iid, &g );
}

/* IKeyValuePair<HSTRING,HSTRING> [II 5][Key 6][Value 7] */
struct kv_pair_obj
{
    const void *vt_pair;
    LONG ref;
    const WCHAR *key;
    const WCHAR *value;
};
static inline struct kv_pair_obj *pair_from( void *iface ) { return (struct kv_pair_obj *)iface; }
static HRESULT WINAPI pair_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct kv_pair_obj *impl = pair_from( iface );
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        param_iid_eq( iid, 3, ELEM_KEYVALUEPAIR L"`2", ELEM_STRING, ELEM_STRING ))
    {
        *out = &impl->vt_pair;
        incr( &impl->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI pair_AddRef( void *iface ) { return incr( &pair_from( iface )->ref ); }
static ULONG WINAPI pair_Release( void *iface ) { return decr( &pair_from( iface )->ref ); }
static HRESULT WINAPI pair_GetIids( void *iface, ULONG *c, IID **i ) { return simple_get_iids( iface, c, i ); }
static HRESULT WINAPI pair_GetRuntimeClassName( void *iface, HSTRING *n )
{
    return WindowsCreateString( L"Windows.Foundation.Collections.IKeyValuePair`2<Windows.Foundation.String,Windows.Foundation.String>",
                                 113, n );
}
static HRESULT WINAPI pair_GetTrustLevel( void *iface, INT32 *t ) { return get_trust_base( iface, t ); }
static HRESULT WINAPI pair_Key( void *iface, HSTRING *v )
{
    struct kv_pair_obj *impl = pair_from( iface );
    return WindowsCreateString( impl->key, (UINT32)lstrlenW( impl->key ), v );
}
static HRESULT WINAPI pair_Value( void *iface, HSTRING *v )
{
    struct kv_pair_obj *impl = pair_from( iface );
    return WindowsCreateString( impl->value, (UINT32)lstrlenW( impl->value ), v );
}
static const void *kv_pair_obj_vtbl[] =
{
    pair_QueryInterface, pair_AddRef, pair_Release, pair_GetIids, pair_GetRuntimeClassName, pair_GetTrustLevel,
    pair_Key, pair_Value,
};

/* IKeyValuePair<HSTRING,IResourceMap*> */
struct rm_pair_obj
{
    const void *vt_pair;
    LONG ref;
    WCHAR *key;
    void *map;
};
static inline struct rm_pair_obj *rmpair_from( void *iface ) { return (struct rm_pair_obj *)iface; }
static HRESULT WINAPI rmpair_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct rm_pair_obj *impl = rmpair_from( iface );
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        param_iid_eq( iid, 3, ELEM_KEYVALUEPAIR L"`2", ELEM_STRING, ELEM_RESOURCEMAP ))
    {
        *out = &impl->vt_pair;
        incr( &impl->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI rmpair_AddRef( void *iface ) { return incr( &rmpair_from( iface )->ref ); }
static ULONG WINAPI rmpair_Release( void *iface ) { return decr( &rmpair_from( iface )->ref ); }
static HRESULT WINAPI rmpair_GetIids( void *iface, ULONG *c, IID **i ) { return simple_get_iids( iface, c, i ); }
static HRESULT WINAPI rmpair_GetRuntimeClassName( void *iface, HSTRING *n )
{
    return WindowsCreateString( L"Windows.Foundation.Collections.IKeyValuePair`2<Windows.Foundation.String,Windows.ApplicationModel.Resources.Core.ResourceMap>",
                                 149, n );
}
static HRESULT WINAPI rmpair_GetTrustLevel( void *iface, INT32 *t ) { return get_trust_base( iface, t ); }
static HRESULT WINAPI rmpair_Key( void *iface, HSTRING *v )
{
    struct rm_pair_obj *impl = rmpair_from( iface );
    return WindowsCreateString( impl->key, (UINT32)lstrlenW( impl->key ), v );
}
static HRESULT WINAPI rmpair_Value( void *iface, void **v )
{
    struct rm_pair_obj *impl = rmpair_from( iface );
    *v = impl->map;
    return S_OK;
}
static const void *rm_pair_obj_vtbl[] =
{
    rmpair_QueryInterface, rmpair_AddRef, rmpair_Release, rmpair_GetIids, rmpair_GetRuntimeClassName, rmpair_GetTrustLevel,
    rmpair_Key, rmpair_Value,
};

/* IIterator<T> [II 5][MoveNext 6][GetCurrent 7][get_HasCurrent 8][get_Current 9]
 * T is: HSTRING (out HSTRING), kv pair (HSTRING,HSTRING), rm pair (HSTRING,map), iface ptr. */
struct iterator
{
    const void *vt_it;
    LONG ref;
    UINT32 count;
    UINT32 idx;
    INT32 begun;
    UINT8 kind;   /* 0 string, 1 kvpair, 2 rm pair, 3 iface ptr */
    union
    {
        struct map_view *mv;
        struct rm_map_view *rmv;
        struct str_vector *sv;
        struct iface_vector *iv;
    } src;
};
static inline struct iterator *iter_from( void *iface ) { return (struct iterator *)iface; }

#define IT_STRING 0
#define IT_KVPAIR 1
#define IT_RMPAIR 2
#define IT_IFACE  3

static HRESULT WINAPI iter_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct iterator *impl = iter_from( iface );
    BOOL ok = IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
              IsEqualGUID( iid, &IID_IAgileObject );
    if (!ok)
    {
        switch (impl->kind)
        {
        case IT_STRING: ok = param_iid_eq( iid, 2, ELEM_ITERATOR L"`1", ELEM_STRING ); break;
        case IT_KVPAIR: ok = param_iid_eq( iid, 4, ELEM_ITERATOR L"`1", ELEM_KEYVALUEPAIR L"`2", ELEM_STRING, ELEM_STRING ); break;
        case IT_RMPAIR: ok = param_iid_eq( iid, 4, ELEM_ITERATOR L"`1", ELEM_KEYVALUEPAIR L"`2", ELEM_STRING, ELEM_RESOURCEMAP ); break;
        case IT_IFACE:  ok = TRUE; break;
        }
    }
    if (ok)
    {
        *out = &impl->vt_it;
        incr( &impl->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI iter_AddRef( void *iface ) { return incr( &iter_from( iface )->ref ); }
static ULONG WINAPI iter_Release( void *iface ) { return decr( &iter_from( iface )->ref ); }
static HRESULT WINAPI iter_GetIids( void *iface, ULONG *c, IID **i ) { return simple_get_iids( iface, c, i ); }
static HRESULT WINAPI iter_GetRuntimeClassName( void *iface, HSTRING *n )
{
    return WindowsCreateString( L"Windows.Foundation.Collections.IIterator", 40, n );
}
static HRESULT WINAPI iter_GetTrustLevel( void *iface, INT32 *t ) { return get_trust_base( iface, t ); }
static HRESULT WINAPI iter_MoveNext( void *iface, INT32 *result )
{
    struct iterator *impl = iter_from( iface );
    UINT32 next = impl->begun ? impl->idx + 1 : 0;
    impl->begun = 1;
    if (next < impl->count) { impl->idx = next; *result = TRUE; }
    else { impl->idx = impl->count; *result = FALSE; }
    return S_OK;
}
static HRESULT WINAPI iter_HasCurrent( void *iface, INT32 *result )
{
    struct iterator *impl = iter_from( iface );
    *result = impl->begun && impl->idx < impl->count;
    return S_OK;
}
static HRESULT WINAPI iter_GetCurrent_String( void *iface, HSTRING *v )
{
    struct iterator *impl = iter_from( iface );
    if (impl->kind != IT_STRING || !impl->begun || impl->idx >= impl->count) return E_ILLEGAL_METHOD_CALL;
    return WindowsCreateString( impl->src.sv->items[impl->idx],
                                WindowsGetStringLen( impl->src.sv->items[impl->idx] ), v );
}
static HRESULT WINAPI iter_GetCurrent_Pair( void *iface, void **v )
{
    struct iterator *impl = iter_from( iface );
    if (!impl->begun || impl->idx >= impl->count) return E_ILLEGAL_METHOD_CALL;
    if (impl->kind == IT_KVPAIR)
    {
        struct kv_pair_obj *p = HeapAlloc( GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*p) );
        if (!p) return E_OUTOFMEMORY;
        p->vt_pair = kv_pair_obj_vtbl;
        p->ref = 1;
        p->key = impl->src.mv->pairs[impl->idx].key;
        p->value = impl->src.mv->pairs[impl->idx].value;
        *v = &p->vt_pair;
        return S_OK;
    }
    if (impl->kind == IT_RMPAIR)
    {
        struct rm_pair_obj *p = HeapAlloc( GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*p) );
        if (!p) return E_OUTOFMEMORY;
        p->vt_pair = rm_pair_obj_vtbl;
        p->ref = 1;
        p->key = impl->src.rmv->entries[impl->idx].key;
        p->map = impl->src.rmv->entries[impl->idx].map;
        *v = &p->vt_pair;
        return S_OK;
    }
    /* IT_IFACE */
    {
        void *item = impl->src.iv->items[impl->idx];
        void *vt = ((void **)item)[0];
        return ((HRESULT (STDMETHODCALLTYPE *)(void *, REFIID, void **))((void **)vt)[0])
               (item, &IID_IInspectable, v);
    }
}
/* two iterator vtables: string-current and pair-current (GetCurrent slot type differs only
 * in the out-parameter kind) */
static const void *iterator_string_vtbl[] =
{
    iter_QueryInterface, iter_AddRef, iter_Release, iter_GetIids, iter_GetRuntimeClassName, iter_GetTrustLevel,
    iter_MoveNext, iter_GetCurrent_String, iter_HasCurrent,
};
static const void *iterator_pair_vtbl[] =
{
    iter_QueryInterface, iter_AddRef, iter_Release, iter_GetIids, iter_GetRuntimeClassName, iter_GetTrustLevel,
    iter_MoveNext, iter_GetCurrent_Pair, iter_HasCurrent,
};
static struct iterator *iter_new( UINT8 kind, void *src, UINT32 count, const void *vtbl )
{
    struct iterator *it = HeapAlloc( GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*it) );
    if (!it) return NULL;
    it->vt_it = vtbl;
    it->ref = 1;
    it->count = count;
    it->kind = kind;
    it->src.mv = (struct map_view *)src;
    return it;
}

static HRESULT WINAPI svv_First( void *iface, void **first )
{
    struct str_vector *impl = svv_from( iface );
    struct iterator *it = iter_new( IT_STRING, impl, impl->count, iterator_string_vtbl );
    if (!it) return E_OUTOFMEMORY;
    *first = &it->vt_it;
    return S_OK;
}

static HRESULT WINAPI ivv_First( void *iface, void **first )
{
    struct iface_vector *impl = ivv_from( iface );
    struct iterator *it = iter_new( IT_IFACE, impl, impl->count, iterator_pair_vtbl );
    if (!it) return E_OUTOFMEMORY;
    *first = &it->vt_it;
    return S_OK;
}

static HRESULT WINAPI mv_First( void *iface, void **first )
{
    struct map_view *impl = mv_from( iface );
    struct iterator *it = iter_new( IT_KVPAIR, impl, impl->count, iterator_pair_vtbl );
    if (!it) return E_OUTOFMEMORY;
    *first = &it->vt_it;
    return S_OK;
}

static HRESULT WINAPI rmv_First( void *iface, void **first )
{
    struct rm_map_view *impl = rmv_from( iface );
    struct iterator *it = iter_new( IT_RMPAIR, impl, impl->count, iterator_pair_vtbl );
    if (!it) return E_OUTOFMEMORY;
    *first = &it->vt_it;
    return S_OK;
}


/* ------------------------------------------------------------------ */
/* ResourceQualifier: [II 5][QualifierName 6][QualifierValue 7][IsDefault 8][IsMatch 9][Score 10] */
/* ------------------------------------------------------------------ */

struct qualifier
{
    const void *vt_q;
    LONG ref;
    const WCHAR *name;
    const WCHAR *value;
};
static inline struct qualifier *q_from( void *iface ) { return (struct qualifier *)iface; }
static HRESULT WINAPI q_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct qualifier *impl = q_from( iface );
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IResourceQualifier_g ))
    {
        *out = &impl->vt_q;
        incr( &impl->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI q_AddRef( void *iface ) { return incr( &q_from( iface )->ref ); }
static ULONG WINAPI q_Release( void *iface ) { return decr( &q_from( iface )->ref ); }
static HRESULT WINAPI q_GetIids( void *iface, ULONG *c, IID **i ) { return simple_get_iids( iface, c, i ); }
static HRESULT WINAPI q_GetRuntimeClassName( void *iface, HSTRING *n ) { return className( RTCLASS_RESOURCEQUALIFIER, n ); }
static HRESULT WINAPI q_GetTrustLevel( void *iface, INT32 *t ) { return get_trust_base( iface, t ); }
static HRESULT WINAPI q_Name( void *iface, HSTRING *n ) { return WindowsCreateString( q_from( iface )->name, (UINT32)lstrlenW( q_from( iface )->name ), n ); }
static HRESULT WINAPI q_Value( void *iface, HSTRING *v ) { return WindowsCreateString( q_from( iface )->value, (UINT32)lstrlenW( q_from( iface )->value ), v ); }
static HRESULT WINAPI q_IsDefault( void *iface, INT32 *v ) { *v = TRUE; return S_OK; }
static HRESULT WINAPI q_IsMatch( void *iface, INT32 *v ) { *v = TRUE; return S_OK; }
static HRESULT WINAPI q_Score( void *iface, DOUBLE *v ) { *v = 1.0; return S_OK; }
static const void *qualifier_vtbl[] =
{
    q_QueryInterface, q_AddRef, q_Release, q_GetIids, q_GetRuntimeClassName, q_GetTrustLevel,
    q_Name, q_Value, q_IsDefault, q_IsMatch, q_Score,
};

/* ------------------------------------------------------------------ */
/* ResourceCandidate: [II 5][Qualifiers 6][IsMatch 7][IsMatchAsDefault 8][IsDefault 9]
 *                    [ValueAsString 10][GetValueAsFileAsync 11][GetQualifierValue 12]
 * IResourceCandidate2: [II 5][GetValueAsStreamAsync 6]
 * IResourceCandidate3: [II 5][Kind 6]                                                       */
/* ------------------------------------------------------------------ */

struct res_candidate
{
    const void *vt_cand;
    const void *vt_cand2;
    const void *vt_cand3;
    LONG ref;
    WCHAR *full_name;
    WCHAR *value;
    struct kv_pair *qpairs;
    UINT32 qcount;
};
static inline struct res_candidate *cand_from( void *iface ) { return (struct res_candidate *)iface; }
static inline struct res_candidate *cand2_from( void *iface ) { return (struct res_candidate *)((BYTE *)iface - sizeof(void *)); }
static inline struct res_candidate *cand3_from( void *iface ) { return (struct res_candidate *)((BYTE *)iface - 2 * sizeof(void *)); }

static HRESULT WINAPI cand_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct res_candidate *impl = cand_from( iface );
    TRACE( "iid %s\n", debugstr_guid( iid ) );
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IResourceCandidate_g ))
    {
        *out = &impl->vt_cand;
        incr( &impl->ref );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IResourceCandidate2_g ))
    {
        *out = &impl->vt_cand2;
        incr( &impl->ref );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IResourceCandidate3_g ))
    {
        *out = &impl->vt_cand3;
        incr( &impl->ref );
        return S_OK;
    }
    FIXME( "%s not implemented\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI cand_AddRef( void *iface ) { return incr( &cand_from( iface )->ref ); }
static ULONG WINAPI cand_Release( void *iface )
{
    struct res_candidate *impl = cand_from( iface );
    return decr( &impl->ref );
}
static HRESULT WINAPI cand_GetIids( void *iface, ULONG *c, IID **i ) { return simple_get_iids( iface, c, i ); }
static HRESULT WINAPI cand_GetRuntimeClassName( void *iface, HSTRING *n ) { return className( RTCLASS_RESOURCECANDIDATE, n ); }
static HRESULT WINAPI cand_GetTrustLevel( void *iface, INT32 *t ) { return get_trust_base( iface, t ); }
static HRESULT WINAPI cand_Qualifiers( void *iface, void **out )
{
    struct res_candidate *impl = cand_from( iface );
    struct iface_vector *vec;
    void **items;
    UINT32 i;

    *out = NULL;
    items = impl->qcount ? HeapAlloc( GetProcessHeap(), 0, impl->qcount * sizeof(*items) ) : NULL;
    if (impl->qcount && !items) return E_OUTOFMEMORY;
    for (i = 0; i < impl->qcount; i++)
    {
        struct qualifier *q = HeapAlloc( GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*q) );
        if (!q) break;
        q->vt_q = qualifier_vtbl;
        q->ref = 1;
        q->name = impl->qpairs[i].key;
        q->value = impl->qpairs[i].value;
        items[i] = &q->vt_q;
    }
    vec = HeapAlloc( GetProcessHeap(), 0, sizeof(*vec) );
    if (!vec) { HeapFree( GetProcessHeap(), 0, items ); return E_OUTOFMEMORY; }
    vec->vt_vv = iface_vector_vtbl;
    vec->ref = 1;
    vec->items = items;
    vec->count = impl->qcount;
    vec->elem = ELEM_RESQUALIFIER;
    *out = &vec->vt_vv;
    return S_OK;
}
static HRESULT WINAPI cand_IsMatch( void *iface, INT32 *v ) { *v = TRUE; return S_OK; }
static HRESULT WINAPI cand_IsMatchAsDefault( void *iface, INT32 *v ) { *v = TRUE; return S_OK; }
static HRESULT WINAPI cand_IsDefault( void *iface, INT32 *v ) { *v = FALSE; return S_OK; }
static HRESULT WINAPI cand_ValueAsString( void *iface, HSTRING *v )
{
    struct res_candidate *impl = cand_from( iface );
    return WindowsCreateString( impl->value, (UINT32)lstrlenW( impl->value ), v );
}
static HRESULT WINAPI cand_GetValueAsFileAsync( void *iface, void **op )
{
    FIXME( "stub\n" );
    *op = NULL;
    return E_NOTIMPL;
}
static HRESULT WINAPI cand_GetQualifierValue( void *iface, HSTRING name, HSTRING *value )
{
    struct res_candidate *impl = cand_from( iface );
    const WCHAR *raw = WindowsGetStringRawBuffer( name, NULL );
    UINT32 i;
    for (i = 0; i < impl->qcount; i++)
        if (!lstrcmpiW( raw, impl->qpairs[i].key ))
            return WindowsCreateString( impl->qpairs[i].value, (UINT32)lstrlenW( impl->qpairs[i].value ), value );
    return ELEMENT_NOT_FOUND_E;
}
static const void *res_candidate_vtbl[] =
{
    cand_QueryInterface, cand_AddRef, cand_Release, cand_GetIids, cand_GetRuntimeClassName, cand_GetTrustLevel,
    cand_Qualifiers, cand_IsMatch, cand_IsMatchAsDefault, cand_IsDefault,
    cand_ValueAsString, cand_GetValueAsFileAsync, cand_GetQualifierValue,
};
static HRESULT WINAPI cand2_QueryInterface( void *iface, REFIID iid, void **out ) { return cand_QueryInterface( cand2_from( iface ), iid, out ); }
static ULONG WINAPI cand2_AddRef( void *iface ) { return incr( &cand2_from( iface )->ref ); }
static ULONG WINAPI cand2_Release( void *iface ) { return decr( &cand2_from( iface )->ref ); }
static HRESULT WINAPI cand2_GetIids( void *iface, ULONG *c, IID **i ) { return simple_get_iids( iface, c, i ); }
static HRESULT WINAPI cand2_GetRuntimeClassName( void *iface, HSTRING *n ) { return className( RTCLASS_RESOURCECANDIDATE, n ); }
static HRESULT WINAPI cand2_GetTrustLevel( void *iface, INT32 *t ) { return get_trust_base( iface, t ); }
static HRESULT WINAPI cand2_GetValueAsStreamAsync( void *iface, void **op ) { FIXME( "stub\n" ); *op = NULL; return E_NOTIMPL; }
static const void *res_candidate2_vtbl[] =
{
    cand2_QueryInterface, cand2_AddRef, cand2_Release, cand2_GetIids, cand2_GetRuntimeClassName, cand2_GetTrustLevel,
    cand2_GetValueAsStreamAsync,
};
static HRESULT WINAPI cand3_QueryInterface( void *iface, REFIID iid, void **out ) { return cand_QueryInterface( cand3_from( iface ), iid, out ); }
static ULONG WINAPI cand3_AddRef( void *iface ) { return incr( &cand3_from( iface )->ref ); }
static ULONG WINAPI cand3_Release( void *iface ) { return decr( &cand3_from( iface )->ref ); }
static HRESULT WINAPI cand3_GetIids( void *iface, ULONG *c, IID **i ) { return simple_get_iids( iface, c, i ); }
static HRESULT WINAPI cand3_GetRuntimeClassName( void *iface, HSTRING *n ) { return className( RTCLASS_RESOURCECANDIDATE, n ); }
static HRESULT WINAPI cand3_GetTrustLevel( void *iface, INT32 *t ) { return get_trust_base( iface, t ); }
static HRESULT WINAPI cand3_Kind( void *iface, INT32 *kind ) { *kind = 1 /* String */; return S_OK; }
static const void *res_candidate3_vtbl[] =
{
    cand3_QueryInterface, cand3_AddRef, cand3_Release, cand3_GetIids, cand3_GetRuntimeClassName, cand3_GetTrustLevel,
    cand3_Kind,
};

/* ------------------------------------------------------------------ */
/* ResourceMap: [II 5][Uri 6][GetValue 7][GetValueForContext 8][GetSubtree 9] */
/* ------------------------------------------------------------------ */

#define MAX_SUBTREES 64
static struct res_map *subtree_maps[MAX_SUBTREES];
static UINT32 subtree_count;
static CRITICAL_SECTION subtree_cs;
static volatile LONG cs_done;

struct res_map
{
    const void *vt_map;
    LONG ref;
    WCHAR *prefix;  /* subtree prefix "X\" for GetSubtree("X"), NULL for main map */
};
static inline struct res_map *map_from( void *iface ) { return (struct res_map *)iface; }

static struct kv_pair default_qualifiers[] =
{
    { L"Language", L"en-US" },
    { L"Scale", L"100" },
    { L"Contrast", L"standard" },
    { L"HomeRegion", L"001" },
    { L"TargetSize", L"unscaled" },
    { L"DXLanguage", L"full" },
    { L"LayoutDirection", L"ltr" },
    { L"AltForm", L"uninstalled" },
};

static void ensure_cs(void)
{
    if (InterlockedCompareExchange(&cs_done, 1, 0)) return;
    InitializeCriticalSection( &subtree_cs );
    InterlockedExchange(&cs_done, 2);
}

static HRESULT create_candidate( const WCHAR *full, WCHAR *value, struct res_candidate **out )
{
    struct res_candidate *cand = HeapAlloc( GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*cand) );
    if (!cand) return E_OUTOFMEMORY;
    cand->vt_cand = res_candidate_vtbl;
    cand->vt_cand2 = res_candidate2_vtbl;
    cand->vt_cand3 = res_candidate3_vtbl;
    cand->ref = 1;
    cand->full_name = full ? lstrdupW( full ) : NULL;
    cand->value = value;
    cand->qpairs = default_qualifiers;
    cand->qcount = ARRAYSIZE( default_qualifiers );
    *out = cand;
    return S_OK;
}

struct res_map;
static const void *res_map_vtbl[];
static HRESULT map_lookup( struct res_map *map, const WCHAR *key, struct res_candidate **out )
{
    WCHAR full[2048], namebuf[2048];
    WCHAR *value = NULL;

    init_pri();
    full[0] = 0;
    if (map->prefix) lstrcpynW( full, map->prefix, ARRAYSIZE( full ));
    lstrcatW( full, key );

    if (the_index && pri_lookup( the_index, full, namebuf, ARRAYSIZE( namebuf ), &value ))
        return create_candidate( full, value, out );
    /* documented stub tier: key-as-value so MRT lookups never fail */
    WARN( "resources.pri lookup failed for %s, using key as value\n", debugstr_w( full ) );
    {
        WCHAR *v = HeapAlloc( GetProcessHeap(), 0, (wcslen( key ) + 1) * sizeof(WCHAR) );
        if (!v) return E_OUTOFMEMORY;
        wcscpy( v, key );
        return create_candidate( full, v, out );
    }
}

static HRESULT WINAPI map_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct res_map *impl = map_from( iface );
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IResourceMap_g ))
    {
        *out = &impl->vt_map;
        incr( &impl->ref );
        return S_OK;
    }
    FIXME( "%s not implemented (Map2/Map3 GetCount/FullCount)\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI map_AddRef( void *iface ) { return incr( &map_from( iface )->ref ); }
static ULONG WINAPI map_Release( void *iface ) { return decr( &map_from( iface )->ref ); }
static HRESULT WINAPI map_GetIids( void *iface, ULONG *c, IID **i ) { return simple_get_iids( iface, c, i ); }
static HRESULT WINAPI map_GetRuntimeClassName( void *iface, HSTRING *n ) { return className( RTCLASS_RESOURCEMAP, n ); }
static HRESULT WINAPI map_GetTrustLevel( void *iface, INT32 *t ) { return get_trust_base( iface, t ); }
static HRESULT WINAPI map_Uri( void *iface, HSTRING *uri )
{
    WCHAR buf[2200];
    lstrcpyW( buf, L"ms-appx:///" );
    if (map_from( iface )->prefix) lstrcatW( buf, map_from( iface )->prefix );
    return WindowsCreateString( buf, (UINT32)lstrlenW( buf ), uri );
}
static HRESULT WINAPI map_GetValue( void *iface, HSTRING key, void **out )
{
    struct res_map *impl = map_from( iface );
    struct res_candidate *cand;
    HRESULT hr = map_lookup( impl, WindowsGetStringRawBuffer( key, NULL ), &cand );
    TRACE( "key %s hr=%08x\n", debugstr_hstring( key ), hr );
    if (SUCCEEDED( hr )) *out = &cand->vt_cand;
    else *out = NULL;
    return hr;
}
static HRESULT WINAPI map_GetValueForContext( void *iface, HSTRING key, void *context, void **out )
{
    return map_GetValue( iface, key, out );
}
static HRESULT WINAPI map_GetSubtree( void *iface, HSTRING name, void **out )
{
    struct res_map *impl = map_from( iface );
    const WCHAR *raw = WindowsGetStringRawBuffer( name, NULL );
    WCHAR prefix[1024];
    UINT32 i;

    TRACE( "name %s\n", debugstr_hstring( name ) );
    lstrcpyW( prefix, raw );
    if (prefix[lstrlenW( prefix ) - 1] != L'\\') lstrcatW( prefix, L"\\" );
    if (impl->prefix) lstrcatW( prefix, impl->prefix );
    /* swap: WinRT subtree prefix is the map name as prefix */
    {
        WCHAR tmp[1024];
        lstrcpyW( tmp, impl->prefix ? impl->prefix : L"" );
        lstrcatW( tmp, raw );
        if (tmp[lstrlenW( tmp ) - 1] != L'\\') lstrcatW( tmp, L"\\" );
        lstrcpyW( prefix, tmp );
    }

    ensure_cs();
    EnterCriticalSection( &subtree_cs );
    for (i = 0; i < subtree_count; i++)
    {
        if (!lstrcmpiW( subtree_maps[i]->prefix, prefix ))
        {
            *out = &subtree_maps[i]->vt_map;
            incr( &subtree_maps[i]->ref );
            LeaveCriticalSection( &subtree_cs );
            return S_OK;
        }
    }
    if (subtree_count < MAX_SUBTREES)
    {
        struct res_map *sub = HeapAlloc( GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*sub) );
        if (sub)
        {
            sub->vt_map = res_map_vtbl;
            sub->ref = 2;
            sub->prefix = lstrdupW( prefix );
            subtree_maps[subtree_count++] = sub;
            *out = &sub->vt_map;
            LeaveCriticalSection( &subtree_cs );
            return S_OK;
        }
    }
    LeaveCriticalSection( &subtree_cs );
    *out = NULL;
    return E_FAIL;
}
static const void *res_map_vtbl[] =
{
    map_QueryInterface, map_AddRef, map_Release, map_GetIids, map_GetRuntimeClassName, map_GetTrustLevel,
    map_Uri, map_GetValue, map_GetValueForContext, map_GetSubtree,
};

/* ------------------------------------------------------------------ */
/* NamedResource: [II 5][Uri 6][Candidates 7][Resolve 8][ResolveForContext 9][ResolveAll 10][ResolveAllForContext 11] */
/* ------------------------------------------------------------------ */

struct named_resource
{
    const void *vt_named;
    LONG ref;
    struct res_candidate *cand;
};
static inline struct named_resource *named_from( void *iface ) { return (struct named_resource *)iface; }
static HRESULT WINAPI named_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct named_resource *impl = named_from( iface );
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_INamedResource_g ))
    {
        *out = &impl->vt_named;
        incr( &impl->ref );
        return S_OK;
    }
    FIXME( "%s not implemented (ResourceMapSubtree, NamedResource2)\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI named_AddRef( void *iface ) { return incr( &named_from( iface )->ref ); }
static ULONG WINAPI named_Release( void *iface )
{
    struct named_resource *impl = named_from( iface );
    LONG ref = decr( &impl->ref );
    if (!ref && impl->cand) decr( &impl->cand->ref );
    return ref;
}
static HRESULT WINAPI named_GetIids( void *iface, ULONG *c, IID **i ) { return simple_get_iids( iface, c, i ); }
static HRESULT WINAPI named_GetRuntimeClassName( void *iface, HSTRING *n ) { return className( RTCLASS_NAMEDRESOURCE, n ); }
static HRESULT WINAPI named_GetTrustLevel( void *iface, INT32 *t ) { return get_trust_base( iface, t ); }
static HRESULT WINAPI named_Uri( void *iface, HSTRING *uri )
{
    WCHAR buf[2200];
    lstrcpyW( buf, L"ms-appx:///" );
    if (named_from( iface )->cand && named_from( iface )->cand->full_name) lstrcatW( buf, named_from( iface )->cand->full_name );
    return WindowsCreateString( buf, (UINT32)lstrlenW( buf ), uri );
}
static HRESULT WINAPI named_Candidates( void *iface, void **out )
{
    struct named_resource *impl = named_from( iface );
    struct iface_vector *vec;
    void **items;

    *out = NULL;
    items = HeapAlloc( GetProcessHeap(), 0, sizeof(*items) );
    vec = HeapAlloc( GetProcessHeap(), 0, sizeof(*vec) );
    if (!items || !vec) { HeapFree( GetProcessHeap(), 0, items ); HeapFree( GetProcessHeap(), 0, vec ); return E_OUTOFMEMORY; }
    incr( &impl->cand->ref );
    items[0] = &impl->cand->vt_cand;
    vec->vt_vv = iface_vector_vtbl;
    vec->ref = 1;
    vec->items = items;
    vec->count = 1;
    vec->elem = ELEM_RESCANDIDATE;
    *out = &vec->vt_vv;
    return S_OK;
}
static HRESULT WINAPI named_Resolve( void *iface, void **out )
{
    struct named_resource *impl = named_from( iface );
    *out = &impl->cand->vt_cand;
    incr( &impl->cand->ref );
    return S_OK;
}
static HRESULT WINAPI named_ResolveForContext( void *iface, void *context, void **out )
{
    return named_Resolve( iface, out );
}
static HRESULT WINAPI named_ResolveAll( void *iface, void **out )
{
    return named_Candidates( iface, out );
}
static HRESULT WINAPI named_ResolveAllForContext( void *iface, void *context, void **out )
{
    return named_Candidates( iface, out );
}
static const void *named_resource_vtbl[] =
{
    named_QueryInterface, named_AddRef, named_Release, named_GetIids, named_GetRuntimeClassName, named_GetTrustLevel,
    named_Uri, named_Candidates, named_Resolve, named_ResolveForContext, named_ResolveAll, named_ResolveAllForContext,
};

/* ------------------------------------------------------------------ */
/* ResourceContext: [II 5][QualifierValues 6][Reset 7][ResetQualifierValues 8]
 *                  [OverrideToMatch 9][Clone 10][Languages 11][SetLanguages 12]  */
/* ------------------------------------------------------------------ */

struct res_context
{
    const void *vt_ctx;
    const void *vt_named;     /* IResourceContextNamed (unverified) */
    LONG ref;
    struct map_view qualifiers;
    struct str_vector languages;
    HSTRING lang_items[1];
};
static inline struct res_context *ctx_from( void *iface ) { return (struct res_context *)iface; }
static inline struct res_context *ctx_named_from( void *iface ) { return (struct res_context *)((BYTE *)iface - sizeof(void *)); }

static HRESULT WINAPI ctx_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct res_context *impl = ctx_from( iface );
    TRACE( "iid %s\n", debugstr_guid( iid ) );
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IResourceContext_g ))
    {
        *out = &impl->vt_ctx;
        incr( &impl->ref );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IResourceContextNamed_g ))
    {
        *out = &impl->vt_named;
        incr( &impl->ref );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IResourceContext_v81_g ))
    {
        *out = &impl->vt_ctx;
        incr( &impl->ref );
        return S_OK;
    }
    FIXME( "%s not implemented\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI ctx_AddRef( void *iface ) { return incr( &ctx_from( iface )->ref ); }
static ULONG WINAPI ctx_Release( void *iface ) { return decr( &ctx_from( iface )->ref ); }
static HRESULT WINAPI ctx_GetIids( void *iface, ULONG *c, IID **i ) { return simple_get_iids( iface, c, i ); }
static HRESULT WINAPI ctx_GetRuntimeClassName( void *iface, HSTRING *n ) { return className( RTCLASS_RESOURCECONTEXT, n ); }
static HRESULT WINAPI ctx_GetTrustLevel( void *iface, INT32 *t ) { return get_trust_base( iface, t ); }
static HRESULT WINAPI ctx_QualifierValues( void *iface, void **out )
{
    struct res_context *impl = ctx_from( iface );
    *out = &impl->qualifiers.vt_mv;
    incr( &impl->qualifiers.ref );
    return S_OK;
}
static HRESULT WINAPI ctx_Reset( void *iface ) { return S_OK; }
static HRESULT WINAPI ctx_ResetQualifierValues( void *iface, void *names ) { return S_OK; }
static HRESULT WINAPI ctx_OverrideToMatch( void *iface, void *qualifiers ) { return S_OK; }
static HRESULT WINAPI ctx_Clone( void *iface, void **out );
static HRESULT WINAPI ctx_Languages( void *iface, void **out )
{
    struct res_context *impl = ctx_from( iface );
    *out = &impl->languages.vt_vv;
    incr( &impl->languages.ref );
    return S_OK;
}
static HRESULT WINAPI ctx_SetLanguages( void *iface, void *languages )
{
    TRACE( "stub\n" );
    return S_OK;
}
static const void *res_context_vtbl[] =
{
    ctx_QueryInterface, ctx_AddRef, ctx_Release, ctx_GetIids, ctx_GetRuntimeClassName, ctx_GetTrustLevel,
    ctx_QualifierValues, ctx_Reset, ctx_ResetQualifierValues, ctx_OverrideToMatch,
    ctx_Clone, ctx_Languages, ctx_SetLanguages,
};
/* IResourceContextNamed (unverified): [II 5][get_PackageResourceContext 6][put_PackageResourceContext 7] */
static HRESULT WINAPI ctxn_QueryInterface( void *iface, REFIID iid, void **out ) { return ctx_QueryInterface( ctx_named_from( iface ), iid, out ); }
static ULONG WINAPI ctxn_AddRef( void *iface ) { return incr( &ctx_named_from( iface )->ref ); }
static ULONG WINAPI ctxn_Release( void *iface ) { return decr( &ctx_named_from( iface )->ref ); }
static HRESULT WINAPI ctxn_GetIids( void *iface, ULONG *c, IID **i ) { return simple_get_iids( iface, c, i ); }
static HRESULT WINAPI ctxn_GetRuntimeClassName( void *iface, HSTRING *n ) { return className( RTCLASS_RESOURCECONTEXT, n ); }
static HRESULT WINAPI ctxn_GetTrustLevel( void *iface, INT32 *t ) { return get_trust_base( iface, t ); }
static HRESULT WINAPI ctxn_GetPackageResourceContext( void *iface, void **out ) { return ctx_Clone( iface, out ); }
static HRESULT WINAPI ctxn_SetPackageResourceContext( void *iface, void *context ) { return S_OK; }
static const void *res_context_named_vtbl[] =
{
    ctxn_QueryInterface, ctxn_AddRef, ctxn_Release, ctxn_GetIids, ctxn_GetRuntimeClassName, ctxn_GetTrustLevel,
    ctxn_GetPackageResourceContext, ctxn_SetPackageResourceContext,
};

static HRESULT create_context( struct res_context **out )
{
    struct res_context *ctx = HeapAlloc( GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*ctx) );
    if (!ctx) return E_OUTOFMEMORY;
    ctx->vt_ctx = res_context_vtbl;
    ctx->vt_named = res_context_named_vtbl;
    ctx->ref = 1;
    ctx->qualifiers.vt_mv = map_view_vtbl;
    ctx->qualifiers.ref = 1;
    ctx->qualifiers.pairs = default_qualifiers;
    ctx->qualifiers.count = ARRAYSIZE( default_qualifiers );
    ctx->languages.vt_vv = str_vector_vtbl;
    ctx->languages.ref = 1;
    ctx->languages.items = ctx->lang_items;
    ctx->languages.count = 1;
    WindowsCreateString( L"en-US", 5, &ctx->lang_items[0] );
    *out = ctx;
    return S_OK;
}

static HRESULT WINAPI ctx_Clone( void *iface, void **out )
{
    struct res_context *ctx;
    HRESULT hr = create_context( &ctx );
    if (SUCCEEDED( hr )) *out = &ctx->vt_ctx;
    return hr;
}

/* ------------------------------------------------------------------ */
/* factories                                                           */
/* ------------------------------------------------------------------ */

struct rc_factory
{
    const void *vt_primary;  /* IActivationFactory */
    const void *vt_statics;  /* IResourceContextStatics */
    const void *vt_statics2; /* IResourceContextStatics2 */
    const void *vt_statics3;
    const void *vt_statics4;
    const void *vt_v81;      /* IResourceContext (8.1 class identity): [II 5][Create 6][CreateWithCurrentView 7] */
};
static struct rc_factory *rcf_from( void *iface ) { return (struct rc_factory *)iface; }
static struct rc_factory *rcf_from_s( void *iface, size_t off ) { return (struct rc_factory *)((BYTE *)iface - off); }

static struct res_context *the_current_context;

static HRESULT get_current_context( void **out )
{
    if (!the_current_context)
    {
        HRESULT hr = create_context( &the_current_context );
        if (FAILED( hr )) return hr;
    }
    *out = &the_current_context->vt_ctx;
    incr( &the_current_context->ref );
    return S_OK;
}

static HRESULT WINAPI rcf_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct rc_factory *impl = rcf_from( iface );
    TRACE( "iid %s\n", debugstr_guid( iid ) );
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IActivationFactory_g_ ))
    {
        *out = &impl->vt_primary;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IAgileObject ))
    {
        *out = &impl->vt_primary;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IResourceContextStatics_g ))
    {
        *out = &impl->vt_statics;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IResourceContextStatics2_g ))
    {
        *out = &impl->vt_statics2;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IResourceContextStatics3_g ))
    {
        *out = &impl->vt_statics3;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IResourceContextStatics4_g ))
    {
        *out = &impl->vt_statics4;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IResourceContext_v81_g ))
    {
        *out = &impl->vt_v81;
        return S_OK;
    }
    FIXME( "%s not implemented\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI rcf_AddRef( void *iface ) { return 2; }
static ULONG WINAPI rcf_Release( void *iface ) { return 1; }
static HRESULT WINAPI rcf_GetIids( void *iface, ULONG *c, IID **i ) { return simple_get_iids( iface, c, i ); }
static HRESULT WINAPI rcf_GetRuntimeClassName( void *iface, HSTRING *n ) { return className( RTCLASS_RESOURCECONTEXT, n ); }
static HRESULT WINAPI rcf_GetTrustLevel( void *iface, INT32 *t ) { return get_trust_base( iface, t ); }
static HRESULT WINAPI rcf_ActivateInstance( void *iface, void **instance )
{
    struct res_context *ctx;
    HRESULT hr = create_context( &ctx );
    TRACE( "ResourceContext::ActivateInstance\n" );
    if (SUCCEEDED( hr )) *instance = &ctx->vt_ctx;
    return hr;
}
static const void *rc_factory_vtbl[] =
{
    rcf_QueryInterface, rcf_AddRef, rcf_Release, rcf_GetIids, rcf_GetRuntimeClassName, rcf_GetTrustLevel,
    rcf_ActivateInstance,
};
/* IResourceContextStatics: [II 5][CreateMatchingContext 6] */
static HRESULT WINAPI rcs_QueryInterface( void *iface, REFIID iid, void **out ) { return rcf_QueryInterface( rcf_from_s( iface, offsetof(struct rc_factory, vt_statics) ), iid, out ); }
static ULONG WINAPI rcs_AddRef( void *iface ) { return 2; }
static ULONG WINAPI rcs_Release( void *iface ) { return 1; }
static HRESULT WINAPI rcs_GetIids( void *iface, ULONG *c, IID **i ) { return simple_get_iids( iface, c, i ); }
static HRESULT WINAPI rcs_GetRuntimeClassName( void *iface, HSTRING *n ) { return className( RTCLASS_RESOURCECONTEXT, n ); }
static HRESULT WINAPI rcs_GetTrustLevel( void *iface, INT32 *t ) { return get_trust_base( iface, t ); }
static HRESULT WINAPI rcs_CreateMatchingContext( void *iface, void *qualifiers, void **out )
{
    return get_current_context( out );
}
static const void *rc_statics_vtbl[] =
{
    rcs_QueryInterface, rcs_AddRef, rcs_Release, rcs_GetIids, rcs_GetRuntimeClassName, rcs_GetTrustLevel,
    rcs_CreateMatchingContext,
};
/* IResourceContextStatics2: [II 5][GetForCurrentView 6][SetGlobalQualifierValue 7]
 *   [ResetGlobalQualifierValues 8][ResetGlobalQualifierValuesForSpecifiedQualifiers 9][GetForViewIndependentUse 10] */
static HRESULT WINAPI rc2_QueryInterface( void *iface, REFIID iid, void **out ) { return rcf_QueryInterface( rcf_from_s( iface, offsetof(struct rc_factory, vt_statics2) ), iid, out ); }
static ULONG WINAPI rc2_AddRef( void *iface ) { return 2; }
static ULONG WINAPI rc2_Release( void *iface ) { return 1; }
static HRESULT WINAPI rc2_GetIids( void *iface, ULONG *c, IID **i ) { return simple_get_iids( iface, c, i ); }
static HRESULT WINAPI rc2_GetRuntimeClassName( void *iface, HSTRING *n ) { return className( RTCLASS_RESOURCECONTEXT, n ); }
static HRESULT WINAPI rc2_GetTrustLevel( void *iface, INT32 *t ) { return get_trust_base( iface, t ); }
static HRESULT WINAPI rc2_GetForCurrentView( void *iface, void **out )
{
    TRACE( "GetForCurrentView\n" );
    return get_current_context( out );
}
static HRESULT WINAPI rc2_SetGlobalQualifierValue( void *iface, HSTRING value, HSTRING name ) { TRACE( "stub\n" ); return S_OK; }
static HRESULT WINAPI rc2_ResetGlobalQualifierValues( void *iface ) { return S_OK; }
static HRESULT WINAPI rc2_ResetGlobalQualifierValuesForSpecifiedQualifiers( void *iface, void *names ) { return S_OK; }
static HRESULT WINAPI rc2_GetForViewIndependentUse( void *iface, void **out )
{
    TRACE( "GetForViewIndependentUse\n" );
    return get_current_context( out );
}
static const void *rc_statics2_vtbl[] =
{
    rc2_QueryInterface, rc2_AddRef, rc2_Release, rc2_GetIids, rc2_GetRuntimeClassName, rc2_GetTrustLevel,
    rc2_GetForCurrentView, rc2_SetGlobalQualifierValue, rc2_ResetGlobalQualifierValues,
    rc2_ResetGlobalQualifierValuesForSpecifiedQualifiers, rc2_GetForViewIndependentUse,
};
static HRESULT WINAPI rc3_QueryInterface( void *iface, REFIID iid, void **out ) { return rcf_QueryInterface( rcf_from_s( iface, offsetof(struct rc_factory, vt_statics3) ), iid, out ); }
static ULONG WINAPI rc3_AddRef( void *iface ) { return 2; }
static ULONG WINAPI rc3_Release( void *iface ) { return 1; }
static HRESULT WINAPI rc3_GetIids( void *iface, ULONG *c, IID **i ) { return simple_get_iids( iface, c, i ); }
static HRESULT WINAPI rc3_GetRuntimeClassName( void *iface, HSTRING *n ) { return className( RTCLASS_RESOURCECONTEXT, n ); }
static HRESULT WINAPI rc3_GetTrustLevel( void *iface, INT32 *t ) { return get_trust_base( iface, t ); }
static HRESULT WINAPI rc3_SetGlobalQualifierValueWithPersistence( void *iface, HSTRING value, HSTRING name, INT32 persist ) { return S_OK; }
static const void *rc_statics3_vtbl[] =
{
    rc3_QueryInterface, rc3_AddRef, rc3_Release, rc3_GetIids, rc3_GetRuntimeClassName, rc3_GetTrustLevel,
    rc3_SetGlobalQualifierValueWithPersistence,
};
static HRESULT WINAPI rc4_QueryInterface( void *iface, REFIID iid, void **out ) { return rcf_QueryInterface( rcf_from_s( iface, offsetof(struct rc_factory, vt_statics4) ), iid, out ); }
static ULONG WINAPI rc4_AddRef( void *iface ) { return 2; }
static ULONG WINAPI rc4_Release( void *iface ) { return 1; }
static HRESULT WINAPI rc4_GetIids( void *iface, ULONG *c, IID **i ) { return simple_get_iids( iface, c, i ); }
static HRESULT WINAPI rc4_GetRuntimeClassName( void *iface, HSTRING *n ) { return className( RTCLASS_RESOURCECONTEXT, n ); }
static HRESULT WINAPI rc4_GetTrustLevel( void *iface, INT32 *t ) { return get_trust_base( iface, t ); }
static HRESULT WINAPI rc4_GetForUIContext( void *iface, void *context, void **out )
{
    return get_current_context( out );
}
static const void *rc_statics4_vtbl[] =
{
    rc4_QueryInterface, rc4_AddRef, rc4_Release, rc4_GetIids, rc4_GetRuntimeClassName, rc4_GetTrustLevel,
    rc4_GetForUIContext,
};
/* 8.1 class identity on the factory: Create / CreateWithCurrentView -> a context */
static HRESULT WINAPI rc81_QueryInterface( void *iface, REFIID iid, void **out ) { return rcf_QueryInterface( rcf_from_s( iface, offsetof(struct rc_factory, vt_v81) ), iid, out ); }
static ULONG WINAPI rc81_AddRef( void *iface ) { return 2; }
static ULONG WINAPI rc81_Release( void *iface ) { return 1; }
static HRESULT WINAPI rc81_GetIids( void *iface, ULONG *c, IID **i ) { return simple_get_iids( iface, c, i ); }
static HRESULT WINAPI rc81_GetRuntimeClassName( void *iface, HSTRING *n ) { return className( RTCLASS_RESOURCECONTEXT, n ); }
static HRESULT WINAPI rc81_GetTrustLevel( void *iface, INT32 *t ) { return get_trust_base( iface, t ); }
static HRESULT WINAPI rc81_Create( void *iface, void **out )
{
    struct res_context *ctx;
    HRESULT hr = create_context( &ctx );
    if (SUCCEEDED( hr )) *out = &ctx->vt_ctx;
    return hr;
}
static HRESULT WINAPI rc81_CreateWithCurrentView( void *iface, void *view, void **out )
{
    return rc81_Create( iface, out );
}
static const void *rc_v81_vtbl[] =
{
    rc81_QueryInterface, rc81_AddRef, rc81_Release, rc81_GetIids, rc81_GetRuntimeClassName, rc81_GetTrustLevel,
    rc81_Create, rc81_CreateWithCurrentView,
};
static struct rc_factory rc_factory_inst =
{
    rc_factory_vtbl, rc_statics_vtbl, rc_statics2_vtbl, rc_statics3_vtbl, rc_statics4_vtbl, rc_v81_vtbl,
};

/* ResourceManager */
struct res_manager
{
    const void *vt_rm;
    const void *vt_rm2;
    LONG ref;
};
static inline struct res_manager *rm_from( void *iface ) { return (struct res_manager *)iface; }
static inline struct res_manager *rm2_from( void *iface ) { return (struct res_manager *)((BYTE *)iface - sizeof(void *)); }

static struct res_map main_map = { NULL, 1, NULL };

static HRESULT WINAPI rm_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct res_manager *impl = rm_from( iface );
    TRACE( "iid %s\n", debugstr_guid( iid ) );
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IResourceManager_g ))
    {
        *out = &impl->vt_rm;
        incr( &impl->ref );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IResourceManager2_g ))
    {
        *out = &impl->vt_rm2;
        incr( &impl->ref );
        return S_OK;
    }
    FIXME( "%s not implemented\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI rm_AddRef( void *iface ) { return incr( &rm_from( iface )->ref ); }
static ULONG WINAPI rm_Release( void *iface ) { return decr( &rm_from( iface )->ref ); }
static HRESULT WINAPI rm_GetIids( void *iface, ULONG *c, IID **i ) { return simple_get_iids( iface, c, i ); }
static HRESULT WINAPI rm_GetRuntimeClassName( void *iface, HSTRING *n ) { return className( RTCLASS_RESOURCEMANAGER, n ); }
static HRESULT WINAPI rm_GetTrustLevel( void *iface, INT32 *t ) { return get_trust_base( iface, t ); }
static HRESULT WINAPI rm_MainResourceMap( void *iface, void **out )
{
    *out = &main_map.vt_map;
    incr( &main_map.ref );
    return S_OK;
}
static struct rm_map_entry all_maps_entries[1];
static struct rm_map_view all_maps_view = { NULL, 1, all_maps_entries, 1 };
static struct res_map *all_maps_map;
static HRESULT WINAPI rm_AllResourceMaps( void *iface, void **out )
{
    if (!all_maps_map)
    {
        all_maps_map = &main_map;
        all_maps_entries[0].key = (WCHAR *)L"MainPkg"; /* documented tier: package identity unavailable */
        all_maps_entries[0].map = &main_map.vt_map;
        all_maps_view.vt_mv = rm_map_view_vtbl;
    }
    *out = &all_maps_view.vt_mv;
    incr( &all_maps_view.ref );
    return S_OK;
}
static HRESULT WINAPI rm_DefaultContext( void *iface, void **out )
{
    return get_current_context( out );
}
static HRESULT WINAPI rm_LoadPriFiles( void *iface, void *files )
{
    TRACE( "stub\n" );
    return S_OK;
}
static HRESULT WINAPI rm_UnloadPriFiles( void *iface, void *files )
{
    TRACE( "stub\n" );
    return S_OK;
}
static const void *res_manager_vtbl[] =
{
    rm_QueryInterface, rm_AddRef, rm_Release, rm_GetIids, rm_GetRuntimeClassName, rm_GetTrustLevel,
    rm_MainResourceMap, rm_AllResourceMaps, rm_DefaultContext, rm_LoadPriFiles, rm_UnloadPriFiles,
};
static HRESULT WINAPI rm2_QueryInterface( void *iface, REFIID iid, void **out ) { return rm_QueryInterface( rm2_from( iface ), iid, out ); }
static ULONG WINAPI rm2_AddRef( void *iface ) { return incr( &rm2_from( iface )->ref ); }
static ULONG WINAPI rm2_Release( void *iface ) { return decr( &rm2_from( iface )->ref ); }
static HRESULT WINAPI rm2_GetIids( void *iface, ULONG *c, IID **i ) { return simple_get_iids( iface, c, i ); }
static HRESULT WINAPI rm2_GetRuntimeClassName( void *iface, HSTRING *n ) { return className( RTCLASS_RESOURCEMANAGER, n ); }
static HRESULT WINAPI rm2_GetTrustLevel( void *iface, INT32 *t ) { return get_trust_base( iface, t ); }
static HRESULT WINAPI rm2_GetAllNamedResourcesForPackage( void *iface, HSTRING pkgname, INT32 info, void **out )
{
    struct iface_vector *vec = HeapAlloc( GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*vec) );
    if (!vec) return E_OUTOFMEMORY;
    vec->vt_vv = iface_vector_vtbl;
    vec->ref = 1;
    vec->count = 0;
    vec->elem = ELEM_NAMEDRES;
    *out = &vec->vt_vv;
    return S_OK;
}
static HRESULT WINAPI rm2_GetAllSubtreesForPackage( void *iface, HSTRING pkgname, INT32 info, void **out )
{
    return rm2_GetAllNamedResourcesForPackage( iface, pkgname, info, out );
}
static const void *res_manager2_vtbl[] =
{
    rm2_QueryInterface, rm2_AddRef, rm2_Release, rm2_GetIids, rm2_GetRuntimeClassName, rm2_GetTrustLevel,
    rm2_GetAllNamedResourcesForPackage, rm2_GetAllSubtreesForPackage,
};
static struct res_manager res_manager_inst = { res_manager_vtbl, res_manager2_vtbl, 1 };

struct rm_factory
{
    const void *vt_primary;
    const void *vt_statics;
};
static struct rm_factory *rmf_from( void *iface ) { return (struct rm_factory *)iface; }
static struct rm_factory *rmf_from_s( void *iface, size_t off ) { return (struct rm_factory *)((BYTE *)iface - off); }

static HRESULT WINAPI rmf_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct rm_factory *impl = rmf_from( iface );
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IActivationFactory_g_ ) || IsEqualGUID( iid, &IID_IAgileObject ))
    {
        *out = &impl->vt_primary;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IResourceManagerStatics_g ))
    {
        *out = &impl->vt_statics;
        return S_OK;
    }
    FIXME( "%s not implemented\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI rmf_AddRef( void *iface ) { return 2; }
static ULONG WINAPI rmf_Release( void *iface ) { return 1; }
static HRESULT WINAPI rmf_GetIids( void *iface, ULONG *c, IID **i ) { return simple_get_iids( iface, c, i ); }
static HRESULT WINAPI rmf_GetRuntimeClassName( void *iface, HSTRING *n ) { return className( RTCLASS_RESOURCEMANAGER, n ); }
static HRESULT WINAPI rmf_GetTrustLevel( void *iface, INT32 *t ) { return get_trust_base( iface, t ); }
static HRESULT WINAPI rmf_ActivateInstance( void *iface, void **instance )
{
    *instance = &res_manager_inst.vt_rm;
    incr( &res_manager_inst.ref );
    return S_OK;
}
static const void *rm_factory_vtbl[] =
{
    rmf_QueryInterface, rmf_AddRef, rmf_Release, rmf_GetIids, rmf_GetRuntimeClassName, rmf_GetTrustLevel,
    rmf_ActivateInstance,
};
static HRESULT WINAPI rms_QueryInterface( void *iface, REFIID iid, void **out ) { return rmf_QueryInterface( rmf_from_s( iface, offsetof(struct rm_factory, vt_statics) ), iid, out ); }
static ULONG WINAPI rms_AddRef( void *iface ) { return 2; }
static ULONG WINAPI rms_Release( void *iface ) { return 1; }
static HRESULT WINAPI rms_GetIids( void *iface, ULONG *c, IID **i ) { return simple_get_iids( iface, c, i ); }
static HRESULT WINAPI rms_GetRuntimeClassName( void *iface, HSTRING *n ) { return className( RTCLASS_RESOURCEMANAGER, n ); }
static HRESULT WINAPI rms_GetTrustLevel( void *iface, INT32 *t ) { return get_trust_base( iface, t ); }
static HRESULT WINAPI rms_Current( void *iface, void **out )
{
    TRACE( "ResourceManager::Current\n" );
    *out = &res_manager_inst.vt_rm;
    incr( &res_manager_inst.ref );
    return S_OK;
}
static HRESULT WINAPI rms_IsResourceReference( void *iface, HSTRING resource, INT32 *result )
{
    const WCHAR *raw = WindowsGetStringRawBuffer( resource, NULL );
    *result = !!wcschr( raw, L'/' );
    return S_OK;
}
static const void *rm_statics_vtbl[] =
{
    rms_QueryInterface, rms_AddRef, rms_Release, rms_GetIids, rms_GetRuntimeClassName, rms_GetTrustLevel,
    rms_Current, rms_IsResourceReference,
};
static struct rm_factory rm_factory_inst = { rm_factory_vtbl, rm_statics_vtbl };

/* passive factories for non-activatable classes */
struct simple_factory
{
    const void *vt_primary;
    const WCHAR *class_name;
};
static struct simple_factory *sf_from( void *iface ) { return (struct simple_factory *)iface; }
static HRESULT WINAPI sf_QueryInterface( void *iface, REFIID iid, void **out )
{
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IActivationFactory_g_ ) || IsEqualGUID( iid, &IID_IAgileObject ))
    {
        *out = iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI sf_AddRef( void *iface ) { return 2; }
static ULONG WINAPI sf_Release( void *iface ) { return 1; }
static HRESULT WINAPI sf_GetIids( void *iface, ULONG *c, IID **i ) { return simple_get_iids( iface, c, i ); }
static HRESULT WINAPI sf_GetRuntimeClassName( void *iface, HSTRING *n ) { return className( sf_from( iface )->class_name, n ); }
static HRESULT WINAPI sf_GetTrustLevel( void *iface, INT32 *t ) { return get_trust_base( iface, t ); }
static HRESULT WINAPI sf_ActivateInstance( void *iface, void **instance )
{
    FIXME( "%s not activatable\n", debugstr_w( sf_from( iface )->class_name ) );
    *instance = NULL;
    return E_FAIL;
}
static const void *simple_factory_vtbl[] =
{
    sf_QueryInterface, sf_AddRef, sf_Release, sf_GetIids, sf_GetRuntimeClassName, sf_GetTrustLevel,
    sf_ActivateInstance,
};
static struct simple_factory map_factory_inst   = { simple_factory_vtbl, RTCLASS_RESOURCEMAP };
static struct simple_factory cand_factory_inst  = { simple_factory_vtbl, RTCLASS_RESOURCECANDIDATE };
static struct simple_factory named_factory_inst = { simple_factory_vtbl, RTCLASS_NAMEDRESOURCE };
static struct simple_factory qual_factory_inst  = { simple_factory_vtbl, RTCLASS_RESOURCEQUALIFIER };

/* Windows.System.MemoryManager: no-op activation stub */
struct memory_manager
{
    const void *vt_obj;
    LONG ref;
};
static inline struct memory_manager *mm_from( void *iface ) { return (struct memory_manager *)iface; }
static HRESULT WINAPI mm_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct memory_manager *impl = mm_from( iface );
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ))
    {
        *out = &impl->vt_obj;
        incr( &impl->ref );
        return S_OK;
    }
    FIXME( "MemoryManager interface %s not implemented\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI mm_AddRef( void *iface ) { return incr( &mm_from( iface )->ref ); }
static ULONG WINAPI mm_Release( void *iface ) { return decr( &mm_from( iface )->ref ); }
static HRESULT WINAPI mm_GetIids( void *iface, ULONG *c, IID **i ) { return simple_get_iids( iface, c, i ); }
static HRESULT WINAPI mm_GetRuntimeClassName( void *iface, HSTRING *n ) { return className( RTCLASS_MEMORYMANAGER, n ); }
static HRESULT WINAPI mm_GetTrustLevel( void *iface, INT32 *t ) { return get_trust_base( iface, t ); }
static const void *memory_manager_vtbl[] =
{
    mm_QueryInterface, mm_AddRef, mm_Release, mm_GetIids, mm_GetRuntimeClassName, mm_GetTrustLevel,
};
static struct memory_manager memory_manager_inst = { memory_manager_vtbl, 1 };

/* factory ActivateInstance for MemoryManager returns the stub object */
static HRESULT WINAPI mmf_ActivateInstance( void *iface, void **instance );
static const void *mm_factory_vtbl2[] =
{
    sf_QueryInterface, sf_AddRef, sf_Release, sf_GetIids, sf_GetRuntimeClassName, sf_GetTrustLevel,
    mmf_ActivateInstance,
};
static struct simple_factory mm_factory = { mm_factory_vtbl2, RTCLASS_MEMORYMANAGER };
static HRESULT WINAPI mmf_ActivateInstance( void *iface, void **instance )
{
    TRACE( "MemoryManager::ActivateInstance stub\n" );
    *instance = &memory_manager_inst.vt_obj;
    incr( &memory_manager_inst.ref );
    return S_OK;
}

/* ------------------------------------------------------------------ */
/* exports                                                             */
/* ------------------------------------------------------------------ */

HRESULT WINAPI DllCanUnloadNow(void)
{
    return S_FALSE;
}

HRESULT WINAPI DllGetClassObject(REFCLSID clsid, REFIID riid, void **out)
{
    FIXME("clsid %s, riid %s, out %p stub!\n", debugstr_guid(clsid), debugstr_guid(riid), out);
    return CLASS_E_CLASSNOTAVAILABLE;
}

HRESULT WINAPI DllGetActivationFactory(HSTRING classid, void **factory)
{
    const WCHAR *buffer = WindowsGetStringRawBuffer( classid, NULL );

    TRACE("class %s, factory %p.\n", debugstr_hstring(classid), factory);

    if (!factory) return E_POINTER;
    *factory = NULL;

    if (!wcscmp( buffer, RTCLASS_RESOURCECONTEXT ))
    {
        main_map.vt_map = res_map_vtbl;
        *factory = (void *)&rc_factory_inst.vt_primary;
        return S_OK;
    }
    if (!wcscmp( buffer, RTCLASS_RESOURCEMANAGER ))
    {
        main_map.vt_map = res_map_vtbl;
        *factory = (void *)&rm_factory_inst.vt_primary;
        return S_OK;
    }
    if (!wcscmp( buffer, RTCLASS_RESOURCEMAP ))
    {
        main_map.vt_map = res_map_vtbl;
        *factory = (void *)&map_factory_inst.vt_primary;
        return S_OK;
    }
    if (!wcscmp( buffer, RTCLASS_RESOURCECANDIDATE ))
    {
        *factory = (void *)&cand_factory_inst.vt_primary;
        return S_OK;
    }
    if (!wcscmp( buffer, RTCLASS_NAMEDRESOURCE ))
    {
        *factory = (void *)&named_factory_inst.vt_primary;
        return S_OK;
    }
    if (!wcscmp( buffer, RTCLASS_RESOURCEQUALIFIER ))
    {
        *factory = (void *)&qual_factory_inst.vt_primary;
        return S_OK;
    }
    if (!wcscmp( buffer, RTCLASS_MEMORYMANAGER ))
    {
        /* Measured on Calculator (wbappgate run): .NET Native mrt100_app.dll
         * DllMain queries Windows.System.MemoryManager with
         * {6eee351f-6d62-423f-9479-b01f9c9f7669}; when the class resolves to
         * any factory other than "not registered" (e.g. a stub answering
         * E_NOINTERFACE), mrt100's managed fallback path is skipped and
         * DllMain throws -> STATUS_DLL_INIT_FAILED (c0000142) aborts process
         * startup.  Keep the tolerated REGDB_E_CLASSNOTREG code (the
         * one-shot "Failed to find library" warning) to let the app
         * initialize; the no-op stub objects below stay available for callers
         * that activate the class explicitly. */
        FIXME( "MemoryManager activation left as REGDB_E_CLASSNOTREG: mrt100_app DllMain requires the not-registered failure code\n" );
        *factory = NULL;
        return REGDB_E_CLASSNOTREG;
    }

    FIXME("class %s not supported\n", debugstr_hstring(classid));
    return E_NOINTERFACE;
}
