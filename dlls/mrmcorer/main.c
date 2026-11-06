/*
 * MRT Core (Microsoft.Windows.ApplicationModel.Resources) builtin provider.
 *
 * Serves the WinRT classes the .NET Native / XAML plane activates against
 * mrmcorer.dll: ResourceManager (activatable, statics CreateResourceManager),
 * ResourceMap, NamedResource, ResourceCandidate, ResourceContext.
 *
 * Resource content comes from the app's own resources.pri via the minimal
 * reader in pri.c (shared with windows.applicationmodel.resources.core).
 */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define COBJMACROS
#include <initguid.h>
#include "windef.h"
#include "winbase.h"
#include "winstring.h"
#include "winnls.h"
#include "wine/debug.h"

#include "mrmcorer.h"
#include "pri.h"

#define IID_IResourceManager2 IID_IResourceManager2_g
#define IID_IResourceManagerStatics IID_IResourceManagerStatics_g
#define IID_IResourceMap2 IID_IResourceMap2_g
static HRESULT mwr_nr_pair_create( HSTRING key, INamedResource *value, IKeyValuePair_HSTRING_NAMEDRESOURCE_ **out );

WINE_DEFAULT_DEBUG_CHANNEL(mrmcorer);

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
    GetModuleFileNameW( GetModuleHandleW( L"mrmcorer.dll" ), path, ARRAYSIZE( path ));
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
/* iids                                                               */
/* ------------------------------------------------------------------ */

/* vtable-companion interfaces (accepted, same vtable as their v1 base) */
typedef int _x;
static const GUID IID_IResourceManager2_g       = {0x7ec10160,0xa154,0x5c42,{0x82,0x68,0x30,0xe3,0x06,0xb1,0xf5,0x85}};
static const GUID IID_IResourceManagerStatics_g = {0xacf18f46,0x8ad6,0x5b45,{0x53,0xa5,0xc4,0xac,0x2d,0xc4,0xe4,0x90}};
static const GUID IID_IResourceMap2_g           = {0x778b2377,0x80ab,0x647d,{0xe7,0x26,0x5b,0x83,0x0f,0x15,0x94,0x1e}};

/* forward declarations of the shared faces */
struct mwr_string_vector;
struct mwr_string_map;
struct mwr_candidate_states;
extern const struct IVectorView_HSTRINGVtbl mwr_string_vector_vtbl;
extern const struct IMapView_HSTRING_HSTRINGVtbl mwr_string_map_vtbl;
static struct mwr_string_vector *mwr_string_vector_create( UINT32 count );
static void mwr_string_vector_set( struct mwr_string_vector *v, UINT32 index, const WCHAR *s );
static struct mwr_string_map *mwr_empty_qualifier_map( void );
static IVectorView_ResourceCandidateState_ *mwr_candidate_states( void );
static HRESULT mwr_pair_create( HSTRING key, IUnknown *value, IKeyValuePair_HSTRING_RESOURCE_ **out );

static HRESULT WINAPI svv_GetIids_stub( IInspectable *iface, ULONG *count, IID **iids )
{
    if (!count || !iids) return E_POINTER;
    *count = 0;
    *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI svv_GetRuntimeClassName_stub( IInspectable *iface, HSTRING *name )
{
    if (!name) return E_POINTER;
    return WindowsCreateString( L"Windows.Foundation.Collections.IVectorView`1<System.String>",
                                55, name );
}

static HRESULT WINAPI svv_GetTrustLevel_stub( IInspectable *iface, enum TrustLevel *level )
{
    *level = FullTrust;
    return S_OK;
}

/* ------------------------------------------------------------------ */
/* helpers                                                            */
/* ------------------------------------------------------------------ */

static inline LONG incr( LONG *ref ) { return InterlockedIncrement( ref ); }
static inline LONG decr( LONG *ref ) { return InterlockedDecrement( ref ); }

static WCHAR *dupW( const WCHAR *s )
{
    SIZE_T bytes = (lstrlenW( s ) + 1) * sizeof(*s);
    WCHAR *out = HeapAlloc( GetProcessHeap(), 0, bytes );
    if (out) memcpy( out, s, bytes );
    return out;
}

static HRESULT hstring_from( const WCHAR *s, HSTRING *out )
{
    if (!out) return E_POINTER;
    return WindowsCreateString( s, lstrlenW( s ), out );
}

static HRESULT simple_get_iids( void *iface, ULONG *count, IID **iids )
{
    if (!count || !iids) return E_POINTER;
    *count = 0;
    *iids = NULL;
    return S_OK;
}

static HRESULT get_class_name( const WCHAR *name, HSTRING *out )
{
    return hstring_from( name, out );
}


struct mwr_string_vector
{
    IVectorView_HSTRING_ IVectorView_HSTRING__iface;
    LONG ref;
    UINT32 count;
    UINT32 cap;
    HSTRING strs[1];
};

struct mwr_string_map
{
    IMapView_HSTRING_HSTRING_ IMapView_HSTRING_HSTRING__iface;
    LONG ref;
    UINT32 count;
    HSTRING *keys;
    HSTRING *vals;
};

static struct mwr_candidate_states_obj
{
    IVectorView_ResourceCandidateState_ IVectorView_iface;
    LONG ref;
} *the_states;

const struct IVectorView_ResourceCandidateStateVtbl mwr_candidate_states_vtbl;

static IVectorView_ResourceCandidateState_ *mwr_candidate_states( void )
{
    if (!the_states)
    {
        struct mwr_candidate_states_obj *s = HeapAlloc( GetProcessHeap(), 0, sizeof(*s) );
        if (!s) return NULL;
        s->IVectorView_iface.lpVtbl = &mwr_candidate_states_vtbl;
        s->ref = 1;
        the_states = s;
    }
    return &the_states->IVectorView_iface;
}

/* ------------------------------------------------------------------ */
/* ResourceContext (Microsoft.Windows.ApplicationModel.Resources)     */
/*   II: QI AR R GetIids GetRuntimeClassName GetTrustLevel            */
/*   6 Languages, 7 QualifierValues, 8 GetAllQualifierNames           */
/* ------------------------------------------------------------------ */

struct mwr_context
{
    IResourceContext IResourceContext_iface;
    LONG ref;
};

static struct mwr_context *context_from_iface( IResourceContext *iface )
{
    return CONTAINING_RECORD( iface, struct mwr_context, IResourceContext_iface );
}

static HRESULT WINAPI context_QueryInterface( IResourceContext *iface, REFIID iid, void **obj )
{
    struct mwr_context *ctx = context_from_iface( iface );
    TRACE( "QueryInterface %p %s\n", iface, debugstr_guid( iid ) );
    *obj = NULL;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable )
            || IsEqualGUID( iid, &IID_IResourceContext ))
    {
        *obj = iface;
    }
    else
    {
        TRACE( "interface %s not implemented\n", debugstr_guid( iid ) );
        return E_NOINTERFACE;
    }
    incr( &ctx->ref );
    return S_OK;
}

static ULONG WINAPI context_AddRef( IResourceContext *iface ) { return incr( &context_from_iface( iface )->ref ); }
static ULONG WINAPI context_Release( IResourceContext *iface )
{
    struct mwr_context *ctx = context_from_iface( iface );
    LONG ref = decr( &ctx->ref );
    if (!ref) HeapFree( GetProcessHeap(), 0, ctx );
    return ref;
}

static HRESULT WINAPI context_GetIids( IResourceContext *iface, ULONG *count, IID **iids )
{
    return simple_get_iids( iface, count, iids );
}

static HRESULT WINAPI context_GetRuntimeClassName( IResourceContext *iface, HSTRING *name )
{
    return get_class_name( L"Microsoft.Windows.ApplicationModel.Resources.ResourceContext", name );
}

static HRESULT WINAPI context_GetTrustLevel( IResourceContext *iface, enum TrustLevel *level )
{
    *level = FullTrust;
    return S_OK;
}

static HRESULT WINAPI context_get_Languages( IResourceContext *iface, IVectorView_HSTRING_ **value )
{
    static const WCHAR *langs[] = { L"en-US", L"en" };
    struct mwr_string_vector *vec;
    UINT32 i;

    TRACE( "get_Languages\n" );
    if (!value) return E_POINTER;
    vec = mwr_string_vector_create( ARRAYSIZE( langs ) );
    if (!vec) return E_OUTOFMEMORY;
    for (i = 0; i < ARRAYSIZE( langs ); i++) mwr_string_vector_set( vec, i, langs[i] );
    *value = &vec->IVectorView_HSTRING__iface;
    return S_OK;
}

static HRESULT WINAPI context_get_QualifierValues( IResourceContext *iface, IMapView_HSTRING_HSTRING_ **value )
{
    TRACE( "get_QualifierValues\n" );
    if (!value) return E_POINTER;
    { struct mwr_string_map *qm = mwr_empty_qualifier_map(); if (!qm) return E_OUTOFMEMORY; *value = &qm->IMapView_HSTRING_HSTRING__iface; }
    return S_OK;
}

static HRESULT WINAPI context_GetAllQualifierNames( IResourceContext *iface, IVectorView_HSTRING_ **value )
{
    TRACE( "GetAllQualifierNames\n" );
    return context_get_Languages( iface, value );
}

static const struct IResourceContextVtbl context_vtbl =
{
    context_QueryInterface,
    context_AddRef,
    context_Release,
    context_GetIids,
    context_GetRuntimeClassName,
    context_GetTrustLevel,
    context_get_Languages,
    context_get_QualifierValues,
    context_GetAllQualifierNames,
};

static struct mwr_context *context_create( void )
{
    struct mwr_context *ctx = HeapAlloc( GetProcessHeap(), 0, sizeof(*ctx) );
    if (!ctx) return NULL;
    ctx->IResourceContext_iface.lpVtbl = &context_vtbl;
    ctx->ref = 1;
    return ctx;
}

/* ------------------------------------------------------------------ */
/* string vector view + qualifier map faces (defined in mrmcorer.h)   */
/* ------------------------------------------------------------------ */

static struct mwr_string_vector *mwr_string_vector_create( UINT32 count )
{
    struct mwr_string_vector *v = HeapAlloc( GetProcessHeap(), 0, sizeof(*v) + count * sizeof(HSTRING) );
    if (!v) return NULL;
    v->IVectorView_HSTRING__iface.lpVtbl = &mwr_string_vector_vtbl;
    v->ref = 1;
    v->count = 0;
    v->cap = count;
    return v;
}

static void mwr_string_vector_set( struct mwr_string_vector *v, UINT32 index, const WCHAR *s )
{
    WindowsCreateString( s, lstrlenW( s ), &v->strs[index] );
    if (index + 1 > v->count) v->count = index + 1;
}

static HRESULT WINAPI svv_QueryInterface( IVectorView_HSTRING_ *iface, REFIID iid, void **obj )
{
    TRACE( "svv_QueryInterface %s\n", debugstr_guid( iid ) );
    *obj = NULL;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable )
            || IsEqualGUID( iid, &IID_IVectorView_HSTRING_ ))
        *obj = iface;
    else
        return E_NOINTERFACE;
    incr( &CONTAINING_RECORD( iface, struct mwr_string_vector, IVectorView_HSTRING__iface )->ref );
    return S_OK;
}

static ULONG WINAPI svv_AddRef( IVectorView_HSTRING_ *iface )
{
    return incr( &CONTAINING_RECORD( iface, struct mwr_string_vector, IVectorView_HSTRING__iface )->ref );
}

static ULONG WINAPI svv_Release( IVectorView_HSTRING_ *iface )
{
    struct mwr_string_vector *v = CONTAINING_RECORD( iface, struct mwr_string_vector, IVectorView_HSTRING__iface );
    LONG ref = decr( &v->ref );
    if (!ref)
    {
        UINT32 i;
        for (i = 0; i < v->count; i++) WindowsDeleteString( v->strs[i] );
        HeapFree( GetProcessHeap(), 0, v );
    }
    return ref;
}

static HRESULT WINAPI svv_GetAt( IVectorView_HSTRING_ *iface, UINT32 index, HSTRING *item )
{
    struct mwr_string_vector *v = CONTAINING_RECORD( iface, struct mwr_string_vector, IVectorView_HSTRING__iface );
    TRACE( "svv_GetAt %u\n", index );
    if (!item) return E_POINTER;
    if (index >= v->count) return E_INVALIDARG;
    *item = v->strs[index];
    return S_OK;
}

static HRESULT WINAPI svv_get_Size( IVectorView_HSTRING_ *iface, UINT32 *size )
{
    struct mwr_string_vector *v = CONTAINING_RECORD( iface, struct mwr_string_vector, IVectorView_HSTRING__iface );
    TRACE( "svv_get_Size -> %u\n", v->count );
    if (!size) return E_POINTER;
    *size = v->count;
    return S_OK;
}

static HRESULT WINAPI svv_IndexOf( IVectorView_HSTRING_ *iface, HSTRING item, UINT32 *index, boolean *found )
{
    struct mwr_string_vector *v = CONTAINING_RECORD( iface, struct mwr_string_vector, IVectorView_HSTRING__iface );
    UINT32 i;
    TRACE( "svv_IndexOf\n" );
    if (!index || !found) return E_POINTER;
    for (i = 0; i < v->count; i++)
    {
        INT32 c;
        if (WindowsCompareStringOrdinal( v->strs[i], item, &c ) == S_OK && !c)
        {
            *index = i;
            *found = true;
            return S_OK;
        }
    }
    *index = 0;
    *found = false;
    return S_OK;
}

const struct IVectorView_HSTRINGVtbl mwr_string_vector_vtbl =
{
    svv_QueryInterface,
    svv_AddRef,
    svv_Release,
    svv_GetIids_stub,
    svv_GetRuntimeClassName_stub,
    svv_GetTrustLevel_stub,
    svv_GetAt,
    svv_get_Size,
    svv_IndexOf,
};

static struct mwr_string_map *the_qualifier_map;

static struct mwr_string_map *mwr_empty_qualifier_map( void )
{
    if (!the_qualifier_map)
    {
        struct mwr_string_map *m = HeapAlloc( GetProcessHeap(), 0, sizeof(*m) );
        if (!m) return NULL;
        m->IMapView_HSTRING_HSTRING__iface.lpVtbl = &mwr_string_map_vtbl;
        m->ref = 1;
        m->count = 0;
        the_qualifier_map = m;
    }
    return the_qualifier_map;
}

static HRESULT WINAPI smv_QueryInterface( IMapView_HSTRING_HSTRING_ *iface, REFIID iid, void **obj )
{
    TRACE( "smv_QueryInterface %s\n", debugstr_guid( iid ) );
    *obj = NULL;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable )
            || IsEqualGUID( iid, &IID_IMapView_HSTRING_HSTRING_ ))
        *obj = iface;
    else
        return E_NOINTERFACE;
    incr( &CONTAINING_RECORD( iface, struct mwr_string_map, IMapView_HSTRING_HSTRING__iface )->ref );
    return S_OK;
}

static ULONG WINAPI smv_AddRef( IMapView_HSTRING_HSTRING_ *iface )
{
    return incr( &CONTAINING_RECORD( iface, struct mwr_string_map, IMapView_HSTRING_HSTRING__iface )->ref );
}

static ULONG WINAPI smv_Release( IMapView_HSTRING_HSTRING_ *iface )
{
    struct mwr_string_map *m = CONTAINING_RECORD( iface, struct mwr_string_map, IMapView_HSTRING_HSTRING__iface );
    LONG ref = decr( &m->ref );
    if (!ref && m != the_qualifier_map) HeapFree( GetProcessHeap(), 0, m );
    return ref;
}

static HRESULT WINAPI smv_GetAt( IMapView_HSTRING_HSTRING_ *iface, UINT32 index,
                                 IKeyValuePair_HSTRING_HSTRING_ **item )
{
    struct mwr_string_map *m = CONTAINING_RECORD( iface, struct mwr_string_map, IMapView_HSTRING_HSTRING__iface );
    TRACE( "smv_GetAt %u\n", index );
    if (!item) return E_POINTER;
    if (index >= m->count) return E_INVALIDARG;
    return mwr_pair_create( m->keys[index], m->vals[index], item );
}

static HRESULT WINAPI smv_get_Size( IMapView_HSTRING_HSTRING_ *iface, UINT32 *size )
{
    struct mwr_string_map *m = CONTAINING_RECORD( iface, struct mwr_string_map, IMapView_HSTRING_HSTRING__iface );
    TRACE( "smv_get_Size -> %u\n", m->count );
    if (!size) return E_POINTER;
    *size = m->count;
    return S_OK;
}

static HRESULT WINAPI smv_HasKey( IMapView_HSTRING_HSTRING_ *iface, HSTRING key, boolean *found )
{
    struct mwr_string_map *m = CONTAINING_RECORD( iface, struct mwr_string_map, IMapView_HSTRING_HSTRING__iface );
    UINT32 i;
    TRACE( "smv_HasKey\n" );
    if (!found) return E_POINTER;
    *found = false;
    for (i = 0; i < m->count; i++)
    {
        INT32 c;
        if (WindowsCompareStringOrdinal( m->keys[i], key, &c ) == S_OK && !c) { *found = true; break; }
    }
    return S_OK;
}

static HRESULT WINAPI smv_Lookup( IMapView_HSTRING_HSTRING_ *iface, HSTRING key, HSTRING *value )
{
    struct mwr_string_map *m = CONTAINING_RECORD( iface, struct mwr_string_map, IMapView_HSTRING_HSTRING__iface );
    UINT32 i;
    TRACE( "smv_Lookup\n" );
    if (!value) return E_POINTER;
    for (i = 0; i < m->count; i++)
    {
        INT32 c;
        if (WindowsCompareStringOrdinal( m->keys[i], key, &c ) == S_OK && !c)
        {
            *value = m->vals[i];
            return S_OK;
        }
    }
    return hstring_from( L"", value );
}

const struct IMapView_HSTRING_HSTRINGVtbl mwr_string_map_vtbl =
{
    smv_QueryInterface,
    smv_AddRef,
    smv_Release,
    svv_GetIids_stub,
    svv_GetRuntimeClassName_stub,
    svv_GetTrustLevel_stub,
    smv_GetAt,
    smv_get_Size,
    smv_HasKey,
    smv_Lookup,
};

/* key/value pair face for TopLevelResourceKeys / GetValueByIndex maps */
struct mwr_pair
{
    IKeyValuePair_HSTRING_RESOURCE_ IKeyValuePair_iface;
    LONG ref;
    HSTRING key;
    IUnknown *value;
};

static HRESULT WINAPI pair_QueryInterface( IKeyValuePair_HSTRING_RESOURCE_ *iface, REFIID iid, void **obj )
{
    *obj = NULL;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable )
            || IsEqualGUID( iid, &IID_IKeyValuePair_HSTRING_RESOURCE_ ))
        *obj = iface;
    else
        return E_NOINTERFACE;
    incr( &CONTAINING_RECORD( iface, struct mwr_pair, IKeyValuePair_iface )->ref );
    return S_OK;
}

static ULONG WINAPI pair_AddRef( IKeyValuePair_HSTRING_RESOURCE_ *iface )
{
    return incr( &CONTAINING_RECORD( iface, struct mwr_pair, IKeyValuePair_iface )->ref );
}

static ULONG WINAPI pair_Release( IKeyValuePair_HSTRING_RESOURCE_ *iface )
{
    struct mwr_pair *p = CONTAINING_RECORD( iface, struct mwr_pair, IKeyValuePair_iface );
    LONG ref = decr( &p->ref );
    if (!ref)
    {
        WindowsDeleteString( p->key );
        if (p->value) IUnknown_Release( p->value );
        HeapFree( GetProcessHeap(), 0, p );
    }
    return ref;
}

static HRESULT WINAPI pair_GetIids( IKeyValuePair_HSTRING_RESOURCE_ *iface, ULONG *count, IID **iids )
{
    return simple_get_iids( iface, count, iids );
}

static HRESULT WINAPI pair_GetRuntimeClassName( IKeyValuePair_HSTRING_RESOURCE_ *iface, HSTRING *name )
{
    return hstring_from( L"Windows.Foundation.Collections.IKeyValuePair`2<System.String,System.Object>", name );
}

static HRESULT WINAPI pair_GetTrustLevel( IKeyValuePair_HSTRING_RESOURCE_ *iface, enum TrustLevel *level )
{
    *level = FullTrust;
    return S_OK;
}

static HRESULT WINAPI pair_get_Key( IKeyValuePair_HSTRING_RESOURCE_ *iface, HSTRING *key )
{
    struct mwr_pair *p = CONTAINING_RECORD( iface, struct mwr_pair, IKeyValuePair_iface );
    if (!key) return E_POINTER;
    *key = p->key;
    return S_OK;
}

static HRESULT WINAPI pair_get_Value( IKeyValuePair_HSTRING_RESOURCE_ *iface, IUnknown **value )
{
    struct mwr_pair *p = CONTAINING_RECORD( iface, struct mwr_pair, IKeyValuePair_iface );
    TRACE( "pair_get_Value\n" );
    if (!value) return E_POINTER;
    *value = p->value;
    if (*value) IUnknown_AddRef( *value );
    return S_OK;
}

static const struct IKeyValuePair_HSTRING_RESOURCEVtbl pair_vtbl =
{
    pair_QueryInterface,
    pair_AddRef,
    pair_Release,
    pair_GetIids,
    pair_GetRuntimeClassName,
    pair_GetTrustLevel,
    pair_get_Key,
    pair_get_Value,
};

static HRESULT mwr_pair_create( HSTRING key, IUnknown *value, IKeyValuePair_HSTRING_RESOURCE_ **out )
{
    struct mwr_pair *p = HeapAlloc( GetProcessHeap(), 0, sizeof(*p) );
    if (!p) return E_OUTOFMEMORY;
    p->IKeyValuePair_iface.lpVtbl = &pair_vtbl;
    p->ref = 1;
    WindowsDuplicateString( key, &p->key );
    p->value = value;
    if (value) IUnknown_AddRef( value );
    *out = &p->IKeyValuePair_iface;
    return S_OK;
}


/* ------------------------------------------------------------------ */
/* ResourceCandidate                                                  */
/*   6 Qualifiers, 7 Language, 8 CandidateStates, 9 Matched,           */
/*   10 MatchedUri, 11 Score, 12 Text, 13 ValueAsString,               */
/*   14 GetAppResourceUri, 15 GetValueAsString(context)                */
/* ------------------------------------------------------------------ */

struct mwr_candidate
{
    IResourceCandidate IResourceCandidate_iface;
    LONG ref;
    WCHAR *name;
    WCHAR *value;
    WCHAR *language;
};

static struct mwr_candidate *candidate_from_iface( IResourceCandidate *iface )
{
    return CONTAINING_RECORD( iface, struct mwr_candidate, IResourceCandidate_iface );
}

static HRESULT WINAPI cand_QueryInterface( IResourceCandidate *iface, REFIID iid, void **obj )
{
    TRACE( "cand_QueryInterface %s\n", debugstr_guid( iid ) );
    *obj = NULL;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable )
            || IsEqualGUID( iid, &IID_IResourceCandidate ))
        *obj = iface;
    else
        return E_NOINTERFACE;
    incr( &candidate_from_iface( iface )->ref );
    return S_OK;
}

static ULONG WINAPI cand_AddRef( IResourceCandidate *iface ) { return incr( &candidate_from_iface( iface )->ref ); }
static ULONG WINAPI cand_Release( IResourceCandidate *iface )
{
    struct mwr_candidate *c = candidate_from_iface( iface );
    LONG ref = decr( &c->ref );
    if (!ref)
    {
        HeapFree( GetProcessHeap(), 0, c->name );
        HeapFree( GetProcessHeap(), 0, c->value );
        HeapFree( GetProcessHeap(), 0, c->language );
        HeapFree( GetProcessHeap(), 0, c );
    }
    return ref;
}

static HRESULT WINAPI cand_GetIids( IResourceCandidate *iface, ULONG *count, IID **iids )
{
    return simple_get_iids( iface, count, iids );
}

static HRESULT WINAPI cand_GetRuntimeClassName( IResourceCandidate *iface, HSTRING *name )
{
    return hstring_from( L"Microsoft.Windows.ApplicationModel.Resources.ResourceCandidate", name );
}

static HRESULT WINAPI cand_GetTrustLevel( IResourceCandidate *iface, enum TrustLevel *level )
{
    *level = FullTrust;
    return S_OK;
}

static HRESULT WINAPI cand_get_Qualifiers( IResourceCandidate *iface, IMapView_HSTRING_HSTRING_ **value )
{
    TRACE( "cand_get_Qualifiers\n" );
    if (!value) return E_POINTER;
    *value = &mwr_empty_qualifier_map()->IMapView_HSTRING_HSTRING__iface;
    return S_OK;
}

static HRESULT WINAPI cand_get_Language( IResourceCandidate *iface, HSTRING *value )
{
    struct mwr_candidate *c = candidate_from_iface( iface );
    TRACE( "cand_get_Language\n" );
    return hstring_from( c->language ? c->language : L"en-US", value );
}

static HRESULT WINAPI cand_get_CandidateStates( IResourceCandidate *iface,
                                                IVectorView_ResourceCandidateState_ **value )
{
    TRACE( "cand_get_CandidateStates\n" );
    if (!value) return E_POINTER;
    *value = mwr_candidate_states();
    if (!*value) return E_OUTOFMEMORY;
    return S_OK;
}

static HRESULT WINAPI cand_get_Matched( IResourceCandidate *iface, boolean *value )
{
    TRACE( "cand_get_Matched\n" );
    if (!value) return E_POINTER;
    *value = true;
    return S_OK;
}

static HRESULT WINAPI cand_get_MatchedUri( IResourceCandidate *iface, Uri **value )
{
    TRACE( "cand_get_MatchedUri\n" );
    if (!value) return E_POINTER;
    *value = NULL;
    return S_OK;
}

static HRESULT WINAPI cand_get_Score( IResourceCandidate *iface, DOUBLE *value )
{
    TRACE( "cand_get_Score\n" );
    if (!value) return E_POINTER;
    *value = 1.0;
    return S_OK;
}

static HRESULT WINAPI cand_get_Text( IResourceCandidate *iface, HSTRING *value )
{
    struct mwr_candidate *c = candidate_from_iface( iface );
    TRACE( "cand_get_Text\n" );
    return hstring_from( c->value, value );
}

static HRESULT WINAPI cand_get_ValueAsString( IResourceCandidate *iface, HSTRING *value )
{
    struct mwr_candidate *c = candidate_from_iface( iface );
    TRACE( "cand_get_ValueAsString %s\n", debugstr_w( c->value ) );
    return hstring_from( c->value, value );
}

static HRESULT WINAPI cand_GetAppResourceUri( IResourceCandidate *iface, Uri **value )
{
    TRACE( "cand_GetAppResourceUri\n" );
    if (!value) return E_POINTER;
    *value = NULL;
    return S_OK;
}

static HRESULT WINAPI cand_GetValueAsString( IResourceCandidate *iface, IResourceContext *ctx, HSTRING *value )
{
    return cand_get_ValueAsString( iface, value );
}

static const struct IResourceCandidateVtbl cand_vtbl =
{
    cand_QueryInterface,
    cand_AddRef,
    cand_Release,
    cand_GetIids,
    cand_GetRuntimeClassName,
    cand_GetTrustLevel,
    cand_get_Qualifiers,
    cand_get_Language,
    cand_get_CandidateStates,
    cand_get_Matched,
    cand_get_MatchedUri,
    cand_get_Score,
    cand_get_Text,
    cand_get_ValueAsString,
    cand_GetAppResourceUri,
    cand_GetValueAsString,
};

static HRESULT candidate_create( const WCHAR *name, const WCHAR *value, const WCHAR *language,
                                 IResourceCandidate **out )
{
    struct mwr_candidate *c = HeapAlloc( GetProcessHeap(), 0, sizeof(*c) );
    if (!c) return E_OUTOFMEMORY;
    c->IResourceCandidate_iface.lpVtbl = &cand_vtbl;
    c->ref = 1;
    c->name = dupW( name );
    c->value = dupW( value );
    c->language = language ? dupW( language ) : NULL;
    *out = &c->IResourceCandidate_iface;
    return S_OK;
}

/* ------------------------------------------------------------------ */
/* NamedResource                                                       */
/*   6 Name, 7 Value, 8 ValueAsString, 9 GetValue(context)             */
/* ------------------------------------------------------------------ */

struct mwr_named_resource
{
    INamedResource INamedResource_iface;
    LONG ref;
    WCHAR *name;
    WCHAR *value;
};

static struct mwr_named_resource *named_from_iface( INamedResource *iface )
{
    return CONTAINING_RECORD( iface, struct mwr_named_resource, INamedResource_iface );
}

static HRESULT WINAPI named_QueryInterface( INamedResource *iface, REFIID iid, void **obj )
{
    TRACE( "named_QueryInterface %s\n", debugstr_guid( iid ) );
    *obj = NULL;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable )
            || IsEqualGUID( iid, &IID_INamedResource ))
        *obj = iface;
    else
        return E_NOINTERFACE;
    incr( &named_from_iface( iface )->ref );
    return S_OK;
}

static ULONG WINAPI named_AddRef( INamedResource *iface ) { return incr( &named_from_iface( iface )->ref ); }
static ULONG WINAPI named_Release( INamedResource *iface )
{
    struct mwr_named_resource *n = named_from_iface( iface );
    LONG ref = decr( &n->ref );
    if (!ref)
    {
        HeapFree( GetProcessHeap(), 0, n->name );
        HeapFree( GetProcessHeap(), 0, n->value );
        HeapFree( GetProcessHeap(), 0, n );
    }
    return ref;
}

static HRESULT WINAPI named_GetIids( INamedResource *iface, ULONG *count, IID **iids )
{
    return simple_get_iids( iface, count, iids );
}

static HRESULT WINAPI named_GetRuntimeClassName( INamedResource *iface, HSTRING *name )
{
    return hstring_from( L"Microsoft.Windows.ApplicationModel.Resources.NamedResource", name );
}

static HRESULT WINAPI named_GetTrustLevel( INamedResource *iface, enum TrustLevel *level )
{
    *level = FullTrust;
    return S_OK;
}

static HRESULT WINAPI named_get_Name( INamedResource *iface, HSTRING *value )
{
    struct mwr_named_resource *n = named_from_iface( iface );
    TRACE( "named_get_Name\n" );
    return hstring_from( n->name, value );
}

static HRESULT WINAPI named_get_Value( INamedResource *iface, IResourceCandidate **value )
{
    struct mwr_named_resource *n = named_from_iface( iface );
    TRACE( "named_get_Value\n" );
    if (!value) return E_POINTER;
    return candidate_create( n->name, n->value, NULL, value );
}

static HRESULT WINAPI named_get_ValueAsString( INamedResource *iface, HSTRING *value )
{
    struct mwr_named_resource *n = named_from_iface( iface );
    TRACE( "named_get_ValueAsString %s\n", debugstr_w( n->value ) );
    return hstring_from( n->value, value );
}

static HRESULT WINAPI named_GetValue( INamedResource *iface, IResourceContext *ctx, HSTRING *value )
{
    return named_get_ValueAsString( iface, value );
}

static const struct INamedResourceVtbl named_vtbl =
{
    named_QueryInterface,
    named_AddRef,
    named_Release,
    named_GetIids,
    named_GetRuntimeClassName,
    named_GetTrustLevel,
    named_get_Name,
    named_get_Value,
    named_get_ValueAsString,
    named_GetValue,
};

static HRESULT named_create( const WCHAR *name, const WCHAR *value, INamedResource **out )
{
    struct mwr_named_resource *n = HeapAlloc( GetProcessHeap(), 0, sizeof(*n) );
    if (!n) return E_OUTOFMEMORY;
    n->INamedResource_iface.lpVtbl = &named_vtbl;
    n->ref = 1;
    n->name = dupW( name );
    n->value = dupW( value );
    *out = &n->INamedResource_iface;
    return S_OK;
}

/* ------------------------------------------------------------------ */
/* string -> NamedResource map (TopLevelResourceKeys)                 */
/* ------------------------------------------------------------------ */

struct mwr_nr_map
{
    IMapView_HSTRING_NAMEDRESOURCE_ IMapView_iface;
    LONG ref;
    UINT32 count;
};

static struct mwr_nr_map *nrmap_from_iface( IMapView_HSTRING_NAMEDRESOURCE_ *iface )
{
    return CONTAINING_RECORD( iface, struct mwr_nr_map, IMapView_iface );
}

static HRESULT WINAPI nrm_QueryInterface( IMapView_HSTRING_NAMEDRESOURCE_ *iface, REFIID iid, void **obj )
{
    TRACE( "nrm_QueryInterface %s\n", debugstr_guid( iid ) );
    *obj = NULL;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable )
            || IsEqualGUID( iid, &IID_IMapView_HSTRING_NAMEDRESOURCE_ ))
        *obj = iface;
    else
        return E_NOINTERFACE;
    incr( &nrmap_from_iface( iface )->ref );
    return S_OK;
}

static ULONG WINAPI nrm_AddRef( IMapView_HSTRING_NAMEDRESOURCE_ *iface )
{
    return incr( &nrmap_from_iface( iface )->ref );
}

static ULONG WINAPI nrm_Release( IMapView_HSTRING_NAMEDRESOURCE_ *iface )
{
    struct mwr_nr_map *m = nrmap_from_iface( iface );
    LONG ref = decr( &m->ref );
    if (!ref) HeapFree( GetProcessHeap(), 0, m );
    return ref;
}

static HRESULT WINAPI nrm_GetIids( IMapView_HSTRING_NAMEDRESOURCE_ *iface, ULONG *count, IID **iids )
{
    return simple_get_iids( iface, count, iids );
}

static HRESULT WINAPI nrm_GetRuntimeClassName( IMapView_HSTRING_NAMEDRESOURCE_ *iface, HSTRING *name )
{
    return hstring_from( L"Windows.Foundation.Collections.IMapView`2<System.String,Microsoft.Windows.ApplicationModel.Resources.NamedResource>", name );
}

static HRESULT WINAPI nrm_GetTrustLevel( IMapView_HSTRING_NAMEDRESOURCE_ *iface, enum TrustLevel *level )
{
    *level = FullTrust;
    return S_OK;
}

static HRESULT WINAPI nrm_GetAt( IMapView_HSTRING_NAMEDRESOURCE_ *iface, UINT32 index,
                                 IKeyValuePair_HSTRING_NAMEDRESOURCE_ **item )
{
    struct mwr_nr_map *m = nrmap_from_iface( iface );
    WCHAR *key = NULL, *val = NULL;
    INamedResource *nr = NULL;
    HSTRING k = NULL;
    HRESULT hr;

    TRACE( "nrm_GetAt %u\n", index );
    if (!item) return E_POINTER;
    init_pri();
    if (!the_index || index >= pri_name_count( the_index )) return E_INVALIDARG;
    key = HeapAlloc( GetProcessHeap(), 0, 1024 * sizeof(WCHAR) );
    if (!key) return E_OUTOFMEMORY;
    if (!pri_name( the_index, index, key, 1024 ))
    {
        HeapFree( GetProcessHeap(), 0, key );
        return E_FAIL;
    }
    hr = hstring_from( key, &k );
    if (hr == S_OK && SUCCEEDED( hr = pri_lookup_dup( the_index, key, NULL, 0, &val ) )
            && SUCCEEDED( hr = named_create( key, val ? val : L"", &nr ) ))
    {
        hr = mwr_nr_pair_create( k, (IUnknown *)nr, item );
        IUnknown_Release( nr );
    }
    HeapFree( GetProcessHeap(), 0, key );
    HeapFree( GetProcessHeap(), 0, val );
    WindowsDeleteString( k );
    return hr;
}

static HRESULT WINAPI nrm_get_Size( IMapView_HSTRING_NAMEDRESOURCE_ *iface, UINT32 *size )
{
    struct mwr_nr_map *m = nrmap_from_iface( iface );
    init_pri();
    TRACE( "nrm_get_Size -> %u\n", the_index ? pri_name_count( the_index ) : 0 );
    if (!size) return E_POINTER;
    *size = m->count;
    return S_OK;
}

static HRESULT WINAPI nrm_HasKey( IMapView_HSTRING_NAMEDRESOURCE_ *iface, HSTRING key, boolean *found )
{
    TRACE( "nrm_HasKey\n" );
    if (!found) return E_POINTER;
    *found = true;
    return S_OK;
}

static HRESULT WINAPI nrm_Lookup( IMapView_HSTRING_NAMEDRESOURCE_ *iface, HSTRING key,
                                  INamedResource **value )
{
    WCHAR *kbuf = NULL, *val = NULL;
    HRESULT hr = S_OK;
    const WCHAR *kstr;
    UINT32 klen;

    TRACE( "nrm_Lookup\n" );
    if (!value) return E_POINTER;
    init_pri();
    kstr = WindowsGetStringRawBuffer( key, &klen );
    if (!kstr || !the_index) return E_FAIL;
    kbuf = HeapAlloc( GetProcessHeap(), 0, (klen + 1) * sizeof(WCHAR) );
    if (!kbuf) return E_OUTOFMEMORY;
    memcpy( kbuf, kstr, klen * sizeof(WCHAR) );
    kbuf[klen] = 0;
    if (SUCCEEDED( hr = pri_lookup_dup( the_index, kbuf, NULL, 0, &val ) ))
        hr = named_create( kbuf, val ? val : L"", value );
    HeapFree( GetProcessHeap(), 0, kbuf );
    HeapFree( GetProcessHeap(), 0, val );
    return hr;
}

const struct IMapView_HSTRING_NAMEDRESOURCEVtbl nrm_vtbl =
{
    nrm_QueryInterface,
    nrm_AddRef,
    nrm_Release,
    nrm_GetIids,
    nrm_GetRuntimeClassName,
    nrm_GetTrustLevel,
    nrm_GetAt,
    nrm_get_Size,
    nrm_HasKey,
    nrm_Lookup,
};

/* key->NamedResource pair (TopLevelResourceKeys element) */
struct mwr_nr_pair
{
    IKeyValuePair_HSTRING_NAMEDRESOURCE_ IKeyValuePair_iface;
    LONG ref;
    HSTRING key;
    INamedResource *value;
};

static HRESULT WINAPI nrp_QueryInterface( IKeyValuePair_HSTRING_NAMEDRESOURCE_ *iface, REFIID iid, void **obj )
{
    *obj = NULL;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable )
            || IsEqualGUID( iid, &IID_IKeyValuePair_HSTRING_NAMEDRESOURCE_ ))
        *obj = iface;
    else
        return E_NOINTERFACE;
    incr( &CONTAINING_RECORD( iface, struct mwr_nr_pair, IKeyValuePair_iface )->ref );
    return S_OK;
}

static ULONG WINAPI nrp_AddRef( IKeyValuePair_HSTRING_NAMEDRESOURCE_ *iface )
{
    return incr( &CONTAINING_RECORD( iface, struct mwr_nr_pair, IKeyValuePair_iface )->ref );
}

static ULONG WINAPI nrp_Release( IKeyValuePair_HSTRING_NAMEDRESOURCE_ *iface )
{
    struct mwr_nr_pair *p = CONTAINING_RECORD( iface, struct mwr_nr_pair, IKeyValuePair_iface );
    LONG ref = decr( &p->ref );
    if (!ref)
    {
        WindowsDeleteString( p->key );
        INamedResource_Release( p->value );
        HeapFree( GetProcessHeap(), 0, p );
    }
    return ref;
}

static HRESULT WINAPI nrp_GetIids( IKeyValuePair_HSTRING_NAMEDRESOURCE_ *iface, ULONG *count, IID **iids )
{
    return simple_get_iids( iface, count, iids );
}

static HRESULT WINAPI nrp_GetRuntimeClassName( IKeyValuePair_HSTRING_NAMEDRESOURCE_ *iface, HSTRING *name )
{
    return hstring_from( L"Windows.Foundation.Collections.IKeyValuePair`2<System.String,Microsoft.Windows.ApplicationModel.Resources.NamedResource>", name );
}

static HRESULT WINAPI nrp_GetTrustLevel( IKeyValuePair_HSTRING_NAMEDRESOURCE_ *iface, enum TrustLevel *level )
{
    *level = FullTrust;
    return S_OK;
}

static HRESULT WINAPI nrp_get_Key( IKeyValuePair_HSTRING_NAMEDRESOURCE_ *iface, HSTRING *key )
{
    struct mwr_nr_pair *p = CONTAINING_RECORD( iface, struct mwr_nr_pair, IKeyValuePair_iface );
    if (!key) return E_POINTER;
    *key = p->key;
    return S_OK;
}

static HRESULT WINAPI nrp_get_Value( IKeyValuePair_HSTRING_NAMEDRESOURCE_ *iface, INamedResource **value )
{
    struct mwr_nr_pair *p = CONTAINING_RECORD( iface, struct mwr_nr_pair, IKeyValuePair_iface );
    if (!value) return E_POINTER;
    *value = p->value;
    INamedResource_AddRef( *value );
    return S_OK;
}

static const struct IKeyValuePair_HSTRING_NAMEDRESOURCEVtbl nrp_vtbl =
{
    nrp_QueryInterface,
    nrp_AddRef,
    nrp_Release,
    nrp_GetIids,
    nrp_GetRuntimeClassName,
    nrp_GetTrustLevel,
    nrp_get_Key,
    nrp_get_Value,
};

static HRESULT mwr_nr_pair_create( HSTRING key, INamedResource *value, IKeyValuePair_HSTRING_NAMEDRESOURCE_ **out )
{
    struct mwr_nr_pair *p = HeapAlloc( GetProcessHeap(), 0, sizeof(*p) );
    if (!p) return E_OUTOFMEMORY;
    p->IKeyValuePair_iface.lpVtbl = &nrp_vtbl;
    p->ref = 1;
    WindowsDuplicateString( key, &p->key );
    p->value = value;
    INamedResource_AddRef( value );
    *out = &p->IKeyValuePair_iface;
    return S_OK;
}

static struct mwr_nr_map *the_nr_map;

static struct mwr_nr_map *mwr_top_level_resource_keys( void )
{
    if (!the_nr_map)
    {
        struct mwr_nr_map *m = HeapAlloc( GetProcessHeap(), 0, sizeof(*m) );
        if (!m) return NULL;
        m->IMapView_iface.lpVtbl = &nrm_vtbl;
        m->ref = 1;
        init_pri();
        m->count = the_index ? pri_name_count( the_index ) : 0;
        the_nr_map = m;
    }
    return the_nr_map;
}

/* ------------------------------------------------------------------ */
/* ResourceMap                                                         */
/*   6 TryGetValueWithContext 7 TryGetValue 8 GetValueByIndexWithContext*/
/*   9 GetValueByIndex 10 GetValueWithContext 11 GetValue              */
/*   12 TryGetSubtree 13 GetSubtree 14 get_ResourceCount               */
/*   15 get_Name 16 get_Uri 17 get_Language 18 get_QualifierValues     */
/*   19 get_TopLevelResourceKeys                                       */
/* ------------------------------------------------------------------ */

struct mwr_map
{
    IResourceMap IResourceMap_iface;
    LONG ref;
};

static struct mwr_map *map_from_iface( IResourceMap *iface )
{
    return CONTAINING_RECORD( iface, struct mwr_map, IResourceMap_iface );
}

static HRESULT map_lookup_candidate( const WCHAR *key, IResourceCandidate **out )
{
    WCHAR *val = NULL, *namebuf = NULL;
    HRESULT hr;

    init_pri();
    if (!the_index) return E_FAIL;
    namebuf = HeapAlloc( GetProcessHeap(), 0, 1024 * sizeof(WCHAR) );
    if (!namebuf) return E_OUTOFMEMORY;
    hr = pri_lookup_dup( the_index, key, NULL, 0, &val );
    if (SUCCEEDED( hr ))
    {
        hr = candidate_create( key, val ? val : L"", NULL, out );
        HeapFree( GetProcessHeap(), 0, val );
    }
    HeapFree( GetProcessHeap(), 0, namebuf );
    return hr;
}

static HRESULT WINAPI map_QueryInterface( IResourceMap *iface, REFIID iid, void **obj )
{
    TRACE( "map_QueryInterface %s\n", debugstr_guid( iid ) );
    *obj = NULL;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable )
            || IsEqualGUID( iid, &IID_IResourceMap ) || IsEqualGUID( iid, &IID_IResourceMap2 ))
        *obj = iface;
    else
        return E_NOINTERFACE;
    incr( &map_from_iface( iface )->ref );
    return S_OK;
}

static ULONG WINAPI map_AddRef( IResourceMap *iface ) { return incr( &map_from_iface( iface )->ref ); }
static ULONG WINAPI map_Release( IResourceMap *iface )
{
    struct mwr_map *m = map_from_iface( iface );
    LONG ref = decr( &m->ref );
    if (!ref) HeapFree( GetProcessHeap(), 0, m );
    return ref;
}

static HRESULT WINAPI map_GetIids( IResourceMap *iface, ULONG *count, IID **iids )
{
    return simple_get_iids( iface, count, iids );
}

static HRESULT WINAPI map_GetRuntimeClassName( IResourceMap *iface, HSTRING *name )
{
    return hstring_from( L"Microsoft.Windows.ApplicationModel.Resources.ResourceMap", name );
}

static HRESULT WINAPI map_GetTrustLevel( IResourceMap *iface, enum TrustLevel *level )
{
    *level = FullTrust;
    return S_OK;
}

static HRESULT WINAPI map_TryGetValueWithContext( IResourceMap *iface, IResourceContext *ctx, HSTRING key,
                                                  IResourceCandidate **value )
{
    const WCHAR *kstr;
    UINT32 klen;
    WCHAR *kbuf;
    HRESULT hr;

    TRACE( "TryGetValueWithContext\n" );
    if (!value) return E_POINTER;
    kstr = WindowsGetStringRawBuffer( key, &klen );
    kbuf = HeapAlloc( GetProcessHeap(), 0, (klen + 1) * sizeof(WCHAR) );
    if (!kbuf) return E_OUTOFMEMORY;
    memcpy( kbuf, kstr, klen * sizeof(WCHAR) );
    kbuf[klen] = 0;
    hr = map_lookup_candidate( kbuf, value );
    HeapFree( GetProcessHeap(), 0, kbuf );
    return hr;
}

static HRESULT WINAPI map_TryGetValue( IResourceMap *iface, HSTRING key, IResourceCandidate **value )
{
    TRACE( "TryGetValue\n" );
    return map_TryGetValueWithContext( iface, NULL, key, value );
}

static HRESULT WINAPI map_GetValueByIndexWithContext( IResourceMap *iface, IResourceContext *ctx,
                                                      UINT32 index, IKeyValuePair_HSTRING_RESOURCE_ **item )
{
    WCHAR *key = NULL, *val = NULL;
    IResourceCandidate *cand = NULL;
    HSTRING k = NULL;
    HRESULT hr = E_FAIL;

    TRACE( "GetValueByIndexWithContext %u\n", index );
    if (!item) return E_POINTER;
    init_pri();
    if (!the_index) return E_FAIL;
    key = HeapAlloc( GetProcessHeap(), 0, 1024 * sizeof(WCHAR) );
    if (!key) return E_OUTOFMEMORY;
    if (pri_name( the_index, index, key, 1024 ))
    {
        if (SUCCEEDED( hr = pri_lookup_dup( the_index, key, NULL, 0, &val ) )
                && SUCCEEDED( hr = candidate_create( key, val ? val : L"", NULL, &cand ) )
                && SUCCEEDED( hr = hstring_from( key, &k ) ))
            hr = mwr_pair_create( k, (IUnknown *)cand, item );
        if (cand) IResourceCandidate_Release( cand );
        WindowsDeleteString( k );
    }
    HeapFree( GetProcessHeap(), 0, key );
    HeapFree( GetProcessHeap(), 0, val );
    return hr;
}

static HRESULT WINAPI map_GetValueByIndex( IResourceMap *iface, UINT32 index,
                                           IKeyValuePair_HSTRING_RESOURCE_ **item )
{
    TRACE( "GetValueByIndex %u\n", index );
    return map_GetValueByIndexWithContext( iface, NULL, index, item );
}

static HRESULT WINAPI map_GetValueWithContext( IResourceMap *iface, IResourceContext *ctx, HSTRING key,
                                               IResourceCandidate **value )
{
    TRACE( "GetValueWithContext\n" );
    return map_TryGetValueWithContext( iface, ctx, key, value );
}

static HRESULT WINAPI map_GetValue( IResourceMap *iface, HSTRING key, IResourceCandidate **value )
{
    TRACE( "GetValue\n" );
    return map_TryGetValueWithContext( iface, NULL, key, value );
}

static HRESULT WINAPI map_TryGetSubtree( IResourceMap *iface, HSTRING name, IResourceMap **value )
{
    TRACE( "TryGetSubtree\n" );
    if (!value) return E_POINTER;
    *value = NULL;
    return E_FAIL;
}

static HRESULT WINAPI map_GetSubtree( IResourceMap *iface, HSTRING name, IResourceMap **value )
{
    TRACE( "GetSubtree\n" );
    return map_TryGetSubtree( iface, name, value );
}

static HRESULT WINAPI map_get_ResourceCount( IResourceMap *iface, UINT32 *value )
{
    init_pri();
    TRACE( "get_ResourceCount -> %u\n", the_index ? pri_name_count( the_index ) : 0 );
    if (!value) return E_POINTER;
    *value = the_index ? pri_name_count( the_index ) : 0;
    return S_OK;
}

static HRESULT WINAPI map_get_Name( IResourceMap *iface, HSTRING *value )
{
    TRACE( "map_get_Name\n" );
    return hstring_from( L"Resources", value );
}

static HRESULT WINAPI map_get_Uri( IResourceMap *iface, HSTRING *value )
{
    TRACE( "map_get_Uri\n" );
    return hstring_from( L"ms-resource://CalculatorApp/Files/Resources", value );
}

static HRESULT WINAPI map_get_Language( IResourceMap *iface, HSTRING *value )
{
    TRACE( "map_get_Language\n" );
    return hstring_from( L"en-US", value );
}

static HRESULT WINAPI map_get_QualifierValues( IResourceMap *iface, IMapView_HSTRING_HSTRING_ **value )
{
    TRACE( "map_get_QualifierValues\n" );
    if (!value) return E_POINTER;
    *value = &mwr_empty_qualifier_map()->IMapView_HSTRING_HSTRING__iface;
    return S_OK;
}

static HRESULT WINAPI map_get_TopLevelResourceKeys( IResourceMap *iface,
                                                    IMapView_HSTRING_NAMEDRESOURCE_ **value )
{
    struct mwr_nr_map *m = mwr_top_level_resource_keys();
    TRACE( "map_get_TopLevelResourceKeys\n" );
    if (!value) return E_POINTER;
    if (!m) return E_OUTOFMEMORY;
    *value = &m->IMapView_iface;
    IUnknown_AddRef( (IUnknown *)*value );
    return S_OK;
}

static const struct IResourceMapVtbl map_vtbl =
{
    map_QueryInterface,
    map_AddRef,
    map_Release,
    map_GetIids,
    map_GetRuntimeClassName,
    map_GetTrustLevel,
    map_TryGetValueWithContext,
    map_TryGetValue,
    map_GetValueByIndexWithContext,
    map_GetValueByIndex,
    map_GetValueWithContext,
    map_GetValue,
    map_TryGetSubtree,
    map_GetSubtree,
    map_get_ResourceCount,
    map_get_Name,
    map_get_Uri,
    map_get_Language,
    map_get_QualifierValues,
    map_get_TopLevelResourceKeys,
};

static struct mwr_map *the_map;

static struct mwr_map *map_create( void )
{
    if (!the_map)
    {
        struct mwr_map *m = HeapAlloc( GetProcessHeap(), 0, sizeof(*m) );
        if (!m) return NULL;
        m->IResourceMap_iface.lpVtbl = &map_vtbl;
        m->ref = 1;
        the_map = m;
    }
    return the_map;
}

/* ------------------------------------------------------------------ */
/* ResourceManager                                                     */
/*   6 get_MainResourceMap 7 get_ResourceContext 8 put_ResourceContext */
/*   9 UpdateResourceLocators 10 GetQualifiedResourceMap               */
/*   11 CreateResourceContext                                          */
/* ------------------------------------------------------------------ */

struct mwr_manager
{
    IResourceManager IResourceManager_iface;
    LONG ref;
    struct mwr_context *context;
};

static struct mwr_manager *manager_from_iface( IResourceManager *iface )
{
    return CONTAINING_RECORD( iface, struct mwr_manager, IResourceManager_iface );
}

static HRESULT WINAPI manager_QueryInterface( IResourceManager *iface, REFIID iid, void **obj )
{
    TRACE( "manager_QueryInterface %s\n", debugstr_guid( iid ) );
    *obj = NULL;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable )
            || IsEqualGUID( iid, &IID_IResourceManager ) || IsEqualGUID( iid, &IID_IResourceManager2 ))
        *obj = iface;
    else
        return E_NOINTERFACE;
    incr( &manager_from_iface( iface )->ref );
    return S_OK;
}

static ULONG WINAPI manager_AddRef( IResourceManager *iface ) { return incr( &manager_from_iface( iface )->ref ); }
static ULONG WINAPI manager_Release( IResourceManager *iface )
{
    struct mwr_manager *m = manager_from_iface( iface );
    LONG ref = decr( &m->ref );
    if (!ref)
    {
        if (m->context) IResourceContext_Release( (IResourceContext *)&m->context->IResourceContext_iface );
        HeapFree( GetProcessHeap(), 0, m );
    }
    return ref;
}

static HRESULT WINAPI manager_GetIids( IResourceManager *iface, ULONG *count, IID **iids )
{
    return simple_get_iids( iface, count, iids );
}

static HRESULT WINAPI manager_GetRuntimeClassName( IResourceManager *iface, HSTRING *name )
{
    return hstring_from( L"Microsoft.Windows.ApplicationModel.Resources.ResourceManager", name );
}

static HRESULT WINAPI manager_GetTrustLevel( IResourceManager *iface, enum TrustLevel *level )
{
    *level = FullTrust;
    return S_OK;
}

static HRESULT WINAPI manager_get_MainResourceMap( IResourceManager *iface, IResourceMap **value )
{
    struct mwr_map *m = map_create();
    TRACE( "get_MainResourceMap\n" );
    if (!value) return E_POINTER;
    if (!m) return E_OUTOFMEMORY;
    *value = &m->IResourceMap_iface;
    IResourceMap_AddRef( *value );
    return S_OK;
}

static HRESULT WINAPI manager_get_ResourceContext( IResourceManager *iface, IResourceContext **value )
{
    struct mwr_manager *m = manager_from_iface( iface );
    TRACE( "get_ResourceContext\n" );
    if (!value) return E_POINTER;
    if (!m->context && !(m->context = context_create())) return E_OUTOFMEMORY;
    *value = &m->context->IResourceContext_iface;
    IResourceContext_AddRef( *value );
    return S_OK;
}

static HRESULT WINAPI manager_put_ResourceContext( IResourceManager *iface, IResourceContext *value )
{
    struct mwr_manager *m = manager_from_iface( iface );
    TRACE( "put_ResourceContext\n" );
    if (m->context) IResourceContext_Release( (IResourceContext *)&m->context->IResourceContext_iface );
    m->context = value ? context_from_iface( value ) : NULL;
    if (m->context) IResourceContext_AddRef( (IResourceContext *)&m->context->IResourceContext_iface );
    return S_OK;
}

static HRESULT WINAPI manager_UpdateResourceLocators( IResourceManager *iface, boolean *value )
{
    TRACE( "UpdateResourceLocators\n" );
    if (!value) return E_POINTER;
    *value = true;
    return S_OK;
}

static HRESULT WINAPI manager_GetQualifiedResourceMap( IResourceManager *iface,
                                                       IMapView_HSTRING_HSTRING_ *qualifiers,
                                                       IResourceMap **value )
{
    TRACE( "GetQualifiedResourceMap\n" );
    return manager_get_MainResourceMap( iface, value );
}

static HRESULT WINAPI manager_CreateResourceContext( IResourceManager *iface, IResourceContext **value )
{
    struct mwr_context *ctx = context_create();
    TRACE( "CreateResourceContext\n" );
    if (!value) return E_POINTER;
    if (!ctx) return E_OUTOFMEMORY;
    *value = &ctx->IResourceContext_iface;
    return S_OK;
}

static const struct IResourceManagerVtbl manager_vtbl =
{
    manager_QueryInterface,
    manager_AddRef,
    manager_Release,
    manager_GetIids,
    manager_GetRuntimeClassName,
    manager_GetTrustLevel,
    manager_get_MainResourceMap,
    manager_get_ResourceContext,
    manager_put_ResourceContext,
    manager_UpdateResourceLocators,
    manager_GetQualifiedResourceMap,
    manager_CreateResourceContext,
};

static struct mwr_manager *the_manager;

static struct mwr_manager *manager_create( void )
{
    if (!the_manager)
    {
        struct mwr_manager *m = HeapAlloc( GetProcessHeap(), 0, sizeof(*m) );
        if (!m) return NULL;
        m->IResourceManager_iface.lpVtbl = &manager_vtbl;
        m->ref = 1;
        m->context = NULL;
        the_manager = m;
    }
    return the_manager;
}

/* ------------------------------------------------------------------ */
/* MRMFactory: IActivationFactory + IResourceManagerStatics          */
/*   6 ActivateInstance 7 CreateResourceManager                        */
/* ------------------------------------------------------------------ */

struct mrm_factory
{
    IActivationFactory IActivationFactory_iface;
    LONG ref;
};

static struct mrm_factory *factory_from_iface( IActivationFactory *iface )
{
    return CONTAINING_RECORD( iface, struct mrm_factory, IActivationFactory_iface );
}

static HRESULT WINAPI factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **obj )
{
    TRACE( "factory_QueryInterface %s\n", debugstr_guid( iid ) );
    *obj = NULL;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable )
            || IsEqualGUID( iid, &IID_IActivationFactory ))
        *obj = iface;
    else if (IsEqualGUID( iid, &IID_IResourceManagerStatics ))
        *obj = iface;
    else
        return E_NOINTERFACE;
    incr( &factory_from_iface( iface )->ref );
    return S_OK;
}

static ULONG WINAPI factory_AddRef( IActivationFactory *iface ) { return incr( &factory_from_iface( iface )->ref ); }
static ULONG WINAPI factory_Release( IActivationFactory *iface )
{
    struct mrm_factory *f = factory_from_iface( iface );
    LONG ref = decr( &f->ref );
    if (!ref) HeapFree( GetProcessHeap(), 0, f );
    return ref;
}

static HRESULT WINAPI factory_GetIids( IActivationFactory *iface, ULONG *count, IID **iids )
{
    return simple_get_iids( iface, count, iids );
}

static HRESULT WINAPI factory_GetRuntimeClassName( IActivationFactory *iface, HSTRING *name )
{
    return hstring_from( L"Microsoft.Windows.ApplicationModel.Resources.ResourceManager", name );
}

static HRESULT WINAPI factory_GetTrustLevel( IActivationFactory *iface, enum TrustLevel *level )
{
    *level = FullTrust;
    return S_OK;
}

static HRESULT WINAPI factory_ActivateInstance( IActivationFactory *iface, IInspectable **value )
{
    struct mwr_manager *m = manager_create();
    TRACE( "ActivateInstance\n" );
    if (!value) return E_POINTER;
    if (!m) return E_OUTOFMEMORY;
    *value = (IInspectable *)&m->IResourceManager_iface;
    IResourceManager_AddRef( (IResourceManager *)*value );
    return S_OK;
}

static HRESULT WINAPI factory_CreateResourceManager( IActivationFactory *iface, IInspectable **value )
{
    TRACE( "CreateResourceManager\n" );
    return factory_ActivateInstance( iface, value );
}

static const struct IActivationFactoryVtbl factory_vtbl =
{
    factory_QueryInterface,
    factory_AddRef,
    factory_Release,
    factory_GetIids,
    factory_GetRuntimeClassName,
    factory_GetTrustLevel,
    factory_ActivateInstance,
    factory_CreateResourceManager,
};

static struct mrm_factory *the_factory;

static struct mrm_factory *mrm_factory_get( void )
{
    if (!the_factory)
    {
        struct mrm_factory *f = HeapAlloc( GetProcessHeap(), 0, sizeof(*f) );
        if (!f) return NULL;
        f->IActivationFactory_iface.lpVtbl = &factory_vtbl;
        f->ref = 1;
        the_factory = f;
    }
    return the_factory;
}


static HRESULT WINAPI csv_QueryInterface( IVectorView_ResourceCandidateState_ *iface, REFIID iid, void **obj )
{
    *obj = NULL;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable )
            || IsEqualGUID( iid, &IID_IVectorView_ResourceCandidateState_ ))
        *obj = iface;
    else
        return E_NOINTERFACE;
    incr( &the_states->ref );
    return S_OK;
}
static ULONG WINAPI csv_AddRef( IVectorView_ResourceCandidateState_ *iface ) { return incr( &the_states->ref ); }
static ULONG WINAPI csv_Release( IVectorView_ResourceCandidateState_ *iface ) { return decr( &the_states->ref ); }
static HRESULT WINAPI csv_GetAt( IVectorView_ResourceCandidateState_ *iface, UINT32 index, enum ResourceCandidateState *item )
{
    if (!item) return E_POINTER;
    if (index) return E_INVALIDARG;
    *item = ResourceCandidateState_Matched;
    return S_OK;
}
static HRESULT WINAPI csv_get_Size( IVectorView_ResourceCandidateState_ *iface, UINT32 *size )
{
    if (!size) return E_POINTER;
    *size = 1;
    return S_OK;
}
static HRESULT WINAPI csv_IndexOf( IVectorView_ResourceCandidateState_ *iface, enum ResourceCandidateState item,
                                   UINT32 *index, boolean *found )
{
    if (!index || !found) return E_POINTER;
    *index = 0;
    *found = (item == ResourceCandidateState_Matched);
    return S_OK;
}

const struct IVectorView_ResourceCandidateStateVtbl mwr_candidate_states_vtbl =
{
    csv_QueryInterface,
    csv_AddRef,
    csv_Release,
    svv_GetIids_stub,
    svv_GetRuntimeClassName_stub,
    svv_GetTrustLevel_stub,
    csv_GetAt,
    csv_get_Size,
    csv_IndexOf,
};

/* ------------------------------------------------------------------ */
/* exports                                                            */
/* ------------------------------------------------------------------ */

HRESULT WINAPI DllGetActivationFactory( HSTRING class, IActivationFactory **factory )
{
    struct mrm_factory *f = mrm_factory_get();

    TRACE( "DllGetActivationFactory %s\n", debugstr_hstring( class ) );
    if (!factory) return E_POINTER;
    if (!f) return E_OUTOFMEMORY;
    *factory = &f->IActivationFactory_iface;
    IActivationFactory_AddRef( *factory );
    return S_OK;
}

HRESULT WINAPI DllGetClassObject( REFCLSID clsid, REFIID iid, void **obj )
{
    struct mrm_factory *f = mrm_factory_get();
    TRACE( "DllGetClassObject %s\n", debugstr_guid( iid ) );
    if (!obj) return E_POINTER;
    if (!f) return E_OUTOFMEMORY;
    *obj = &f->IActivationFactory_iface;
    IActivationFactory_AddRef( (IActivationFactory *)*obj );
    return S_OK;
}

BOOL WINAPI DllMain( HINSTANCE inst, DWORD reason, LPVOID reserved )
{
    if (reason == DLL_PROCESS_ATTACH) DisableThreadLibraryCalls( inst );
    return TRUE;
}

HRESULT WINAPI DllCanUnloadNow( void )
{
    return S_FALSE;
}
