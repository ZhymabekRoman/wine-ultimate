/* WinRT Windows.Web Implementation
 *
 * Copyright (C) 2024 Mohamad Al-Jaf
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

#ifndef __WINE_WINDOWS_WEB_PRIVATE_H
#define __WINE_WINDOWS_WEB_PRIVATE_H

#include <stdarg.h>
#include <errno.h>

#define COBJMACROS
#include "windef.h"
#include "winbase.h"
#include "winstring.h"

#include "activation.h"
#include "roapi.h"

#define WIDL_using_Windows_Foundation
#define WIDL_using_Windows_Foundation_Collections
#include "windows.foundation.h"
#define WIDL_using_Windows_Data_Json
#include "windows.data.json.h"
#define WIDL_using_Windows_Web_Http
#define WIDL_using_Windows_Web_Http_Headers
#define WIDL_using_Windows_Web_Http_Filters
#include "windows.web.http.h"

#include "cjson/cJSON.h"

/* streams.c: in-memory IBuffer + DataReader (Windows.Storage.Streams) */
HRESULT create_mem_buffer_from_hstring( HSTRING str, void **out );
HRESULT create_mem_buffer_from_bytes( const void *data, unsigned long len, void **out );
HRESULT create_empty_mem_buffer( unsigned long cap, void **out );
HRESULT create_ras_stream_from_buffer( void *buffer, void **out );
void *create_data_reader_factory( void );
void *create_buffer_factory( void );
HRESULT create_async_http_op_obj( void *result_obj, void **out );

extern IActivationFactory *json_array_factory;
extern IActivationFactory *json_object_factory;
extern IActivationFactory *json_value_factory;

extern IActivationFactory *http_client_factory;
extern IActivationFactory *http_request_factory;
extern IActivationFactory *http_response_factory;
extern IActivationFactory *http_string_content_factory;
extern IActivationFactory *http_buffer_content_factory;
extern IActivationFactory *http_stream_content_factory;
extern IActivationFactory *http_method_factory;
extern IActivationFactory *http_base_protocol_filter_factory;
extern IActivationFactory *http_credentials_header_value_factory;

struct json_root
{
    LONG ref;
    cJSON *tree;
};

static inline struct json_root *json_root_create( cJSON *tree )
{
    struct json_root *root = malloc( sizeof(*root) );
    if (!root) return NULL;
    root->ref = 1;
    root->tree = tree;
    return root;
}

static inline void json_root_addref( struct json_root *root )
{
    if (root) InterlockedIncrement( &root->ref );
}

static inline void json_root_release( struct json_root *root )
{
    if (root && !InterlockedDecrement( &root->ref ))
    {
        if (root->tree) cJSON_Delete( root->tree );
        free( root );
    }
}

/* {65A7C57E-0644-48E0-A6E5-A9D6A0553A89} */
static const GUID IID_IJsonValueInternal = {0x65a7c57e, 0x0644, 0x48e0, {0xa6, 0xe5, 0xa9, 0xd6, 0xa0, 0x55, 0x3a, 0x89}};

typedef struct IJsonValueInternal IJsonValueInternal;
typedef struct IJsonValueInternalVtbl
{
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(IJsonValueInternal *This, REFIID riid, void **ppvObject);
    ULONG (STDMETHODCALLTYPE *AddRef)(IJsonValueInternal *This);
    ULONG (STDMETHODCALLTYPE *Release)(IJsonValueInternal *This);
    cJSON *(STDMETHODCALLTYPE *GetNode)(IJsonValueInternal *This);
} IJsonValueInternalVtbl;

struct IJsonValueInternal
{
    const IJsonValueInternalVtbl *lpVtbl;
};

HRESULT create_json_value( cJSON *node, struct json_root *root, IJsonValue **out );
HRESULT create_json_object( cJSON *node, struct json_root *root, IJsonObject **out );
HRESULT create_json_array( cJSON *node, struct json_root *root, IJsonArray **out );
cJSON *cjson_node_duplicate_from_IJsonValue( IJsonValue *value );
HRESULT json_array_push( IJsonArray *iface, IJsonValue *value );

static inline HRESULT hstring_from_utf8_len( const char *utf8, int utf8_len, HSTRING *out )
{
    int len;
    WCHAR *wstr;
    HRESULT hr;

    if (!out) return E_POINTER;
    *out = NULL;
    if (!utf8 || utf8_len <= 0) return WindowsCreateString( L"", 0, out );
    len = MultiByteToWideChar( CP_UTF8, 0, utf8, utf8_len, NULL, 0 );
    if (!len) return E_FAIL;
    wstr = malloc( len * sizeof(WCHAR) );
    if (!wstr) return E_OUTOFMEMORY;
    MultiByteToWideChar( CP_UTF8, 0, utf8, utf8_len, wstr, len );
    hr = WindowsCreateString( wstr, len, out );
    free( wstr );
    return hr;
}

static inline HRESULT hstring_from_utf8( const char *utf8, HSTRING *out )
{
    if (!utf8) return WindowsCreateString( L"", 0, out );
    return hstring_from_utf8_len( utf8, strlen(utf8), out );
}

static inline char *utf8_from_hstring( HSTRING str, int *out_len )
{
    UINT32 len = 0;
    const WCHAR *wstr;
    int utf8_len;
    char *utf8;

    if (!str)
    {
        if (out_len) *out_len = 0;
        return strdup( "" );
    }
    wstr = WindowsGetStringRawBuffer( str, &len );
    utf8_len = WideCharToMultiByte( CP_UTF8, 0, wstr, len, NULL, 0, NULL, NULL );
    utf8 = malloc( utf8_len + 1 );
    if (!utf8) return NULL;
    WideCharToMultiByte( CP_UTF8, 0, wstr, len, utf8, utf8_len, NULL, NULL );
    utf8[utf8_len] = 0;
    if (out_len) *out_len = utf8_len;
    return utf8;
}

static inline JsonValueType get_json_value_type( const cJSON *node )
{
    if (!node) return JsonValueType_Null;
    if (node->type & (cJSON_False | cJSON_True)) return JsonValueType_Boolean;
    if (node->type & cJSON_NULL) return JsonValueType_Null;
    if (node->type & cJSON_Number) return JsonValueType_Number;
    if (node->type & cJSON_String) return JsonValueType_String;
    if (node->type & cJSON_Array) return JsonValueType_Array;
    if (node->type & cJSON_Object) return JsonValueType_Object;
    return JsonValueType_Null;
}

#define DEFINE_IINSPECTABLE_( pfx, iface_type, impl_type, impl_from, iface_mem, expr )             \
    static inline impl_type *impl_from( iface_type *iface )                                        \
    {                                                                                              \
        return CONTAINING_RECORD( iface, impl_type, iface_mem );                                   \
    }                                                                                              \
    static HRESULT WINAPI pfx##_QueryInterface( iface_type *iface, REFIID iid, void **out )        \
    {                                                                                              \
        impl_type *impl = impl_from( iface );                                                      \
        return IInspectable_QueryInterface( (IInspectable *)(expr), iid, out );                    \
    }                                                                                              \
    static ULONG WINAPI pfx##_AddRef( iface_type *iface )                                          \
    {                                                                                              \
        impl_type *impl = impl_from( iface );                                                      \
        return IInspectable_AddRef( (IInspectable *)(expr) );                                      \
    }                                                                                              \
    static ULONG WINAPI pfx##_Release( iface_type *iface )                                         \
    {                                                                                              \
        impl_type *impl = impl_from( iface );                                                      \
        return IInspectable_Release( (IInspectable *)(expr) );                                     \
    }                                                                                              \
    static HRESULT WINAPI pfx##_GetIids( iface_type *iface, ULONG *iid_count, IID **iids )         \
    {                                                                                              \
        impl_type *impl = impl_from( iface );                                                      \
        return IInspectable_GetIids( (IInspectable *)(expr), iid_count, iids );                    \
    }                                                                                              \
    static HRESULT WINAPI pfx##_GetRuntimeClassName( iface_type *iface, HSTRING *class_name )      \
    {                                                                                              \
        impl_type *impl = impl_from( iface );                                                      \
        return IInspectable_GetRuntimeClassName( (IInspectable *)(expr), class_name );             \
    }                                                                                              \
    static HRESULT WINAPI pfx##_GetTrustLevel( iface_type *iface, TrustLevel *trust_level )        \
    {                                                                                              \
        impl_type *impl = impl_from( iface );                                                      \
        return IInspectable_GetTrustLevel( (IInspectable *)(expr), trust_level );                  \
    }
#define DEFINE_IINSPECTABLE( pfx, iface_type, impl_type, base_iface )                              \
    DEFINE_IINSPECTABLE_( pfx, iface_type, impl_type, impl_from_##iface_type, iface_type##_iface, &impl->base_iface )

#endif
