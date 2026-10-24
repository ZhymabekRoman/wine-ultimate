/* WinRT Windows.Foundation.Uri implementation
 *
 * Copyright 2026 Zhymabek Roman
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
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "windef.h"
#include "winbase.h"
#include "winstring.h"
#include "oleauto.h"
#include "wine/debug.h"

#include "private.h"

WINE_DEFAULT_DEBUG_CHANNEL(wintypes);

#define heap_alloc(size) malloc( size )
#define heap_calloc(num,size) calloc( num, size )
#define heap_strdup(str) wcsdup( str )
#define heap_free free

static const CHAR hex_chars[] = "0123456789ABCDEF";

static HRESULT hstr_dup( const WCHAR *str, HSTRING *out )
{
    return WindowsCreateString( str, lstrlenW( str ), out );
}

static WCHAR *heap_strndup( const WCHAR *str, SIZE_T len )
{
    WCHAR *ret = heap_alloc( (len + 1) * sizeof(WCHAR) );

    if (ret)
    {
        memcpy( ret, str, len * sizeof(WCHAR) );
        ret[len] = 0;
    }
    return ret;
}

static BOOL is_scheme_char( WCHAR c )
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
           || c == '+' || c == '.' || c == '-';
}

static BOOL uri_has_scheme( const WCHAR *str )
{
    const WCHAR *p = str;

    if (!(( *p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z'))) return FALSE;
    for (p++; *p && is_scheme_char(*p); p++);
    return *p == ':';
}

struct windows_uri
{
    IUriRuntimeClass IUriRuntimeClass_iface;
    IUriRuntimeClassWithAbsoluteCanonicalUri IUriRuntimeClassWithAbsoluteCanonicalUri_iface;
    IStringable IStringable_iface;
    LONG ref;

    WCHAR *raw;
    WCHAR *scheme, *user, *password, *host, *path, *query, *fragment, *extension;
    INT32 port;
};

static inline struct windows_uri *impl_from_IUriRuntimeClass( IUriRuntimeClass *iface )
{
    return CONTAINING_RECORD( iface, struct windows_uri, IUriRuntimeClass_iface );
}

static void windows_uri_destroy( struct windows_uri *impl )
{
    heap_free( impl->raw );
    heap_free( impl->scheme );
    heap_free( impl->user );
    heap_free( impl->password );
    heap_free( impl->host );
    heap_free( impl->path );
    heap_free( impl->query );
    heap_free( impl->fragment );
    heap_free( impl->extension );
    heap_free( impl );
}

static void uri_parse( struct windows_uri *impl )
{
    const WCHAR *raw = impl->raw, *p = raw, *scheme_end = NULL, *rest;
    WCHAR *fragment = NULL, *query = NULL, *path;

    impl->port = 80;

    /* fragment, then query: both keep their leading delimiter (Windows semantics) */
    {
        const WCHAR *hash = wcschr( raw, '#' );
        const WCHAR *quest = wcschr( raw, '?' );

        if (hash)
        {
            fragment = heap_strdup( hash );
            if (quest && quest < hash) query = heap_strndup( quest, hash - quest );
            rest = heap_strndup( raw, (quest < hash ? quest : hash) - raw );
        }
        else if (quest)
        {
            query = heap_strdup( quest );
            rest = heap_strndup( raw, quest - raw );
        }
        else rest = heap_strdup( raw );
        p = rest;
    }

    for (scheme_end = p; is_scheme_char(*scheme_end); scheme_end++);
    if (scheme_end == p || *scheme_end != ':') scheme_end = NULL;
    else
    {
        SIZE_T n = scheme_end - p;
        impl->scheme = heap_strndup( p, n );
        impl->port = !wcsncmp( impl->scheme, L"https", 5 ) ? 443 : 80;
    }

    p = scheme_end ? scheme_end + 1 : p;
    if (p[0] == '/' && p[1] == '/')
    {
        const WCHAR *auth = p + 2, *aend = auth, *at = NULL, *colon = NULL, *host;
        SIZE_T host_len;

        while (*aend && *aend != '/') aend++;
        for (p = auth; p < aend; p++)
        {
            if (*p == '@') { at = p; break; }
            if (*p == ':' && !colon) colon = p;
        }
        host = at ? at + 1 : auth;
        if (at)
        {
            const WCHAR *sep = auth;
            while (sep < at && *sep != ':') sep++;
            impl->user = heap_strndup( auth, sep - auth );
            impl->password = sep < at ? heap_strdup( sep + 1 ) : heap_strdup( L"" );
        }
        if (*host == '[')
        {
            const WCHAR *close = host;
            while (close < aend && *close != ']') close++;
            host++;
            host_len = (close < aend ? close : aend) - host;
            colon = NULL;
        }
        else
        {
            const WCHAR *hstop = colon ? colon : aend;
            host_len = hstop - host;
        }
        impl->host = heap_strndup( host, host_len );
        if (colon)
        {
            INT32 value = 0;
            const WCHAR *q = colon + 1;
            while (*q >= '0' && *q <= '9') value = value * 10 + (*q++ - '0');
            impl->port = value;
        }
        path = wcschr( aend, '/' );
    }
    else
    {
        impl->host = heap_strdup( L"" );
        path = (WCHAR *)p;
    }

    impl->path = path && *path ? heap_strdup( path ) : heap_strdup( L"/" );

    /* extension: last dotted suffix of the path */
    {
        const WCHAR *dot = NULL, *q;
        for (q = impl->path; *q; q++)
            if (*q == '.') dot = q;
            else if (*q == '/') dot = NULL;
        impl->extension = dot ? heap_strdup( dot ) : heap_strdup( L"" );
    }

    impl->scheme = impl->scheme ? impl->scheme : heap_strdup( L"" );
    impl->user = impl->user ? impl->user : heap_strdup( L"" );
    impl->password = impl->password ? impl->password : heap_strdup( L"" );
    impl->host = impl->host ? impl->host : heap_strdup( L"" );
    impl->query = query ? query : heap_strdup( L"" );
    impl->fragment = fragment ? fragment : heap_strdup( L"" );
    heap_free( rest );
}

static const IUriRuntimeClassVtbl uri_vtbl;
static const IUriRuntimeClassWithAbsoluteCanonicalUriVtbl uri_acu_vtbl;
static const IStringableVtbl uri_stringable_vtbl;

static HRESULT windows_uri_create( const WCHAR *raw, struct windows_uri **out )
{
    struct windows_uri *impl = heap_calloc( 1, sizeof(*impl) );

    if (!impl) return E_OUTOFMEMORY;

    impl->IUriRuntimeClass_iface.lpVtbl = &uri_vtbl;
    impl->IUriRuntimeClassWithAbsoluteCanonicalUri_iface.lpVtbl = &uri_acu_vtbl;
    impl->IStringable_iface.lpVtbl = &uri_stringable_vtbl;
    impl->ref = 1;
    impl->raw = heap_strdup( raw );
    if (!impl->raw)
    {
        heap_free( impl );
        return E_OUTOFMEMORY;
    }
    uri_parse( impl );

    *out = impl;
    return S_OK;
}

static HRESULT WINAPI uri_QueryInterface( IUriRuntimeClass *iface, REFIID iid, void **out )
{
    struct windows_uri *impl = impl_from_IUriRuntimeClass( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IUriRuntimeClass ))
    {
        IInspectable_AddRef( (*out = &impl->IUriRuntimeClass_iface) );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IUriRuntimeClassWithAbsoluteCanonicalUri ))
    {
        IInspectable_AddRef( (*out = &impl->IUriRuntimeClassWithAbsoluteCanonicalUri_iface) );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IStringable ))
    {
        IInspectable_AddRef( (*out = &impl->IStringable_iface) );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI uri_AddRef( IUriRuntimeClass *iface )
{
    struct windows_uri *impl = impl_from_IUriRuntimeClass( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI uri_Release( IUriRuntimeClass *iface )
{
    struct windows_uri *impl = impl_from_IUriRuntimeClass( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    if (!ref) windows_uri_destroy( impl );
    return ref;
}

static HRESULT WINAPI uri_GetIids( IUriRuntimeClass *iface, ULONG *iid_count, IID **iids )
{
    IID instance_iids[] =
    {
        IID_IUriRuntimeClass,
        IID_IUriRuntimeClassWithAbsoluteCanonicalUri,
        IID_IStringable,
    };
    IID *array = CoTaskMemAlloc( sizeof(instance_iids) );

    TRACE( "iface %p, iid_count %p, iids %p.\n", iface, iid_count, iids );

    if (!array) return E_OUTOFMEMORY;
    memcpy( array, instance_iids, sizeof(instance_iids) );
    *iid_count = ARRAY_SIZE(instance_iids);
    *iids = array;
    return S_OK;
}

static HRESULT WINAPI uri_GetRuntimeClassName( IUriRuntimeClass *iface, HSTRING *class_name )
{
    return hstr_dup( L"Windows.Foundation.Uri", class_name );
}

static HRESULT WINAPI uri_GetTrustLevel( IUriRuntimeClass *iface, TrustLevel *trust_level )
{
    *trust_level = FullTrust;
    return S_OK;
}

#define URI_GETTER(name, field) \
    static HRESULT WINAPI uri_get_##name( IUriRuntimeClass *iface, HSTRING *value ) \
    { \
        struct windows_uri *impl = impl_from_IUriRuntimeClass( iface ); \
        return hstr_dup( impl->field, value ); \
    }

URI_GETTER( RawUri, raw )
URI_GETTER( DisplayUri, raw )
URI_GETTER( SchemeName, scheme )
URI_GETTER( UserName, user )
URI_GETTER( Password, password )
URI_GETTER( Host, host )
URI_GETTER( Domain, host )
URI_GETTER( Path, path )
URI_GETTER( Extension, extension )
URI_GETTER( Fragment, fragment )
URI_GETTER( Query, query )

static HRESULT WINAPI uri_get_AbsoluteUri( IUriRuntimeClass *iface, HSTRING *value )
{
    struct windows_uri *impl = impl_from_IUriRuntimeClass( iface );
    const WCHAR *hash = wcschr( impl->raw, '#' );

    return WindowsCreateString( impl->raw, hash ? hash - impl->raw : lstrlenW( impl->raw ), value );
}

static HRESULT WINAPI uri_get_QueryParsed( IUriRuntimeClass *iface, IWwwFormUrlDecoderRuntimeClass **decoder )
{
    FIXME( "iface %p, decoder %p stub!\n", iface, decoder );
    return E_NOTIMPL;
}

static HRESULT WINAPI uri_get_Port( IUriRuntimeClass *iface, INT32 *value )
{
    struct windows_uri *impl = impl_from_IUriRuntimeClass( iface );
    *value = impl->port;
    return S_OK;
}

static HRESULT WINAPI uri_get_Suspicious( IUriRuntimeClass *iface, boolean *value )
{
    *value = false;
    return S_OK;
}

static HRESULT WINAPI uri_Equals( IUriRuntimeClass *iface, IUriRuntimeClass *uri, boolean *value )
{
    struct windows_uri *impl = impl_from_IUriRuntimeClass( iface );
    HSTRING other = NULL;
    const WCHAR *buffer;
    HRESULT hr;

    *value = false;
    if (!uri) return E_INVALIDARG;

    if (uri == iface)
    {
        *value = true;
        return S_OK;
    }

    hr = IUriRuntimeClass_get_RawUri( uri, &other );
    if (FAILED( hr )) return hr;

    buffer = WindowsGetStringRawBuffer( other, NULL );
    *value = !wcsicmp( impl->raw, buffer );
    WindowsDeleteString( other );
    return S_OK;
}

static HRESULT uri_combine( struct windows_uri *impl, const WCHAR *relative, struct windows_uri **out )
{
    WCHAR *combined;
    SIZE_T dir = 0, rel_len = lstrlenW( relative ), base_len = lstrlenW( impl->raw ), i;

    if (uri_has_scheme( relative )) return windows_uri_create( relative, out );

    /* cut base at query/fragment */
    for (i = 0; i < base_len; i++)
        if (impl->raw[i] == '#' || impl->raw[i] == '?') { base_len = i; break; }

    if (relative[0] == '/')
    {
        /* rooted: keep scheme://authority only */
        SIZE_T n = 0;

        for (i = 0; i + 2 < base_len; i++)
            if (impl->raw[i] == ':' && impl->raw[i + 1] == '/' && impl->raw[i + 2] == '/')
            {
                n = i + 3;
                break;
            }
        while (n < base_len && impl->raw[n] != '/') n++;
        dir = n;
    }
    else
    {
        /* relative: drop base path last segment */
        SIZE_T n = base_len;
        while (n && impl->raw[n - 1] != '/') n--;
        dir = n;
    }

    combined = heap_alloc( (dir + rel_len + 2) * sizeof(WCHAR) );
    if (!combined) return E_OUTOFMEMORY;
    memcpy( combined, impl->raw, dir * sizeof(WCHAR) );
    if (dir && combined[dir - 1] != '/' && relative[0] != '/') combined[dir++] = '/';
    memcpy( combined + dir, relative, (rel_len + 1) * sizeof(WCHAR) );

    {
        HRESULT hr = windows_uri_create( combined, out );
        heap_free( combined );
        return hr;
    }
}

static HRESULT WINAPI uri_CombineUri( IUriRuntimeClass *iface, HSTRING relative_uri,
                                      IUriRuntimeClass **instance )
{
    struct windows_uri *impl = impl_from_IUriRuntimeClass( iface );
    struct windows_uri *result;
    const WCHAR *buffer = WindowsGetStringRawBuffer( relative_uri, NULL );
    HRESULT hr;

    TRACE( "iface %p, relative_uri %s, instance %p.\n", iface, debugstr_hstring( relative_uri ), instance );

    *instance = NULL;
    hr = uri_combine( impl, buffer, &result );
    if (SUCCEEDED( hr )) *instance = &result->IUriRuntimeClass_iface;
    return hr;
}

static const IUriRuntimeClassVtbl uri_vtbl =
{
    uri_QueryInterface,
    uri_AddRef,
    uri_Release,
    uri_GetIids,
    uri_GetRuntimeClassName,
    uri_GetTrustLevel,
    uri_get_AbsoluteUri,
    uri_get_DisplayUri,
    uri_get_Domain,
    uri_get_Extension,
    uri_get_Fragment,
    uri_get_Host,
    uri_get_Password,
    uri_get_Path,
    uri_get_Query,
    uri_get_QueryParsed,
    uri_get_RawUri,
    uri_get_SchemeName,
    uri_get_UserName,
    uri_get_Port,
    uri_get_Suspicious,
    uri_Equals,
    uri_CombineUri,
};

static inline struct windows_uri *impl_from_IUriRuntimeClassWithAbsoluteCanonicalUri(
        IUriRuntimeClassWithAbsoluteCanonicalUri *iface );

DEFINE_IINSPECTABLE( uri_acu, IUriRuntimeClassWithAbsoluteCanonicalUri, struct windows_uri,
                     IUriRuntimeClass_iface )

static HRESULT WINAPI uri_acu_get_AbsoluteCanonicalUri( IUriRuntimeClassWithAbsoluteCanonicalUri *iface,
                                                        HSTRING *value )
{
    struct windows_uri *impl = impl_from_IUriRuntimeClassWithAbsoluteCanonicalUri( iface );
    return hstr_dup( impl->raw, value );
}

static HRESULT WINAPI uri_acu_get_DisplayIri( IUriRuntimeClassWithAbsoluteCanonicalUri *iface, HSTRING *value )
{
    struct windows_uri *impl = impl_from_IUriRuntimeClassWithAbsoluteCanonicalUri( iface );
    return hstr_dup( impl->raw, value );
}

static const IUriRuntimeClassWithAbsoluteCanonicalUriVtbl uri_acu_vtbl =
{
    uri_acu_QueryInterface,
    uri_acu_AddRef,
    uri_acu_Release,
    uri_acu_GetIids,
    uri_acu_GetRuntimeClassName,
    uri_acu_GetTrustLevel,
    uri_acu_get_AbsoluteCanonicalUri,
    uri_acu_get_DisplayIri,
};

DEFINE_IINSPECTABLE( uri_stringable, IStringable, struct windows_uri, IUriRuntimeClass_iface )

static HRESULT WINAPI uri_stringable_ToString( IStringable *iface, HSTRING *value )
{
    struct windows_uri *impl = impl_from_IStringable( iface );

    TRACE( "iface %p, value %p.\n", iface, value );

    return hstr_dup( impl->raw, value );
}

static const IStringableVtbl uri_stringable_vtbl =
{
    uri_stringable_QueryInterface,
    uri_stringable_AddRef,
    uri_stringable_Release,
    uri_stringable_GetIids,
    uri_stringable_GetRuntimeClassName,
    uri_stringable_GetTrustLevel,
    uri_stringable_ToString,
};

struct uri_statics
{
    IActivationFactory IActivationFactory_iface;
    IUriRuntimeClassFactory IUriRuntimeClassFactory_iface;
    IUriEscapeStatics IUriEscapeStatics_iface;
    IUriRuntimeClass IUriRuntimeClass_iface;
    IUriRuntimeClassWithAbsoluteCanonicalUri IUriRuntimeClassWithAbsoluteCanonicalUri_iface;
    IStringable IStringable_iface;
    LONG ref;
};

static inline struct uri_statics *impl_from_IActivationFactory( IActivationFactory *iface )
{
    return CONTAINING_RECORD( iface, struct uri_statics, IActivationFactory_iface );
}

static HRESULT WINAPI uri_statics_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
    struct uri_statics *impl = impl_from_IActivationFactory( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ))
    {
        IInspectable_AddRef( (*out = &impl->IActivationFactory_iface) );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IUriRuntimeClassFactory ))
    {
        IInspectable_AddRef( (*out = &impl->IUriRuntimeClassFactory_iface) );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IUriEscapeStatics ))
    {
        IInspectable_AddRef( (*out = &impl->IUriEscapeStatics_iface) );
        return S_OK;
    }

    /*
     * Windows's class factory aggregates the runtime class default interface and
     * its implemented interfaces: QI for the default IID on the factory object
     * succeeds (CsWinRT probes the factory for the class interfaces to build its
     * ABI surface; RoGetActivationFactory("Windows.Foundation.Uri") +
     * IInspectable_QueryInterface(IID_IUriRuntimeClass) is the measured app path).
     */
    if (IsEqualGUID( iid, &IID_IUriRuntimeClass ))
    {
        IInspectable_AddRef( (*out = &impl->IUriRuntimeClass_iface) );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IUriRuntimeClassWithAbsoluteCanonicalUri ))
    {
        IInspectable_AddRef( (*out = &impl->IUriRuntimeClassWithAbsoluteCanonicalUri_iface) );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IStringable ))
    {
        IInspectable_AddRef( (*out = &impl->IStringable_iface) );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI uri_statics_AddRef( IActivationFactory *iface )
{
    struct uri_statics *impl = impl_from_IActivationFactory( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI uri_statics_Release( IActivationFactory *iface )
{
    struct uri_statics *impl = impl_from_IActivationFactory( iface );
    return InterlockedDecrement( &impl->ref );
}

static HRESULT WINAPI uri_statics_GetIids( IActivationFactory *iface, ULONG *iid_count, IID **iids )
{
    IID factory_iids[] =
    {
        IID_IActivationFactory,
        IID_IUriRuntimeClassFactory,
        IID_IUriEscapeStatics,
        IID_IUriRuntimeClass,
        IID_IUriRuntimeClassWithAbsoluteCanonicalUri,
        IID_IStringable,
    };
    IID *array = CoTaskMemAlloc( sizeof(factory_iids) );

    TRACE( "iface %p, iid_count %p, iids %p.\n", iface, iid_count, iids );

    if (!array) return E_OUTOFMEMORY;
    memcpy( array, factory_iids, sizeof(factory_iids) );
    *iid_count = ARRAY_SIZE(factory_iids);
    *iids = array;
    return S_OK;
}

static HRESULT WINAPI uri_statics_GetRuntimeClassName( IActivationFactory *iface, HSTRING *class_name )
{
    return hstr_dup( L"Windows.Foundation.Uri", class_name );
}

static HRESULT WINAPI uri_statics_GetTrustLevel( IActivationFactory *iface, TrustLevel *trust_level )
{
    *trust_level = FullTrust;
    return S_OK;
}

static HRESULT WINAPI uri_statics_ActivateInstance( IActivationFactory *iface, IInspectable **instance )
{
    FIXME( "iface %p, instance %p stub!\n", iface, instance );
    *instance = NULL;
    return E_FAIL;
}

static const IActivationFactoryVtbl uri_statics_vtbl =
{
    uri_statics_QueryInterface,
    uri_statics_AddRef,
    uri_statics_Release,
    uri_statics_GetIids,
    uri_statics_GetRuntimeClassName,
    uri_statics_GetTrustLevel,
    uri_statics_ActivateInstance,
};

DEFINE_IINSPECTABLE_( uri_factory, IUriRuntimeClassFactory, struct uri_statics, impl_from_IUriRuntimeClassFactory,
                     IUriRuntimeClassFactory_iface, &impl->IActivationFactory_iface )

static HRESULT WINAPI uri_factory_CreateUri( IUriRuntimeClassFactory *iface, HSTRING uri,
                                             IUriRuntimeClass **instance )
{
    struct windows_uri *impl;
    const WCHAR *buffer = WindowsGetStringRawBuffer( uri, NULL );
    HRESULT hr;

    TRACE( "iface %p, uri %s, instance %p.\n", iface, debugstr_hstring( uri ), instance );

    *instance = NULL;
    if (!buffer) return E_INVALIDARG;
    hr = windows_uri_create( buffer, &impl );
    if (SUCCEEDED( hr )) *instance = &impl->IUriRuntimeClass_iface;
    return hr;
}

static HRESULT WINAPI uri_factory_CreateWithRelativeUri( IUriRuntimeClassFactory *iface, HSTRING base_uri,
                                                         HSTRING relative_uri,
                                                         IUriRuntimeClass **instance )
{
    struct windows_uri *base, *result;
    const WCHAR *buffer = WindowsGetStringRawBuffer( base_uri, NULL );
    const WCHAR *relative = WindowsGetStringRawBuffer( relative_uri, NULL );
    HRESULT hr;

    TRACE( "iface %p, base_uri %s, relative_uri %s, instance %p.\n", iface,
           debugstr_hstring( base_uri ), debugstr_hstring( relative_uri ), instance );

    *instance = NULL;
    if (!buffer || !relative) return E_INVALIDARG;
    hr = windows_uri_create( buffer, &base );
    if (FAILED( hr )) return hr;
    hr = uri_combine( base, relative, &result );
    IUriRuntimeClass_Release( &base->IUriRuntimeClass_iface );
    if (SUCCEEDED( hr )) *instance = &result->IUriRuntimeClass_iface;
    return hr;
}

static const IUriRuntimeClassFactoryVtbl uri_factory_vtbl =
{
    uri_factory_QueryInterface,
    uri_factory_AddRef,
    uri_factory_Release,
    uri_factory_GetIids,
    uri_factory_GetRuntimeClassName,
    uri_factory_GetTrustLevel,
    uri_factory_CreateUri,
    uri_factory_CreateWithRelativeUri,
};

DEFINE_IINSPECTABLE_( uri_escape, IUriEscapeStatics, struct uri_statics, impl_from_IUriEscapeStatics,
                     IUriEscapeStatics_iface, &impl->IActivationFactory_iface )

static HRESULT WINAPI uri_escape_UnescapeComponent( IUriEscapeStatics *iface, HSTRING to_unescape,
                                                    HSTRING *value )
{
    const WCHAR *buffer = WindowsGetStringRawBuffer( to_unescape, NULL );
    CHAR *tmp;
    SIZE_T len, i, o = 0;
    HRESULT hr;

    TRACE( "iface %p, to_unescape %s, value %p.\n", iface, debugstr_hstring( to_unescape ), value );

    len = lstrlenW( buffer );
    if (!(tmp = heap_alloc( len + 1 ))) return E_OUTOFMEMORY;
    for (i = 0; i < len;)
    {
        if (buffer[i] == '%' && i + 2 < len + 1) 
        {
            WCHAR hex[3] = {buffer[i + 1], buffer[i + 2], 0};
            int v = 0, k;
            for (k = 0; k < 2; k++)
            {
                WCHAR c = hex[k];
                v <<= 4;
                if (c >= '0' && c <= '9') v += c - '0';
                else if (c >= 'a' && c <= 'f') v += c - 'a' + 10;
                else if (c >= 'A' && c <= 'F') v += c - 'A' + 10;
            }
            tmp[o++] = (CHAR)v;
            i += 3;
        }
        else tmp[o++] = (CHAR)buffer[i++];
    }
    tmp[o] = 0;

    /* the byte sequence is UTF-8; decode it back to wide characters */
    {
        WCHAR *wide;
        INT wlen = MultiByteToWideChar( CP_UTF8, 0, tmp, o, NULL, 0 );

        if (wlen <= 0)
        {
            heap_free( tmp );
            return E_INVALIDARG;
        }
        wide = heap_alloc( wlen * sizeof(WCHAR) );
        if (!wide)
        {
            heap_free( tmp );
            return E_OUTOFMEMORY;
        }
        MultiByteToWideChar( CP_UTF8, 0, tmp, o, wide, wlen );
        hr = WindowsCreateString( wide, wlen, value );
        heap_free( wide );
    }
    heap_free( tmp );
    return hr;
}

static HRESULT WINAPI uri_escape_EscapeComponent( IUriEscapeStatics *iface, HSTRING to_escape, HSTRING *value )
{
    const WCHAR *buffer = WindowsGetStringRawBuffer( to_escape, NULL );
    SIZE_T len = lstrlenW( buffer ), i;
    WCHAR *tmp, *o;
    HRESULT hr;

    TRACE( "iface %p, to_escape %s, value %p.\n", iface, debugstr_hstring( to_escape ), value );

    tmp = heap_alloc( (len * 9 + 1) * sizeof(WCHAR) );
    if (!tmp) return E_OUTOFMEMORY;
    o = tmp;
    for (i = 0; i < len; i++)
    {
        WCHAR c = buffer[i];
        BOOL unreserved = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
                          || c == '-' || c == '.' || c == '_' || c == '~';
        if (unreserved)
        {
            *o++ = c;
        }
        else if (c <= 0x7f)
        {
            *o++ = '%';
            *o++ = hex_chars[c >> 4];
            *o++ = hex_chars[c & 0xf];
        }
        else
        {
            CHAR utf8[3];
            INT n = WideCharToMultiByte( CP_UTF8, 0, &c, 1, utf8, sizeof(utf8), NULL, NULL );
            INT k;
            for (k = 0; k < n; k++)
            {
                *o++ = '%';
                *o++ = hex_chars[(UCHAR)utf8[k] >> 4];
                *o++ = hex_chars[(UCHAR)utf8[k] & 0xf];
            }
        }
    }
    *o = 0;
    hr = WindowsCreateString( tmp, o - tmp, value );
    heap_free( tmp );
    return hr;
}

static const IUriEscapeStaticsVtbl uri_escape_vtbl =
{
    uri_escape_QueryInterface,
    uri_escape_AddRef,
    uri_escape_Release,
    uri_escape_GetIids,
    uri_escape_GetRuntimeClassName,
    uri_escape_GetTrustLevel,
    uri_escape_UnescapeComponent,
    uri_escape_EscapeComponent,
};

/*
 * The factory answers the class default interface and implemented interfaces
 * (Windows aggregates them on the factory object). The factory is not an
 * instance: QI, reference counting and the inspectable metadata probe surface
 * are live, instance method calls fail.
 */
DEFINE_IINSPECTABLE_( uri_statics_def, IUriRuntimeClass, struct uri_statics, impl_from_uri_statics_def,
                      IUriRuntimeClass_iface, &impl->IActivationFactory_iface )

#define URI_FACTORY_INSTANCE_STUB( name, proto ) \
    static HRESULT WINAPI uri_statics_def_##name proto { return E_FAIL; }

URI_FACTORY_INSTANCE_STUB( get_AbsoluteUri, ( IUriRuntimeClass *iface, HSTRING *value ) )
URI_FACTORY_INSTANCE_STUB( get_DisplayUri, ( IUriRuntimeClass *iface, HSTRING *value ) )
URI_FACTORY_INSTANCE_STUB( get_Domain, ( IUriRuntimeClass *iface, HSTRING *value ) )
URI_FACTORY_INSTANCE_STUB( get_Extension, ( IUriRuntimeClass *iface, HSTRING *value ) )
URI_FACTORY_INSTANCE_STUB( get_Fragment, ( IUriRuntimeClass *iface, HSTRING *value ) )
URI_FACTORY_INSTANCE_STUB( get_Host, ( IUriRuntimeClass *iface, HSTRING *value ) )
URI_FACTORY_INSTANCE_STUB( get_Password, ( IUriRuntimeClass *iface, HSTRING *value ) )
URI_FACTORY_INSTANCE_STUB( get_Path, ( IUriRuntimeClass *iface, HSTRING *value ) )
URI_FACTORY_INSTANCE_STUB( get_Query, ( IUriRuntimeClass *iface, HSTRING *value ) )
URI_FACTORY_INSTANCE_STUB( get_QueryParsed, ( IUriRuntimeClass *iface, IWwwFormUrlDecoderRuntimeClass **decoder ) )
URI_FACTORY_INSTANCE_STUB( get_RawUri, ( IUriRuntimeClass *iface, HSTRING *value ) )
URI_FACTORY_INSTANCE_STUB( get_SchemeName, ( IUriRuntimeClass *iface, HSTRING *value ) )
URI_FACTORY_INSTANCE_STUB( get_UserName, ( IUriRuntimeClass *iface, HSTRING *value ) )
URI_FACTORY_INSTANCE_STUB( get_Port, ( IUriRuntimeClass *iface, INT32 *value ) )
URI_FACTORY_INSTANCE_STUB( get_Suspicious, ( IUriRuntimeClass *iface, boolean *value ) )
URI_FACTORY_INSTANCE_STUB( Equals, ( IUriRuntimeClass *iface, IUriRuntimeClass *uri, boolean *value ) )
URI_FACTORY_INSTANCE_STUB( CombineUri, ( IUriRuntimeClass *iface, HSTRING relative_uri, IUriRuntimeClass **instance ) )

static const IUriRuntimeClassVtbl uri_statics_def_vtbl =
{
    uri_statics_def_QueryInterface,
    uri_statics_def_AddRef,
    uri_statics_def_Release,
    uri_statics_def_GetIids,
    uri_statics_def_GetRuntimeClassName,
    uri_statics_def_GetTrustLevel,
    uri_statics_def_get_AbsoluteUri,
    uri_statics_def_get_DisplayUri,
    uri_statics_def_get_Domain,
    uri_statics_def_get_Extension,
    uri_statics_def_get_Fragment,
    uri_statics_def_get_Host,
    uri_statics_def_get_Password,
    uri_statics_def_get_Path,
    uri_statics_def_get_Query,
    uri_statics_def_get_QueryParsed,
    uri_statics_def_get_RawUri,
    uri_statics_def_get_SchemeName,
    uri_statics_def_get_UserName,
    uri_statics_def_get_Port,
    uri_statics_def_get_Suspicious,
    uri_statics_def_Equals,
    uri_statics_def_CombineUri,
};

DEFINE_IINSPECTABLE_( uri_statics_acu, IUriRuntimeClassWithAbsoluteCanonicalUri, struct uri_statics,
                      impl_from_uri_statics_acu, IUriRuntimeClassWithAbsoluteCanonicalUri_iface,
                      &impl->IActivationFactory_iface )

URI_FACTORY_INSTANCE_STUB( acu_get_AbsoluteCanonicalUri,
                           ( IUriRuntimeClassWithAbsoluteCanonicalUri *iface, HSTRING *value ) )
URI_FACTORY_INSTANCE_STUB( acu_get_DisplayIri,
                           ( IUriRuntimeClassWithAbsoluteCanonicalUri *iface, HSTRING *value ) )

static const IUriRuntimeClassWithAbsoluteCanonicalUriVtbl uri_statics_acu_vtbl =
{
    uri_statics_acu_QueryInterface,
    uri_statics_acu_AddRef,
    uri_statics_acu_Release,
    uri_statics_acu_GetIids,
    uri_statics_acu_GetRuntimeClassName,
    uri_statics_acu_GetTrustLevel,
    uri_statics_def_acu_get_AbsoluteCanonicalUri,
    uri_statics_def_acu_get_DisplayIri,
};

DEFINE_IINSPECTABLE_( uri_statics_stringable, IStringable, struct uri_statics, impl_from_uri_statics_stringable,
                      IStringable_iface, &impl->IActivationFactory_iface )

URI_FACTORY_INSTANCE_STUB( stringable_ToString, ( IStringable *iface, HSTRING *value ) )

static const IStringableVtbl uri_statics_stringable_vtbl =
{
    uri_statics_stringable_QueryInterface,
    uri_statics_stringable_AddRef,
    uri_statics_stringable_Release,
    uri_statics_stringable_GetIids,
    uri_statics_stringable_GetRuntimeClassName,
    uri_statics_stringable_GetTrustLevel,
    uri_statics_def_stringable_ToString,
};

static struct uri_statics uri_statics =
{
    {&uri_statics_vtbl},
    {&uri_factory_vtbl},
    {&uri_escape_vtbl},
    {&uri_statics_def_vtbl},
    {&uri_statics_acu_vtbl},
    {&uri_statics_stringable_vtbl},
    1,
};

IActivationFactory *uri_activation_factory = &uri_statics.IActivationFactory_iface;
