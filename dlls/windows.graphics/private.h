/* WinRT Windows.Graphics Implementation
 *
 * Copyright 2026 Zhiyi Zhang for CodeWeavers
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
#include <stddef.h>

#define COBJMACROS
#include "windef.h"
#include "winbase.h"
#include "wingdi.h"
#include "winuser.h"
#include "winstring.h"
#include "activation.h"
#include "wine/debug.h"

#define WIDL_using_Windows_Foundation
#define WIDL_using_Windows_Graphics_Display
#include "windows.graphics.display.h"

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

/* Windows.Graphics.Display.IDisplayInformationInterop ({7449121c-382b-4705-8da7-a795ba482013}) */
DEFINE_GUID( IID_IDisplayInformationInterop, 0x7449121c, 0x382b, 0x4705, 0x8d, 0xa7, 0xa7, 0x95, 0xba, 0x48, 0x20, 0x13 );

typedef struct IDisplayInformationInterop IDisplayInformationInterop;

typedef struct IDisplayInformationInteropVtbl
{
    HRESULT (WINAPI *QueryInterface)( IDisplayInformationInterop *iface, REFIID riid, void **out );
    ULONG (WINAPI *AddRef)( IDisplayInformationInterop *iface );
    ULONG (WINAPI *Release)( IDisplayInformationInterop *iface );
    HRESULT (WINAPI *GetIids)( IDisplayInformationInterop *iface, ULONG *iid_count, IID **iids );
    HRESULT (WINAPI *GetRuntimeClassName)( IDisplayInformationInterop *iface, HSTRING *class_name );
    HRESULT (WINAPI *GetTrustLevel)( IDisplayInformationInterop *iface, TrustLevel *trust_level );
    HRESULT (WINAPI *GetForWindow)( IDisplayInformationInterop *iface, HWND window, REFIID riid, void **display_info );
    HRESULT (WINAPI *GetForMonitor)( IDisplayInformationInterop *iface, HMONITOR monitor, REFIID riid, void **display_info );
} IDisplayInformationInteropVtbl;

struct IDisplayInformationInterop
{
    const IDisplayInformationInteropVtbl *lpVtbl;
};
