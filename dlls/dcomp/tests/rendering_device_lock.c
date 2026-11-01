/*
 * Lock-order conformance test for the DirectComposition rendering device.
 *
 * A client that passes no rendering device to DCompositionCreateDevice2() gets
 * a default Direct3D one built on demand, and building it enters Direct3D,
 * which loads dlls. The loader lock must never be taken while a critical
 * section is held: a thread that is inside DllMain owns the loader lock and
 * reaches this same code (the composition client is created from a DllMain),
 * and the two threads deadlock, freezing the process. dcomp therefore resolves
 * its Direct3D module at process attach and builds the device outside its own
 * critical section, which this test observes from the outside: creating the
 * rendering device must not load a dll.
 *
 * This is its own test source so that it runs in a process that has not
 * created any device yet; d3d11.dll is not imported, an import table entry
 * would load it at process start and hide the lazy load being asserted about.
 *
 * Copyright 2026
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
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include <stdarg.h>

#include "ntstatus.h"
#define WIN32_NO_STATUS
#define COBJMACROS
#include <winternl.h>
/* No initguid.h here: dcomp.c, which is linked into this same test executable,
 * defines the GUIDs; defining them twice breaks the link. */
#include <d3d11.h>
#include "dcomp.h"
#include "wine/test.h"

static HRESULT (WINAPI *pDCompositionCreateDevice2)( IUnknown *rendering_device, REFIID iid, void **device );

static unsigned int notified_loads;
static WCHAR loaded_names[16][MAX_PATH];

static void WINAPI dll_load_notification( ULONG reason, LDR_DLL_NOTIFICATION_DATA *data, void *context )
{
    if (reason == LDR_DLL_NOTIFICATION_REASON_LOADED && data->Loaded.DllBase && data->Loaded.BaseDllName)
    {
        const UNICODE_STRING *name = data->Loaded.BaseDllName;
        unsigned int i = InterlockedIncrement( &notified_loads ) - 1;

        if (i < ARRAY_SIZE( loaded_names ) && name && name->Buffer)
        {
            unsigned int len = name->Length / sizeof(WCHAR);
            if (len > MAX_PATH - 1) len = MAX_PATH - 1;
            memcpy( loaded_names[i % ARRAY_SIZE( loaded_names )], name->Buffer, len * sizeof(WCHAR) );
            loaded_names[i % ARRAY_SIZE( loaded_names )][len] = 0;
        }
    }
}

START_TEST(rendering_device_lock)
{
    static const WCHAR d3d11W[] = {'d','3','d','1','1','.','d','l','l',0};
    IDCompositionSurface *surface = NULL;
    IDCompositionDevice *device = NULL;
    unsigned int i, count, start;
    BOOL loaded_d3d11 = FALSE;
    void *cookie = NULL;
    NTSTATUS status;
    HMODULE module;
    HRESULT hr;

    module = LoadLibraryW( L"dcomp.dll" );
    if (!module)
    {
        win_skip( "dcomp.dll not found.\n" );
        return;
    }
    pDCompositionCreateDevice2 = (void *)GetProcAddress( module, "DCompositionCreateDevice2" );
    if (!pDCompositionCreateDevice2)
    {
        win_skip( "DCompositionCreateDevice2 not exported\n" );
        FreeLibrary( module );
        return;
    }

    start = notified_loads;
    status = LdrRegisterDllNotification( 0, dll_load_notification, NULL, &cookie );
    if (status != STATUS_SUCCESS)
    {
        win_skip( "LdrRegisterDllNotification failed, %#lx\n", status );
        FreeLibrary( module );
        return;
    }

    hr = pDCompositionCreateDevice2( NULL, &IID_IDCompositionDevice, (void **)&device );
    ok( hr == S_OK, "DCompositionCreateDevice2 failed, hr %#lx.\n", hr );
    if (SUCCEEDED(hr))
    {
        hr = IDCompositionDevice_CreateSurface( device, 64, 64, DXGI_FORMAT_B8G8R8A8_UNORM,
                (DXGI_ALPHA_MODE)1 /* D3D_ALPHA_MODE_IGNORE */, &surface );
        ok( hr == S_OK, "CreateSurface on a device with no rendering device failed, hr %#lx.\n", hr );
        if (SUCCEEDED(hr)) IDCompositionSurface_Release( surface );
        IDCompositionDevice_Release( device );
    }

    count = notified_loads - start;
    LdrUnregisterDllNotification( cookie );

    for (i = 0; i < count && i < ARRAY_SIZE( loaded_names ); i++)
    {
        if (!lstrcmpiW( loaded_names[(start + i) % ARRAY_SIZE( loaded_names )], d3d11W )) loaded_d3d11 = TRUE;
    }
    for (i = 0; i < count && i < ARRAY_SIZE( loaded_names ); i++)
        trace( "loaded during the rendering-device window: %s\n",
                debugstr_w( loaded_names[(start + i) % ARRAY_SIZE( loaded_names )] ) );

    ok( !loaded_d3d11, "the rendering-device path loaded d3d11.dll (%u dlls loaded during the window)\n", count );

    FreeLibrary( module );
}
