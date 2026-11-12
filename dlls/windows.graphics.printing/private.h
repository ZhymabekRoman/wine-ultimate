/* WinRT Windows.Graphics.Printing Implementation
 *
 * Copyright 2026 Wine Developers
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

#ifndef __WINE_WINDOWS_GRAPHICS_PRINTING_PRIVATE_H
#define __WINE_WINDOWS_GRAPHICS_PRINTING_PRIVATE_H

#include <stdarg.h>
#include <stddef.h>
#include <stdlib.h>

#define COBJMACROS
#include "windef.h"
#include "winbase.h"
#include "winstring.h"
#include "winspool.h"

#include "activation.h"
#include "asyncinfo.h"
#include "eventtoken.h"
#include "roapi.h"

#include "wine/debug.h"

#define WIDL_using_Windows_Foundation
#define WIDL_using_Windows_Foundation_Collections
#define WIDL_using_Windows_Graphics_Printing
#include "windows.foundation.h"
#include "windows.foundation.collections.h"
#include "windows.graphics.printing.h"

/* Short names for the ns-prefixed C types the module code uses. The widl
 * header exposes the runtimeclasses through their primary interface, so the
 * short names map to the interface structs that appear in the method
 * signatures. */
#define PrintManager IPrintManager
#define PrintTask IPrintTask
#define PrintTaskRequest IPrintTaskRequest
#define PrintTaskRequestedDeferral IPrintTaskRequestedDeferral
#define PrintTaskRequestedEventArgs IPrintTaskRequestedEventArgs
#define PrintTaskSourceRequestedArgs IPrintTaskSourceRequestedArgs
#define PrintTaskSourceRequestedDeferral IPrintTaskSourceRequestedDeferral
#define PrintTaskSourceRequestedHandler IPrintTaskSourceRequestedHandler
#define WindowsFoundationDateTime __x_ABI_CWindows_CFoundation_CDateTime
#define PrintTaskRequestedEventHandler \
    __FITypedEventHandler_2_Windows__CGraphics__CPrinting__CPrintManager_Windows__CGraphics__CPrinting__CPrintTaskRequestedEventArgs

/* Token-paste friendly call helpers: Foo_Invoke is one token, so the plain
 * type alias above does not expand in call position. */
#define PrintTaskSourceRequestedHandler_Invoke( this, args ) \
    IPrintTaskSourceRequestedHandler_Invoke( this, args )
#define PrintTaskRequestedEventHandler_Invoke( this, sender, args ) \
    __FITypedEventHandler_2_Windows__CGraphics__CPrinting__CPrintManager_Windows__CGraphics__CPrinting__CPrintTaskRequestedEventArgs_Invoke( this, sender, args )

extern IActivationFactory *print_manager_factory;

/* main.c: the PrintManager object (factory + statics + statics2 + instance). */
HRESULT raise_print_task_requested( IInspectable *manager );

/* print_task.c: the print-task side of the contract. */
IInspectable *create_print_task_request( void );
IInspectable *create_print_task_requested_event_args( void );

/*
 * Born-completed IAsyncOperation<boolean> (print_task.c). ShowPrintUIAsync has
 * no print UI to wait for in a prefix: the operation is created already
 * completed with a TRUE result, so both the C++/CX and C++/WinRT projections
 * resume the caller synchronously.
 */
struct bool_async_op;

extern const void *bool_async_op_vtbl[];
extern const void *bool_async_info_vtbl[];

struct bool_async_op *create_bool_async_op( void );
void *bool_async_op_iface( struct bool_async_op *impl );

/*
 * winspool/CUPS bridge (main.c). The prefix printer list is the CUPS queue list
 * that winspool.drv exposes: the default queue and the number of printers the
 * print system offers. Both take a caller-owned buffer for the default name.
 */
HRESULT printing_get_default_printer( WCHAR *buffer, DWORD *size );
ULONG printing_get_printer_count( void );

#define DEFINE_IINSPECTABLE_( pfx, iface_type, impl_type, impl_from, iface_mem, expr )             \
    static inline impl_type *pfx##_impl_from( iface_type *iface )                                  \
    {                                                                                              \
        return CONTAINING_RECORD( iface, impl_type, iface_mem );                                   \
    }                                                                                              \
    static HRESULT WINAPI pfx##_QueryInterface( iface_type *iface, REFIID iid, void **out )        \
    {                                                                                              \
        impl_type *impl = pfx##_impl_from( iface );                                                \
        return IInspectable_QueryInterface( (IInspectable *)(expr), iid, out );                    \
    }                                                                                              \
    static ULONG WINAPI pfx##_AddRef( iface_type *iface )                                          \
    {                                                                                              \
        impl_type *impl = pfx##_impl_from( iface );                                                \
        return IInspectable_AddRef( (IInspectable *)(expr) );                                      \
    }                                                                                              \
    static ULONG WINAPI pfx##_Release( iface_type *iface )                                         \
    {                                                                                              \
        impl_type *impl = pfx##_impl_from( iface );                                                \
        return IInspectable_Release( (IInspectable *)(expr) );                                     \
    }                                                                                              \
    static HRESULT WINAPI pfx##_GetIids( iface_type *iface, ULONG *iid_count, IID **iids )         \
    {                                                                                              \
        impl_type *impl = pfx##_impl_from( iface );                                                \
        return IInspectable_GetIids( (IInspectable *)(expr), iid_count, iids );                    \
    }                                                                                              \
    static HRESULT WINAPI pfx##_GetRuntimeClassName( iface_type *iface, HSTRING *class_name )      \
    {                                                                                              \
        impl_type *impl = pfx##_impl_from( iface );                                                \
        return IInspectable_GetRuntimeClassName( (IInspectable *)(expr), class_name );             \
    }                                                                                              \
    static HRESULT WINAPI pfx##_GetTrustLevel( iface_type *iface, TrustLevel *trust_level )        \
    {                                                                                              \
        impl_type *impl = pfx##_impl_from( iface );                                                \
        return IInspectable_GetTrustLevel( (IInspectable *)(expr), trust_level );                  \
    }
#define DEFINE_IINSPECTABLE( pfx, iface_type, impl_type, base_iface )                              \
    DEFINE_IINSPECTABLE_( pfx, iface_type, impl_type, impl_from_##iface_type, iface_type##_iface, &impl->base_iface )

#endif /* __WINE_WINDOWS_GRAPHICS_PRINTING_PRIVATE_H */
