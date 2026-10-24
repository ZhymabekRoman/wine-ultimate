/* WinRT Windows.ApplicationModel.AppInfo Implementation
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "private.h"

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(model);

static const WCHAR manifest_name[] = L"AppxManifest.xml";
static const WCHAR ms_appx_prefix[] = L"ms-appx:///";

/* ------------------------------------------------------------------------- *
 * Package identity
 *
 * AppInfo describes the package the process was launched from.  The process
 * identity is published through the WINE_PACKAGE_* environment variables (see
 * the package identity support in kernelbase), so an unpackaged process
 * reports empty strings the way a real unpackaged process does.
 * ------------------------------------------------------------------------- */

static WCHAR *get_env_dup( const WCHAR *name )
{
    WCHAR buf[MAX_PATH];
    DWORD len = GetEnvironmentVariableW( name, buf, ARRAY_SIZE(buf) );

    if (!len || len >= ARRAY_SIZE(buf)) return NULL;
    return wcsdup( buf );
}

/* A package full name is '<name>_<version>_<arch>__<publisher>', the family
 * name keeps only '<name>_<publisher>'. */
static WCHAR *derive_family_name( const WCHAR *full_name )
{
    const WCHAR *sep, *hash;
    WCHAR *family;
    SIZE_T name_len, hash_len;

    if (!(sep = wcschr( full_name, '_' ))) return NULL;
    if (!(hash = wcsstr( sep, L"__" ))) return NULL;
    hash += 2;
    name_len = sep - full_name;
    hash_len = lstrlenW( hash );
    if (!name_len || !hash_len) return NULL;

    if (!(family = malloc( (name_len + 1 + hash_len + 1) * sizeof(*family) ))) return NULL;
    memcpy( family, full_name, name_len * sizeof(*family) );
    family[name_len] = '_';
    memcpy( family + name_len + 1, hash, hash_len * sizeof(*family) );
    family[name_len + 1 + hash_len] = 0;
    return family;
}

static WCHAR *get_package_family_name(void)
{
    WCHAR *full_name, *family;

    if ((family = get_env_dup( L"WINE_PACKAGE_FAMILY_NAME" ))) return family;
    if ((full_name = get_env_dup( L"WINE_PACKAGE_FULL_NAME" )))
    {
        family = derive_family_name( full_name );
        free( full_name );
    }
    return family;
}

/* ------------------------------------------------------------------------- *
 * AppxManifest.xml reading
 *
 * The application id, the display name, the description, the logo and the
 * declared file extensions of an application all come out of the manifest of
 * the installed package.
 * ------------------------------------------------------------------------- */

static WCHAR *get_manifest_path(void)
{
    WCHAR *dir, *path;

    if (!(dir = get_env_dup( L"WINE_PACKAGE_PATH" ))) return NULL;
    if (!(path = malloc( (lstrlenW( dir ) + 2 + ARRAY_SIZE(manifest_name)) * sizeof(*path) )))
    {
        free( dir );
        return NULL;
    }
    wcscpy( path, dir );
    if (path[lstrlenW( path ) - 1] != '\\') wcscat( path, L"\\" );
    wcscat( path, manifest_name );
    free( dir );
    return path;
}

/* Read a whole text file, UTF-8 or UTF-16, as UTF-8. */
static char *read_manifest_text( const WCHAR *path )
{
    DWORD size, read, utf8_len;
    WCHAR *wide;
    char *utf8 = NULL;
    HANDLE file;

    if ((file = CreateFileW( path, GENERIC_READ, FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE, NULL,
                             OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL )) == INVALID_HANDLE_VALUE)
        return NULL;

    if (!(size = GetFileSize( file, NULL )) || size == INVALID_FILE_SIZE
        || !(wide = malloc( (size + sizeof(*wide)) * sizeof(*wide) )))
    {
        CloseHandle( file );
        return NULL;
    }
    if (!ReadFile( file, wide, size, &read, NULL ) || read != size)
    {
        free( wide );
        CloseHandle( file );
        return NULL;
    }
    CloseHandle( file );
    wide[read / sizeof(*wide)] = 0;
    if (wide[0] == 0xfeff) memmove( wide, wide + 1, lstrlenW( wide ) * sizeof(*wide) );

    if ((utf8_len = WideCharToMultiByte( CP_UTF8, 0, wide, -1, NULL, 0, NULL, NULL ))
        && (utf8 = malloc( utf8_len )))
        WideCharToMultiByte( CP_UTF8, 0, wide, -1, utf8, utf8_len, NULL, NULL );
    free( wide );
    return utf8;
}

static WCHAR *utf8_to_wide( const char *utf8 )
{
    DWORD len;
    WCHAR *wide;

    if (!(len = MultiByteToWideChar( CP_UTF8, 0, utf8, -1, NULL, 0 ))) return NULL;
    if (!(wide = malloc( len * sizeof(*wide) ))) return NULL;
    MultiByteToWideChar( CP_UTF8, 0, utf8, -1, wide, len );
    return wide;
}

static char *wide_to_utf8( const WCHAR *wide )
{
    DWORD len;
    char *utf8;

    if (!(len = WideCharToMultiByte( CP_UTF8, 0, wide, -1, NULL, 0, NULL, NULL ))) return NULL;
    if (!(utf8 = malloc( len ))) return NULL;
    WideCharToMultiByte( CP_UTF8, 0, wide, -1, utf8, len, NULL, NULL );
    return utf8;
}

/* Return the pointer past the '>' of the tag starting at 'start', respecting
 * quoted attribute values. */
static const char *tag_end( const char *start )
{
    BOOL quoted = FALSE;
    const char *p = start;

    for (; *p; p++)
    {
        if (*p == '"') quoted = !quoted;
        else if (*p == '>' && !quoted) return p + 1;
    }
    return NULL;
}

static const char *tag_name_end( const char *tag )
{
    const char *p = tag + 1;

    while (*p && !isspace( (unsigned char)*p ) && *p != '>' && *p != '/') p++;
    return p;
}

static BOOL tag_is_opening( const char *tag, const char *end )
{
    return *tag_name_end( tag ) != '/' && end[-1] != '/';
}

/* Copy the value of the 'attr' attribute of the tag starting at 'tag'. */
static BOOL tag_get_attr( const char *tag, const char *attr, char *out, DWORD size )
{
    const char *end = tag_end( tag ), *p = tag + 1;
    SIZE_T attr_len = strlen( attr ), len;

    if (!end) return FALSE;
    while (p < end)
    {
        p += strspn( p, " \t\r\n/" );
        if (p >= end || *p == '>') break;
        len = strcspn( p, "=" );
        if (!len || p[len] != '=') break;
        /* trim trailing whitespace of the attribute name */
        {
            SIZE_T n = len;
            while (n && isspace( (unsigned char)p[n - 1] )) n--;
            if (n == attr_len && !strncmp( p, attr, attr_len ) && p[len + 1] == '"')
            {
                p += len + 2;
                if (!(len = strcspn( p, "\"" )) || len >= size) return FALSE;
                memcpy( out, p, len );
                out[len] = 0;
                return TRUE;
            }
        }
        p += len + 1;
        if (*p == '"')
        {
            if (!(p = strchr( p, '"' )) || p >= end) return FALSE;
            p++;
        }
    }
    return FALSE;
}

/* Find the next opening '<name' tag in [from,limit) whose 'attr' attribute
 * equals 'value' (any opening tag when 'attr' is NULL). */
static const char *find_tag( const char *from, const char *limit, const char *name,
                             const char *attr, const char *value )
{
    char needle[128], got[1024];

    snprintf( needle, ARRAY_SIZE(needle), "<%s", name );
    while (from < limit && (from = strstr( from, needle )) && from < limit)
    {
        const char *end = tag_end( from );

        if (!end || end > limit) return NULL;
        if (tag_is_opening( from, end ))
        {
            if (!attr) return from;
            if (tag_get_attr( from, attr, got, ARRAY_SIZE(got) ) && !strcmp( got, value )) return from;
        }
        from = end;
    }
    return NULL;
}

/* End of the content of the element that opens at 'tag' (its '</name' tag),
 * or the end of the buffer when the element is not closed. */
static const char *element_content_end( const char *tag, const char *name )
{
    char closing[128];
    const char *start = tag_end( tag ), *p;

    if (!start) return NULL;
    snprintf( closing, ARRAY_SIZE(closing), "</%s", name );
    if ((p = strstr( start, closing ))) return p;
    return start + strlen( start );
}

/* The Application element of the manifest, the one with Id 'app_id' when
 * given, the first one otherwise. */
static const char *find_application_tag( const char *text, const char *app_id )
{
    const char *end = text + strlen( text );

    if (app_id)
    {
        const char *app = find_tag( text, end, "Application", "Id", app_id );

        if (app) return app;
    }
    return find_tag( text, end, "Application", NULL, NULL );
}

/* Attribute of the uap:VisualElements element of the given application. */
static char *get_visual_elements_attr( const char *text, const char *app_id, const char *attr )
{
    const char *app, *app_end, *visual;
    char value[1024];

    if (!(app = find_application_tag( text, app_id )) && !(app = find_application_tag( text, NULL ))) return NULL;
    if (!(app_end = element_content_end( app, "Application" ))) return NULL;
    if (!(visual = find_tag( tag_end( app ), app_end, "uap:VisualElements", NULL, NULL ))) return NULL;
    if (!tag_get_attr( visual, attr, value, ARRAY_SIZE(value) )) return NULL;

    {
        char *out = malloc( strlen( value ) + 1 );
        if (out) strcpy( out, value );
        return out;
    }
}

/* Text of the first '<name>text</name>' child element of the element at 'tag'. */
static char *get_element_text( const char *tag, const char *name )
{
    char needle[128], closing[130];
    const char *start, *end, *text;
    SIZE_T len;
    char *out;

    if (!(start = tag_end( tag ))) return NULL;
    snprintf( needle, ARRAY_SIZE(needle), "<%s>", name );
    snprintf( closing, ARRAY_SIZE(closing), "</%s>", name );
    if (!(text = strstr( start, needle ))) return NULL;
    text += strlen( needle );
    if (!(end = strstr( text, closing ))) return NULL;
    while (text < end && isspace( (unsigned char)*text )) text++;
    len = end - text;
    while (len && isspace( (unsigned char)text[len - 1] )) len--;
    if (!len || !(out = malloc( len + 1 ))) return NULL;
    memcpy( out, text, len );
    out[len] = 0;
    return out;
}

/* Collect the Name attribute of every uap:FileType the application declares. */
static HRESULT get_application_file_types( const char *text, const char *app_id, HSTRING **out, UINT32 *count )
{
    const char *app, *app_end, *p;
    HSTRING values[MAX_PATH];
    UINT32 n = 0, i;

    *out = NULL;
    *count = 0;
    if (!(app = find_application_tag( text, app_id )) && !(app = find_application_tag( text, NULL ))) return S_OK;
    if (!(app_end = element_content_end( app, "Application" ))) return S_OK;

    for (p = tag_end( app ); p && p < app_end && (p = strstr( p, "<uap:FileType" )) && p < app_end;)
    {
        const char *end = tag_end( p );
        char name[256];

        if (!end || end > app_end) break;
        if (tag_is_opening( p, end ) && tag_get_attr( p, "Name", name, ARRAY_SIZE(name) ) && n < ARRAY_SIZE(values))
        {
            WCHAR *wide = utf8_to_wide( name );

            if (wide)
            {
                if (SUCCEEDED( WindowsCreateString( wide, lstrlenW( wide ), &values[n] ))) n++;
                free( wide );
            }
        }
        p = end;
    }
    if (!n) return S_OK;
    if (!(*out = CoTaskMemAlloc( n * sizeof(**out) )))
    {
        while (n) WindowsDeleteString( values[--n] );
        return E_OUTOFMEMORY;
    }
    for (i = 0; i < n; i++) (*out)[i] = values[i];
    *count = n;
    return S_OK;
}

/* ------------------------------------------------------------------------- *
 * AppDisplayInfo
 * ------------------------------------------------------------------------- */

struct app_display_info
{
    IAppDisplayInfo IAppDisplayInfo_iface;
    WCHAR *app_id;
    LONG ref;
};

static inline struct app_display_info *impl_from_IAppDisplayInfo( IAppDisplayInfo *iface )
{
    return CONTAINING_RECORD( iface, struct app_display_info, IAppDisplayInfo_iface );
}

static HRESULT WINAPI app_display_info_QueryInterface( IAppDisplayInfo *iface, REFIID iid, void **out )
{
    struct app_display_info *impl = impl_from_IAppDisplayInfo( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IAppDisplayInfo ))
    {
        *out = &impl->IAppDisplayInfo_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI app_display_info_AddRef( IAppDisplayInfo *iface )
{
    struct app_display_info *impl = impl_from_IAppDisplayInfo( iface );

    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI app_display_info_Release( IAppDisplayInfo *iface )
{
    struct app_display_info *impl = impl_from_IAppDisplayInfo( iface );
    LONG ref = InterlockedDecrement( &impl->ref );

    if (!ref)
    {
        free( impl->app_id );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI app_display_info_GetIids( IAppDisplayInfo *iface, ULONG *iid_count, IID **iids )
{
    IID iid_array[] =
    {
        IID_IAppDisplayInfo,
    };
    IID *array = CoTaskMemAlloc( sizeof(iid_array) );

    if (!array) return E_OUTOFMEMORY;
    memcpy( array, iid_array, sizeof(iid_array) );
    *iid_count = ARRAY_SIZE(iid_array);
    *iids = array;
    return S_OK;
}

static HRESULT WINAPI app_display_info_GetRuntimeClassName( IAppDisplayInfo *iface, HSTRING *class_name )
{
    return WindowsCreateString( RuntimeClass_Windows_ApplicationModel_AppDisplayInfo,
                                lstrlenW( RuntimeClass_Windows_ApplicationModel_AppDisplayInfo ), class_name );
}

static HRESULT WINAPI app_display_info_GetTrustLevel( IAppDisplayInfo *iface, TrustLevel *trust_level )
{
    if (trust_level) *trust_level = BaseTrust;
    return S_OK;
}

/* The id of the application the display info describes, as UTF-8 for the
 * manifest lookups. */
static char *display_info_app_id( struct app_display_info *impl )
{
    return impl->app_id ? wide_to_utf8( impl->app_id ) : NULL;
}

/* Read 'attr' of the uap:VisualElements element of this application. */
static HRESULT get_display_info_string( struct app_display_info *impl, const char *attr, HSTRING *value )
{
    WCHAR *path;
    char *app_id, *text, *result = NULL;
    HRESULT hr = S_OK;

    *value = NULL;
    if (!(path = get_manifest_path() )) return S_OK;
    if ((text = read_manifest_text( path )))
    {
        app_id = display_info_app_id( impl );
        result = get_visual_elements_attr( text, app_id, attr );
        if (!result)
        {
            const char *properties = strstr( text, "<Properties" );

            if (properties) result = get_element_text( properties, "Description" );
        }
        free( app_id );
        if (result)
        {
            WCHAR *wide = utf8_to_wide( result );

            if (wide)
            {
                hr = WindowsCreateString( wide, lstrlenW( wide ), value );
                free( wide );
            }
            free( result );
        }
        free( text );
    }
    free( path );
    return hr;
}

static HRESULT WINAPI app_display_info_get_DisplayName( IAppDisplayInfo *iface, HSTRING *value )
{
    struct app_display_info *impl = impl_from_IAppDisplayInfo( iface );

    TRACE( "iface %p, value %p\n", iface, value );
    if (!value) return E_INVALIDARG;
    return get_display_info_string( impl, "DisplayName", value );
}

static HRESULT WINAPI app_display_info_get_Description( IAppDisplayInfo *iface, HSTRING *value )
{
    struct app_display_info *impl = impl_from_IAppDisplayInfo( iface );

    TRACE( "iface %p, value %p\n", iface, value );
    if (!value) return E_INVALIDARG;
    return get_display_info_string( impl, "Description", value );
}

/* Ask combase for the activation factory of a runtime class. */
static HRESULT get_class_factory( const WCHAR *class_name, REFIID iid, void **out )
{
    HSTRING str = NULL;
    HRESULT hr;

    *out = NULL;
    if (FAILED(hr = WindowsCreateString( class_name, lstrlenW( class_name ), &str ))) return hr;
    hr = RoGetActivationFactory( str, iid, out );
    WindowsDeleteString( str );
    return hr;
}

/* Build a stream reference for a package relative resource, the way Windows
 * hands out the app logo as a 'ms-appx:///' stream. */
static HRESULT create_package_stream_reference( const WCHAR *relative_path, IRandomAccessStreamReference **out )
{
    IRandomAccessStreamReferenceStatics *statics = NULL;
    IUriRuntimeClassFactory *uri_factory = NULL;
    IUriRuntimeClass *uri = NULL;
    WCHAR *buffer, *p;
    HSTRING str = NULL;
    HRESULT hr;

    *out = NULL;
    if (!(buffer = malloc( (lstrlenW( relative_path ) + ARRAY_SIZE(ms_appx_prefix)) * sizeof(*buffer) )))
        return E_OUTOFMEMORY;

    wcscpy( buffer, ms_appx_prefix );
    p = buffer + lstrlenW( ms_appx_prefix );
    for (; *relative_path; relative_path++) *p++ = (*relative_path == '\\') ? '/' : *relative_path;
    *p = 0;

    if (FAILED(hr = WindowsCreateString( buffer, lstrlenW( buffer ), &str ))) goto done;
    if (FAILED(hr = get_class_factory( RuntimeClass_Windows_Foundation_Uri, &IID_IUriRuntimeClassFactory,
                                       (void **)&uri_factory )))
        goto done;
    if (FAILED(hr = IUriRuntimeClassFactory_CreateUri( uri_factory, str, &uri ))) goto done;
    if (FAILED(hr = get_class_factory( RuntimeClass_Windows_Storage_Streams_RandomAccessStreamReference,
                                       &IID_IRandomAccessStreamReferenceStatics, (void **)&statics )))
        goto done;
    hr = IRandomAccessStreamReferenceStatics_CreateFromUri( statics, uri, out );

done:
    if (statics) IRandomAccessStreamReferenceStatics_Release( statics );
    if (uri) IUriRuntimeClass_Release( uri );
    if (uri_factory) IUriRuntimeClassFactory_Release( uri_factory );
    WindowsDeleteString( str );
    free( buffer );
    return hr;
}

static HRESULT WINAPI app_display_info_GetLogo( IAppDisplayInfo *iface, Size size,
                                                IRandomAccessStreamReference **value )
{
    struct app_display_info *impl = impl_from_IAppDisplayInfo( iface );
    WCHAR *path;
    char *app_id, *text, *logo;
    HRESULT hr;

    TRACE( "iface %p, size %dx%d, value %p\n", iface, (int)size.Width, (int)size.Height, value );
    if (!value) return E_INVALIDARG;
    *value = NULL;
    if (!(path = get_manifest_path() )) return HRESULT_FROM_WIN32( ERROR_FILE_NOT_FOUND );
    if (!(text = read_manifest_text( path )))
    {
        free( path );
        return HRESULT_FROM_WIN32( ERROR_FILE_NOT_FOUND );
    }
    free( path );

    /* The 44x44 logo is the app logo, scale selection is not implemented. */
    app_id = display_info_app_id( impl );
    logo = get_visual_elements_attr( text, app_id, "Square44x44Logo" );
    free( app_id );
    free( text );
    if (!logo) return HRESULT_FROM_WIN32( ERROR_FILE_NOT_FOUND );

    {
        WCHAR *wide = utf8_to_wide( logo );

        free( logo );
        if (!wide) return E_OUTOFMEMORY;
        hr = create_package_stream_reference( wide, value );
        free( wide );
    }
    return hr;
}

static const struct IAppDisplayInfoVtbl app_display_info_vtbl =
{
    app_display_info_QueryInterface,
    app_display_info_AddRef,
    app_display_info_Release,
    /* IInspectable methods */
    app_display_info_GetIids,
    app_display_info_GetRuntimeClassName,
    app_display_info_GetTrustLevel,
    /* IAppDisplayInfo methods */
    app_display_info_get_DisplayName,
    app_display_info_get_Description,
    app_display_info_GetLogo,
};

static HRESULT app_display_info_create( const WCHAR *app_id, IAppDisplayInfo **out )
{
    struct app_display_info *impl;

    if (!(impl = malloc( sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->IAppDisplayInfo_iface.lpVtbl = &app_display_info_vtbl;
    impl->app_id = app_id ? wcsdup( app_id ) : NULL;
    impl->ref = 1;
    *out = &impl->IAppDisplayInfo_iface;
    return S_OK;
}

/* ------------------------------------------------------------------------- *
 * AppInfo
 * ------------------------------------------------------------------------- */

struct appinfo
{
    IAppInfo IAppInfo_iface;
    IAppInfo2 IAppInfo2_iface;
    IAppInfo3 IAppInfo3_iface;
    IAppInfo4 IAppInfo4_iface;
    LONG ref;
};

static inline struct appinfo *impl_from_IAppInfo( IAppInfo *iface )
{
    return CONTAINING_RECORD( iface, struct appinfo, IAppInfo_iface );
}

/* The application id is the part of the app user model id behind the '!', the
 * manifest names it directly for an unpackaged process. */
static WCHAR *get_application_id(void)
{
    WCHAR *aumid, *id = NULL, *sep, *path;

    if ((aumid = get_env_dup( L"WINE_PACKAGE_AUMID" )) && (sep = wcschr( aumid, '!' ))) id = wcsdup( sep + 1 );
    free( aumid );
    if (id) return id;

    if ((path = get_manifest_path() ))
    {
        char *text = read_manifest_text( path );

        if (text)
        {
            const char *app = find_application_tag( text, NULL );
            char attr[256];

            if (app && tag_get_attr( app, "Id", attr, ARRAY_SIZE(attr) )) id = utf8_to_wide( attr );
            free( text );
        }
        free( path );
    }
    return id;
}

static WCHAR *get_app_user_model_id(void)
{
    WCHAR *aumid, *family, *id;

    if ((aumid = get_env_dup( L"WINE_PACKAGE_AUMID" ))) return aumid;
    if (!(family = get_package_family_name() )) return NULL;
    if (!(id = get_application_id() ))
    {
        free( family );
        return NULL;
    }
    if ((aumid = malloc( (lstrlenW( family ) + 1 + lstrlenW( id ) + 1) * sizeof(*aumid) )))
    {
        wcscpy( aumid, family );
        wcscat( aumid, L"!" );
        wcscat( aumid, id );
    }
    free( family );
    free( id );
    return aumid;
}

static HRESULT WINAPI appinfo_QueryInterface( IAppInfo *iface, REFIID iid, void **out )
{
    struct appinfo *impl = impl_from_IAppInfo( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IAppInfo ))
    {
        *out = &impl->IAppInfo_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }
    else if (IsEqualGUID( iid, &IID_IAppInfo2 ))
    {
        *out = &impl->IAppInfo2_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }
    else if (IsEqualGUID( iid, &IID_IAppInfo3 ))
    {
        *out = &impl->IAppInfo3_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }
    else if (IsEqualGUID( iid, &IID_IAppInfo4 ))
    {
        *out = &impl->IAppInfo4_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI appinfo_AddRef( IAppInfo *iface )
{
    struct appinfo *impl = impl_from_IAppInfo( iface );

    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI appinfo_Release( IAppInfo *iface )
{
    struct appinfo *impl = impl_from_IAppInfo( iface );
    LONG ref = InterlockedDecrement( &impl->ref );

    if (!ref) free( impl );
    return ref;
}

static HRESULT WINAPI appinfo_GetIids( IAppInfo *iface, ULONG *iid_count, IID **iids )
{
    IID iid_array[] =
    {
        IID_IAppInfo,
        IID_IAppInfo2,
        IID_IAppInfo3,
        IID_IAppInfo4,
    };
    IID *array = CoTaskMemAlloc( sizeof(iid_array) );

    if (!array) return E_OUTOFMEMORY;
    memcpy( array, iid_array, sizeof(iid_array) );
    *iid_count = ARRAY_SIZE(iid_array);
    *iids = array;
    return S_OK;
}

static HRESULT WINAPI appinfo_GetRuntimeClassName( IAppInfo *iface, HSTRING *class_name )
{
    return WindowsCreateString( RuntimeClass_Windows_ApplicationModel_AppInfo,
                                lstrlenW( RuntimeClass_Windows_ApplicationModel_AppInfo ), class_name );
}

static HRESULT WINAPI appinfo_GetTrustLevel( IAppInfo *iface, TrustLevel *trust_level )
{
    if (trust_level) *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT appinfo_create( IAppInfo **out );

static HRESULT WINAPI appinfo_get_Id( IAppInfo *iface, HSTRING *value )
{
    WCHAR *id;

    TRACE( "iface %p, value %p\n", iface, value );
    if (!value) return E_INVALIDARG;
    *value = NULL;
    if ((id = get_application_id() ))
    {
        WindowsCreateString( id, lstrlenW( id ), value );
        free( id );
    }
    return S_OK;
}

static HRESULT WINAPI appinfo_get_AppUserModelId( IAppInfo *iface, HSTRING *value )
{
    WCHAR *aumid;

    TRACE( "iface %p, value %p\n", iface, value );
    if (!value) return E_INVALIDARG;
    *value = NULL;
    if ((aumid = get_app_user_model_id() ))
    {
        WindowsCreateString( aumid, lstrlenW( aumid ), value );
        free( aumid );
    }
    return S_OK;
}

static HRESULT WINAPI appinfo_get_DisplayInfo( IAppInfo *iface, IAppDisplayInfo **value )
{
    WCHAR *id = get_application_id();
    HRESULT hr;

    TRACE( "iface %p, value %p\n", iface, value );
    if (!value)
    {
        free( id );
        return E_INVALIDARG;
    }
    *value = NULL;
    hr = app_display_info_create( id, value );
    free( id );
    return hr;
}

static HRESULT WINAPI appinfo_get_PackageFamilyName( IAppInfo *iface, HSTRING *value )
{
    WCHAR *family;

    TRACE( "iface %p, value %p\n", iface, value );
    if (!value) return E_INVALIDARG;
    *value = NULL;
    if ((family = get_package_family_name() ))
    {
        WindowsCreateString( family, lstrlenW( family ), value );
        free( family );
    }
    return S_OK;
}

static const struct IAppInfoVtbl appinfo_vtbl =
{
    appinfo_QueryInterface,
    appinfo_AddRef,
    appinfo_Release,
    /* IInspectable methods */
    appinfo_GetIids,
    appinfo_GetRuntimeClassName,
    appinfo_GetTrustLevel,
    /* IAppInfo methods */
    appinfo_get_Id,
    appinfo_get_AppUserModelId,
    appinfo_get_DisplayInfo,
    appinfo_get_PackageFamilyName,
};

DEFINE_IINSPECTABLE( appinfo2, IAppInfo2, struct appinfo, IAppInfo_iface )

static HRESULT WINAPI appinfo2_get_Package( IAppInfo2 *iface, IPackage **value )
{
    TRACE( "iface %p, value %p\n", iface, value );
    if (!value) return E_INVALIDARG;
    *value = NULL;
    return create_package_instance( value );
}

static const struct IAppInfo2Vtbl appinfo2_vtbl =
{
    appinfo2_QueryInterface,
    appinfo2_AddRef,
    appinfo2_Release,
    /* IInspectable methods */
    appinfo2_GetIids,
    appinfo2_GetRuntimeClassName,
    appinfo2_GetTrustLevel,
    /* IAppInfo2 methods */
    appinfo2_get_Package,
};

DEFINE_IINSPECTABLE( appinfo3, IAppInfo3, struct appinfo, IAppInfo_iface )

static HRESULT WINAPI appinfo3_get_ExecutionContext( IAppInfo3 *iface, AppExecutionContext *value )
{
    TRACE( "iface %p, value %p\n", iface, value );
    if (!value) return E_INVALIDARG;
    /* Wine has no guest package execution model, the process always runs in
     * the package it was launched from. */
    *value = AppExecutionContext_Host;
    return S_OK;
}

static const struct IAppInfo3Vtbl appinfo3_vtbl =
{
    appinfo3_QueryInterface,
    appinfo3_AddRef,
    appinfo3_Release,
    /* IInspectable methods */
    appinfo3_GetIids,
    appinfo3_GetRuntimeClassName,
    appinfo3_GetTrustLevel,
    /* IAppInfo3 methods */
    appinfo3_get_ExecutionContext,
};

DEFINE_IINSPECTABLE( appinfo4, IAppInfo4, struct appinfo, IAppInfo_iface )

static HRESULT WINAPI appinfo4_get_SupportedFileExtensions( IAppInfo4 *iface, UINT32 *value_size, HSTRING **value )
{
    WCHAR *id, *path;
    char *app_id, *text;
    UINT32 count = 0;
    HRESULT hr = S_OK;

    TRACE( "iface %p, value_size %p, value %p\n", iface, value_size, value );
    if (!value_size || !value) return E_INVALIDARG;
    *value_size = 0;
    *value = NULL;

    if (!(id = get_application_id() )) return S_OK;
    app_id = wide_to_utf8( id );
    free( id );
    if (!app_id) return S_OK;

    path = get_manifest_path();
    text = path ? read_manifest_text( path ) : NULL;
    free( path );
    if (text) hr = get_application_file_types( text, app_id, value, &count );
    if (SUCCEEDED(hr)) *value_size = count;
    free( text );
    free( app_id );
    return hr;
}

static const struct IAppInfo4Vtbl appinfo4_vtbl =
{
    appinfo4_QueryInterface,
    appinfo4_AddRef,
    appinfo4_Release,
    /* IInspectable methods */
    appinfo4_GetIids,
    appinfo4_GetRuntimeClassName,
    appinfo4_GetTrustLevel,
    /* IAppInfo4 methods */
    appinfo4_get_SupportedFileExtensions,
};

static HRESULT appinfo_create( IAppInfo **out )
{
    struct appinfo *impl;

    if (!(impl = malloc( sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->IAppInfo_iface.lpVtbl = &appinfo_vtbl;
    impl->IAppInfo2_iface.lpVtbl = &appinfo2_vtbl;
    impl->IAppInfo3_iface.lpVtbl = &appinfo3_vtbl;
    impl->IAppInfo4_iface.lpVtbl = &appinfo4_vtbl;
    impl->ref = 1;
    *out = &impl->IAppInfo_iface;
    return S_OK;
}

/* ------------------------------------------------------------------------- *
 * AppInfo activation factory / IAppInfoStatics
 * ------------------------------------------------------------------------- */

struct appinfo_statics
{
    IActivationFactory IActivationFactory_iface;
    IAppInfoStatics IAppInfoStatics_iface;
    IAppInfo IAppInfo_iface;
    LONG ref;
};

static inline struct appinfo_statics *impl_from_IActivationFactory( IActivationFactory *iface )
{
    return CONTAINING_RECORD( iface, struct appinfo_statics, IActivationFactory_iface );
}

static HRESULT WINAPI activation_factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
    struct appinfo_statics *impl = impl_from_IActivationFactory( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ))
    {
        *out = &impl->IActivationFactory_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }
    else if (IsEqualGUID( iid, &IID_IAppInfoStatics ))
    {
        *out = &impl->IAppInfoStatics_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }

    /* Windows aggregates the class default interface (IAppInfo) on the factory object
     * (CsWinRT probes the factory for the class interfaces). The factory is not an
     * instance; instance method calls fail. */
    if (IsEqualGUID( iid, &IID_IAppInfo ))
    {
        *out = &impl->IAppInfo_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI activation_factory_AddRef( IActivationFactory *iface )
{
    struct appinfo_statics *impl = impl_from_IActivationFactory( iface );

    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI activation_factory_Release( IActivationFactory *iface )
{
    struct appinfo_statics *impl = impl_from_IActivationFactory( iface );

    return InterlockedDecrement( &impl->ref );
}

static HRESULT WINAPI activation_factory_GetIids( IActivationFactory *iface, ULONG *iid_count, IID **iids )
{
    IID factory_iids[] =
    {
        IID_IActivationFactory,
        IID_IAppInfoStatics,
        IID_IAppInfo,
    };
    IID *array = CoTaskMemAlloc( sizeof(factory_iids) );

    if (!array) return E_OUTOFMEMORY;
    memcpy( array, factory_iids, sizeof(factory_iids) );
    *iid_count = ARRAY_SIZE(factory_iids);
    *iids = array;
    return S_OK;
}

static HRESULT WINAPI activation_factory_GetRuntimeClassName( IActivationFactory *iface, HSTRING *class_name )
{
    return WindowsCreateString( RuntimeClass_Windows_ApplicationModel_AppInfo,
                                lstrlenW( RuntimeClass_Windows_ApplicationModel_AppInfo ), class_name );
}

static HRESULT WINAPI activation_factory_GetTrustLevel( IActivationFactory *iface, TrustLevel *trust_level )
{
    if (trust_level) *trust_level = BaseTrust;
    return S_OK;
}

/* AppInfo is a statics-only class, instances are handed out by Current. */
static HRESULT WINAPI activation_factory_ActivateInstance( IActivationFactory *iface, IInspectable **instance )
{
    FIXME( "iface %p, instance %p stub!\n", iface, instance );
    return E_NOTIMPL;
}

static const struct IActivationFactoryVtbl activation_factory_vtbl =
{
    activation_factory_QueryInterface,
    activation_factory_AddRef,
    activation_factory_Release,
    /* IInspectable methods */
    activation_factory_GetIids,
    activation_factory_GetRuntimeClassName,
    activation_factory_GetTrustLevel,
    /* IActivationFactory methods */
    activation_factory_ActivateInstance,
};

DEFINE_IINSPECTABLE( appinfo_statics, IAppInfoStatics, struct appinfo_statics, IActivationFactory_iface )

static HRESULT WINAPI appinfo_statics_get_Current( IAppInfoStatics *iface, IAppInfo **value )
{
    TRACE( "iface %p, value %p\n", iface, value );
    if (!value) return E_INVALIDARG;
    *value = NULL;
    return appinfo_create( value );
}

/* Only the package the process was launched from is resolvable; an app user
 * model id that does not name it has no AppInfo to report. */
static HRESULT WINAPI appinfo_statics_GetFromAppUserModelId( IAppInfoStatics *iface, HSTRING app_user_model_id,
                                                             IAppInfo **result )
{
    WCHAR *aumid, *current;
    HRESULT hr = S_OK;

    TRACE( "iface %p, app_user_model_id %s, result %p\n", iface, debugstr_hstring( app_user_model_id ), result );
    if (!app_user_model_id || !result) return E_INVALIDARG;
    *result = NULL;
    if (!(aumid = get_app_user_model_id() )) return HRESULT_FROM_WIN32( ERROR_FILE_NOT_FOUND );
    current = (WCHAR *)WindowsGetStringRawBuffer( app_user_model_id, NULL );
    if (lstrcmpiW( current, aumid )) hr = HRESULT_FROM_WIN32( ERROR_FILE_NOT_FOUND );
    else hr = appinfo_create( result );
    free( aumid );
    return hr;
}

/* Resolving an app user model id of another user needs a package database for
 * that user, which Wine does not have. */
static HRESULT WINAPI appinfo_statics_GetFromAppUserModelIdForUser( IAppInfoStatics *iface, IUser *user,
                                                                    HSTRING app_user_model_id, IAppInfo **result )
{
    FIXME( "iface %p, user %p, app_user_model_id %s, result %p stub!\n", iface, user,
           debugstr_hstring( app_user_model_id ), result );
    return HRESULT_FROM_WIN32( ERROR_FILE_NOT_FOUND );
}

static const struct IAppInfoStaticsVtbl appinfo_statics_vtbl =
{
    appinfo_statics_QueryInterface,
    appinfo_statics_AddRef,
    appinfo_statics_Release,
    /* IInspectable methods */
    appinfo_statics_GetIids,
    appinfo_statics_GetRuntimeClassName,
    appinfo_statics_GetTrustLevel,
    /* IAppInfoStatics methods */
    appinfo_statics_get_Current,
    appinfo_statics_GetFromAppUserModelId,
    appinfo_statics_GetFromAppUserModelIdForUser,
};


/* Windows aggregates the class default interface (IAppInfo) on the factory object;
 * the factory is not an instance: method calls fail. */
DEFINE_IINSPECTABLE_( factory_def, IAppInfo, struct appinfo_statics, impl_from_IAppInfo_factory_def,
                      IAppInfo_iface, &impl->IActivationFactory_iface )

static HRESULT WINAPI factory_def_get_Id( IAppInfo *iface, HSTRING *value ) { return E_FAIL; }
static HRESULT WINAPI factory_def_get_AppUserModelId( IAppInfo *iface, HSTRING *value ) { return E_FAIL; }
static HRESULT WINAPI factory_def_get_DisplayInfo( IAppInfo *iface, IAppDisplayInfo **value ) { return E_FAIL; }
static HRESULT WINAPI factory_def_get_PackageFamilyName( IAppInfo *iface, HSTRING *value ) { return E_FAIL; }

static const struct IAppInfoVtbl factory_def_vtbl =
{
    factory_def_QueryInterface,
    factory_def_AddRef,
    factory_def_Release,
    factory_def_GetIids,
    factory_def_GetRuntimeClassName,
    factory_def_GetTrustLevel,
    factory_def_get_Id,
    factory_def_get_AppUserModelId,
    factory_def_get_DisplayInfo,
    factory_def_get_PackageFamilyName,
};

static struct appinfo_statics appinfo_statics =
{
    {&activation_factory_vtbl},
    {&appinfo_statics_vtbl},
    {&factory_def_vtbl}, 1,
};

IActivationFactory *appinfo_factory = &appinfo_statics.IActivationFactory_iface;
