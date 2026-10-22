/* WinRT Windows.Storage Implementation
 *
 * Copyright (C) 2025 Mohamad Al-Jaf
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

#ifndef __WINE_WINDOWS_STORAGE_PRIVATE_H
#define __WINE_WINDOWS_STORAGE_PRIVATE_H

#include <stdarg.h>

#define COBJMACROS
#include "windef.h"
#include "winbase.h"
#include "winstring.h"

#include "activation.h"

#include "wine/debug.h"

#define WIDL_using_Windows_Foundation
#define WIDL_using_Windows_Foundation_Collections
#include "windows.foundation.h"
#define WIDL_using_Windows_Storage
#define WIDL_using_Windows_Storage_Streams
#include "windows.storage.h"
#include "windows.storage.streams.h"
#include "robuffer.h"

extern IActivationFactory *storage_file_factory;
extern IActivationFactory *storage_folder_factory;
extern IActivationFactory *file_io_factory;
extern IActivationFactory *path_io_factory;
extern IActivationFactory *buffer_factory;
extern IActivationFactory *data_reader_factory;
extern IActivationFactory *data_writer_factory;
extern IActivationFactory *in_memory_random_access_stream_factory;
extern IActivationFactory *random_access_stream_reference_factory;

HRESULT storage_file_create( const WCHAR *path, IStorageFile **out );
HRESULT storage_folder_create( const WCHAR *path, IStorageFolder **out );
HRESULT read_file_bytes( const WCHAR *path, BYTE **out_bytes, DWORD *out_size );
HRESULT buffer_create( UINT32 capacity, IBuffer **out );
HRESULT in_memory_stream_create( IRandomAccessStream **out );
HRESULT in_memory_stream_create_from_file( const WCHAR *path, IRandomAccessStream **out );
HRESULT data_reader_create( IInputStream *stream, IDataReader **out );
HRESULT data_writer_create( IOutputStream *stream, IDataWriter **out );
HRESULT random_access_stream_reference_create( IRandomAccessStream *stream, IRandomAccessStreamReference **out );

HRESULT async_operation_inspectable_create( const GUID *iid, const WCHAR *class_name, IInspectable *result, void **out );
HRESULT async_operation_uint32_create( const WCHAR *class_name, UINT32 result, IAsyncOperation_UINT32 **out );
HRESULT async_operation_boolean_create( const WCHAR *class_name, boolean result, IAsyncOperation_boolean **out );
HRESULT async_operation_hstring_create( const WCHAR *class_name, HSTRING result, IAsyncOperation_HSTRING **out );
HRESULT async_action_create( const WCHAR *class_name, HRESULT hr_result, IAsyncAction **out );
HRESULT async_operation_progress_buffer_create( IBuffer *result, IAsyncOperationWithProgress_IBuffer_UINT32 **out );
HRESULT async_operation_progress_uint32_create( UINT32 result, IAsyncOperationWithProgress_UINT32_UINT32 **out );

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
