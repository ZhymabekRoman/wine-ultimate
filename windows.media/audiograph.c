/* WinRT Windows.Media.Audio.AudioGraph Implementation
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

#include "private.h"
#include "objbase.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(media);

#ifndef AsyncStatus_Completed
#define AsyncStatus_Completed Completed
#endif

/* GUIDs for Windows.Media.Audio interfaces */
static const GUID guid_IAudioGraphStatics                = {0x76ec3132, 0xe159, 0x4ab7, {0xa8, 0x2a, 0x17, 0xbe, 0xb4, 0xb3, 0x1e, 0x94}};
static const GUID guid_IAudioGraph                       = {0x1ad46eed, 0xe48c, 0x4e14, {0x96, 0x60, 0x2c, 0x4f, 0x83, 0xe9, 0xcd, 0xd8}};
static const GUID guid_ICreateAudioGraphResult           = {0x5453ef7e, 0x7bde, 0x4b76, {0xbb, 0x5d, 0x48, 0xf7, 0x9c, 0xfc, 0x8c, 0x0b}};
static const GUID guid_ICreateAudioGraphResult2          = {0x6d738dfc, 0x88c6, 0x4fcb, {0xa5, 0x34, 0x85, 0xce, 0xdd, 0x40, 0x50, 0xa1}};

static const GUID guid_IAudioDeviceInputNode             = {0xb01b6be1, 0x6f4e, 0x49e2, {0xac, 0x01, 0x55, 0x9d, 0x62, 0xbe, 0xb3, 0xa9}};
static const GUID guid_ICreateAudioDeviceInputNodeResult = {0x16eec7a8, 0x1ca7, 0x40ef, {0x91, 0xa4, 0xd3, 0x46, 0xe0, 0xaa, 0x1b, 0xba}};
static const GUID guid_ICreateAudioDeviceInputNodeResult2= {0x921c69ce, 0x3f35, 0x41c7, {0x96, 0x22, 0x79, 0xf6, 0x08, 0xba, 0xed, 0xc2}};

static const GUID guid_IAudioDeviceOutputNode            = {0x362edbff, 0xff1c, 0x4434, {0x9e, 0x0f, 0xbd, 0x2e, 0xf5, 0x22, 0xac, 0x82}};
static const GUID guid_ICreateAudioDeviceOutputNodeResult= {0xf7776d27, 0x1d9a, 0x47f7, {0x9c, 0xd4, 0x28, 0x59, 0xcc, 0x1b, 0x7b, 0xff}};
static const GUID guid_ICreateAudioDeviceOutputNodeResult2={0x4864269f, 0xbdce, 0x4ab1, {0xbd, 0x38, 0xfb, 0xae, 0x93, 0xae, 0xda, 0xca}};

static const GUID guid_IAudioFileInputNode               = {0x905b67c8, 0x6f65, 0x4cd4, {0x88, 0x90, 0x46, 0x94, 0x84, 0x3c, 0x27, 0x6d}};
static const GUID guid_ICreateAudioFileInputNodeResult   = {0xce83d61c, 0xe297, 0x4c50, {0x9c, 0xe7, 0x1c, 0x7a, 0x69, 0xd6, 0xbd, 0x09}};
static const GUID guid_ICreateAudioFileInputNodeResult2  = {0xf9082020, 0x3d80, 0x4fe0, {0x81, 0xc1, 0x76, 0x8f, 0xea, 0x7c, 0xa7, 0xe0}};

static const GUID guid_IAudioNode                        = {0x15389d7f, 0xdbd8, 0x4819, {0xbf, 0x03, 0x66, 0x8e, 0x93, 0x57, 0xcd, 0x6d}};
static const GUID guid_IAudioInputNode                   = {0xd148005c, 0x8428, 0x4784, {0xb7, 0xfd, 0xa9, 0x9d, 0x46, 0x8c, 0x5d, 0x20}};
static const GUID guid_IClosable                         = {0x30d5a761, 0x69de, 0x45f2, {0xac, 0x1c, 0x4e, 0x0f, 0x52, 0xe1, 0xec, 0x77}};

static const GUID guid_IAudioGraphSettingsFactory        = {0xa5d91cc6, 0xc2eb, 0x4a61, {0xa2, 0x14, 0x1d, 0x66, 0xd7, 0x5f, 0x83, 0xda}};
static const GUID guid_IAudioGraphSettings               = {0x1d59647f, 0xe6fe, 0x4628, {0x84, 0xf8, 0x9d, 0x8b, 0xdb, 0xa2, 0x57, 0x85}};
static const GUID guid_IAudioGraphSettings2              = {0x72919787, 0x4dab, 0x46e3, {0xb4, 0xc9, 0xd8, 0xe1, 0xa2, 0x63, 0x60, 0x62}};

/* Forward declarations */
static HRESULT create_audio_graph( void **out );
static HRESULT create_device_input_node( void **out );
static HRESULT create_device_output_node( void **out );
static HRESULT create_file_input_node( void *file, void **out );

/* =========================================================================
 * Generic Async Operation
 * ========================================================================= */

struct async_op
{
    const void *vtbl_op;
    const void *vtbl_info;
    LONG ref;
    void *result;
    const WCHAR *class_name;
};

static HRESULT WINAPI async_op_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct async_op *impl = iface;
    if (!out) return E_POINTER;

    if (IsEqualGUID( iid, &IID_IAsyncInfo ))
    {
        *out = (void *)((BYTE *)iface + sizeof(void *));
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }

    *out = iface;
    InterlockedIncrement( &impl->ref );
    return S_OK;
}

static ULONG WINAPI async_op_AddRef( void *iface )
{
    struct async_op *impl = iface;
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI async_op_Release( void *iface )
{
    struct async_op *impl = iface;
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        if (impl->result) IUnknown_Release( (IUnknown *)impl->result );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI async_op_GetIids( void *iface, ULONG *count, IID **iids )
{
    if (count) *count = 0;
    if (iids) *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI async_op_GetRuntimeClassName( void *iface, HSTRING *class_name )
{
    struct async_op *impl = iface;
    return WindowsCreateString( impl->class_name, wcslen( impl->class_name ), class_name );
}

static HRESULT WINAPI async_op_GetTrustLevel( void *iface, TrustLevel *trust_level )
{
    if (trust_level) *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI async_op_put_Completed( void *iface, void *handler )
{
    if (handler)
    {
        void **vtbl = *(void ***)handler;
        HRESULT (STDMETHODCALLTYPE *invoke)(void *, void *, UINT32) = (void *)vtbl[3];
        if (invoke) invoke( handler, iface, AsyncStatus_Completed );
    }
    return S_OK;
}

static HRESULT WINAPI async_op_get_Completed( void *iface, void **handler )
{
    if (handler) *handler = NULL;
    return S_OK;
}

static HRESULT WINAPI async_op_GetResults( void *iface, void **results )
{
    struct async_op *impl = iface;
    if (!results) return E_POINTER;
    *results = impl->result;
    if (impl->result) IUnknown_AddRef( (IUnknown *)impl->result );
    return S_OK;
}

static const void *async_op_vtbl[] =
{
    async_op_QueryInterface,
    async_op_AddRef,
    async_op_Release,
    async_op_GetIids,
    async_op_GetRuntimeClassName,
    async_op_GetTrustLevel,
    async_op_put_Completed,
    async_op_get_Completed,
    async_op_GetResults,
};

static HRESULT WINAPI async_op_info_QueryInterface( void *iface, REFIID iid, void **out )
{
    void *op = (void *)((BYTE *)iface - sizeof(void *));
    return async_op_QueryInterface( op, iid, out );
}

static ULONG WINAPI async_op_info_AddRef( void *iface )
{
    struct async_op *impl = (void *)((BYTE *)iface - sizeof(void *));
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI async_op_info_Release( void *iface )
{
    struct async_op *impl = (void *)((BYTE *)iface - sizeof(void *));
    return async_op_Release( impl );
}

static HRESULT WINAPI async_op_info_GetIids( void *iface, ULONG *count, IID **iids )
{
    if (count) *count = 0;
    if (iids) *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI async_op_info_GetRuntimeClassName( void *iface, HSTRING *class_name )
{
    return WindowsCreateString( L"Windows.Foundation.IAsyncInfo", 29, class_name );
}

static HRESULT WINAPI async_op_info_GetTrustLevel( void *iface, TrustLevel *trust_level )
{
    if (trust_level) *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI async_op_info_get_Id( void *iface, UINT32 *id )
{
    if (id) *id = 1;
    return S_OK;
}

static HRESULT WINAPI async_op_info_get_Status( void *iface, UINT32 *status )
{
    if (status) *status = AsyncStatus_Completed;
    return S_OK;
}

static HRESULT WINAPI async_op_info_get_ErrorCode( void *iface, HRESULT *error_code )
{
    if (error_code) *error_code = S_OK;
    return S_OK;
}

static HRESULT WINAPI async_op_info_Cancel( void *iface )
{
    return S_OK;
}

static HRESULT WINAPI async_op_info_Close( void *iface )
{
    return S_OK;
}

static const void *async_op_info_vtbl[] =
{
    async_op_info_QueryInterface,
    async_op_info_AddRef,
    async_op_info_Release,
    async_op_info_GetIids,
    async_op_info_GetRuntimeClassName,
    async_op_info_GetTrustLevel,
    async_op_info_get_Id,
    async_op_info_get_Status,
    async_op_info_get_ErrorCode,
    async_op_info_Cancel,
    async_op_info_Close,
};

static HRESULT create_async_op( void *result, const WCHAR *class_name, void **out )
{
    struct async_op *impl = calloc( 1, sizeof(*impl) );
    if (!impl) return E_OUTOFMEMORY;
    impl->vtbl_op = async_op_vtbl;
    impl->vtbl_info = async_op_info_vtbl;
    impl->ref = 1;
    impl->result = result;
    impl->class_name = class_name;
    *out = impl;
    return S_OK;
}

/* =========================================================================
 * AudioNode Common Implementation
 * ========================================================================= */

static HRESULT WINAPI node_common_get_EffectDefinitions( void *iface, void **value )
{
    if (value) *value = NULL;
    return S_OK;
}

static HRESULT WINAPI node_common_set_OutgoingGain( void *iface, DOUBLE value )
{
    TRACE( "iface %p, gain %f.\n", iface, value );
    return S_OK;
}

static HRESULT WINAPI node_common_get_OutgoingGain( void *iface, DOUBLE *value )
{
    if (value) *value = 1.0;
    return S_OK;
}

static HRESULT WINAPI node_common_get_EncodingProperties( void *iface, void **value )
{
    if (value) *value = NULL;
    return S_OK;
}

static HRESULT WINAPI node_common_get_ConsumeInput( void *iface, boolean *value )
{
    if (value) *value = 1;
    return S_OK;
}

static HRESULT WINAPI node_common_set_ConsumeInput( void *iface, boolean value )
{
    return S_OK;
}

static HRESULT WINAPI node_common_Start( void *iface )
{
    TRACE( "iface %p.\n", iface );
    return S_OK;
}

static HRESULT WINAPI node_common_Stop( void *iface )
{
    TRACE( "iface %p.\n", iface );
    return S_OK;
}

static HRESULT WINAPI node_common_Reset( void *iface )
{
    TRACE( "iface %p.\n", iface );
    return S_OK;
}

static HRESULT WINAPI node_common_DisableEffectsByDefinition( void *iface, void *def )
{
    return S_OK;
}

static HRESULT WINAPI node_common_EnableEffectsByDefinition( void *iface, void *def )
{
    return S_OK;
}

static HRESULT WINAPI input_node_common_get_OutgoingConnections( void *iface, void **value )
{
    if (value) *value = NULL;
    return S_OK;
}

static HRESULT WINAPI input_node_common_AddOutgoingConnection( void *iface, void *dest )
{
    TRACE( "iface %p, dest %p.\n", iface, dest );
    return S_OK;
}

static HRESULT WINAPI input_node_common_AddOutgoingConnectionWithGain( void *iface, void *dest, DOUBLE gain )
{
    TRACE( "iface %p, dest %p, gain %f.\n", iface, dest, gain );
    return S_OK;
}

static HRESULT WINAPI input_node_common_RemoveOutgoingConnection( void *iface, void *dest )
{
    TRACE( "iface %p, dest %p.\n", iface, dest );
    return S_OK;
}

static HRESULT WINAPI closable_common_Close( void *iface )
{
    TRACE( "iface %p.\n", iface );
    return S_OK;
}

/* =========================================================================
 * AudioDeviceInputNode
 * ========================================================================= */

struct device_input_node
{
    const void *vtbl_device_input;
    const void *vtbl_input_node;
    const void *vtbl_node;
    const void *vtbl_closable;
    LONG ref;
};

static inline struct device_input_node *impl_from_device_input( void *iface )
{
    return (struct device_input_node *)iface;
}

static HRESULT WINAPI device_input_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct device_input_node *impl = impl_from_device_input( iface );
    if (!out) return E_POINTER;

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &guid_IAudioDeviceInputNode ))
    {
        *out = &impl->vtbl_device_input;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    if (IsEqualGUID( iid, &guid_IAudioInputNode ))
    {
        *out = &impl->vtbl_input_node;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    if (IsEqualGUID( iid, &guid_IAudioNode ))
    {
        *out = &impl->vtbl_node;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    if (IsEqualGUID( iid, &guid_IClosable ))
    {
        *out = &impl->vtbl_closable;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }

    WARN( "unsupported IID %s\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI device_input_AddRef( void *iface )
{
    struct device_input_node *impl = impl_from_device_input( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI device_input_Release( void *iface )
{
    struct device_input_node *impl = impl_from_device_input( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref) free( impl );
    return ref;
}

static HRESULT WINAPI device_input_GetIids( void *iface, ULONG *count, IID **iids )
{
    if (count) *count = 0;
    if (iids) *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI device_input_GetRuntimeClassName( void *iface, HSTRING *class_name )
{
    return WindowsCreateString( L"Windows.Media.Audio.AudioDeviceInputNode", 39, class_name );
}

static HRESULT WINAPI device_input_GetTrustLevel( void *iface, TrustLevel *trust_level )
{
    if (trust_level) *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI device_input_get_Device( void *iface, void **value )
{
    if (value) *value = NULL;
    return S_OK;
}

static const void *device_input_vtbl[] =
{
    device_input_QueryInterface,
    device_input_AddRef,
    device_input_Release,
    device_input_GetIids,
    device_input_GetRuntimeClassName,
    device_input_GetTrustLevel,
    device_input_get_Device,
};

/* Secondary vtables for AudioDeviceInputNode */
static HRESULT WINAPI input_sec_QueryInterface( void *iface, REFIID iid, void **out )
{
    void *root = (void *)((BYTE *)iface - sizeof(void *));
    return device_input_QueryInterface( root, iid, out );
}
static ULONG WINAPI input_sec_AddRef( void *iface )
{
    void *root = (void *)((BYTE *)iface - sizeof(void *));
    return device_input_AddRef( root );
}
static ULONG WINAPI input_sec_Release( void *iface )
{
    void *root = (void *)((BYTE *)iface - sizeof(void *));
    return device_input_Release( root );
}

static const void *device_input_node_input_vtbl[] =
{
    input_sec_QueryInterface,
    input_sec_AddRef,
    input_sec_Release,
    device_input_GetIids,
    device_input_GetRuntimeClassName,
    device_input_GetTrustLevel,
    input_node_common_get_OutgoingConnections,
    input_node_common_AddOutgoingConnection,
    input_node_common_AddOutgoingConnectionWithGain,
    input_node_common_RemoveOutgoingConnection,
};

static HRESULT WINAPI node_sec_QueryInterface( void *iface, REFIID iid, void **out )
{
    void *root = (void *)((BYTE *)iface - 2 * sizeof(void *));
    return device_input_QueryInterface( root, iid, out );
}
static ULONG WINAPI node_sec_AddRef( void *iface )
{
    void *root = (void *)((BYTE *)iface - 2 * sizeof(void *));
    return device_input_AddRef( root );
}
static ULONG WINAPI node_sec_Release( void *iface )
{
    void *root = (void *)((BYTE *)iface - 2 * sizeof(void *));
    return device_input_Release( root );
}

static const void *device_input_node_node_vtbl[] =
{
    node_sec_QueryInterface,
    node_sec_AddRef,
    node_sec_Release,
    device_input_GetIids,
    device_input_GetRuntimeClassName,
    device_input_GetTrustLevel,
    node_common_get_EffectDefinitions,
    node_common_set_OutgoingGain,
    node_common_get_OutgoingGain,
    node_common_get_EncodingProperties,
    node_common_get_ConsumeInput,
    node_common_set_ConsumeInput,
    node_common_Start,
    node_common_Stop,
    node_common_Reset,
    node_common_DisableEffectsByDefinition,
    node_common_EnableEffectsByDefinition,
};

static HRESULT WINAPI closable_sec_QueryInterface( void *iface, REFIID iid, void **out )
{
    void *root = (void *)((BYTE *)iface - 3 * sizeof(void *));
    return device_input_QueryInterface( root, iid, out );
}
static ULONG WINAPI closable_sec_AddRef( void *iface )
{
    void *root = (void *)((BYTE *)iface - 3 * sizeof(void *));
    return device_input_AddRef( root );
}
static ULONG WINAPI closable_sec_Release( void *iface )
{
    void *root = (void *)((BYTE *)iface - 3 * sizeof(void *));
    return device_input_Release( root );
}

static const void *device_input_closable_vtbl[] =
{
    closable_sec_QueryInterface,
    closable_sec_AddRef,
    closable_sec_Release,
    device_input_GetIids,
    device_input_GetRuntimeClassName,
    device_input_GetTrustLevel,
    closable_common_Close,
};

static HRESULT create_device_input_node( void **out )
{
    struct device_input_node *impl = calloc( 1, sizeof(*impl) );
    if (!impl) return E_OUTOFMEMORY;
    impl->vtbl_device_input = device_input_vtbl;
    impl->vtbl_input_node   = device_input_node_input_vtbl;
    impl->vtbl_node         = device_input_node_node_vtbl;
    impl->vtbl_closable     = device_input_closable_vtbl;
    impl->ref = 1;
    *out = impl;
    return S_OK;
}

/* =========================================================================
 * AudioDeviceOutputNode
 * ========================================================================= */

struct device_output_node
{
    const void *vtbl_device_output;
    const void *vtbl_node;
    const void *vtbl_closable;
    LONG ref;
};

static inline struct device_output_node *impl_from_device_output( void *iface )
{
    return (struct device_output_node *)iface;
}

static HRESULT WINAPI device_output_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct device_output_node *impl = impl_from_device_output( iface );
    if (!out) return E_POINTER;

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &guid_IAudioDeviceOutputNode ))
    {
        *out = &impl->vtbl_device_output;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    if (IsEqualGUID( iid, &guid_IAudioNode ))
    {
        *out = &impl->vtbl_node;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    if (IsEqualGUID( iid, &guid_IClosable ))
    {
        *out = &impl->vtbl_closable;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }

    WARN( "unsupported IID %s\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI device_output_AddRef( void *iface )
{
    struct device_output_node *impl = impl_from_device_output( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI device_output_Release( void *iface )
{
    struct device_output_node *impl = impl_from_device_output( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref) free( impl );
    return ref;
}

static HRESULT WINAPI device_output_GetIids( void *iface, ULONG *count, IID **iids )
{
    if (count) *count = 0;
    if (iids) *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI device_output_GetRuntimeClassName( void *iface, HSTRING *class_name )
{
    return WindowsCreateString( L"Windows.Media.Audio.AudioDeviceOutputNode", 40, class_name );
}

static HRESULT WINAPI device_output_GetTrustLevel( void *iface, TrustLevel *trust_level )
{
    if (trust_level) *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI device_output_get_Device( void *iface, void **value )
{
    if (value) *value = NULL;
    return S_OK;
}

static const void *device_output_vtbl[] =
{
    device_output_QueryInterface,
    device_output_AddRef,
    device_output_Release,
    device_output_GetIids,
    device_output_GetRuntimeClassName,
    device_output_GetTrustLevel,
    device_output_get_Device,
};

static HRESULT WINAPI devout_node_sec_QueryInterface( void *iface, REFIID iid, void **out )
{
    void *root = (void *)((BYTE *)iface - sizeof(void *));
    return device_output_QueryInterface( root, iid, out );
}
static ULONG WINAPI devout_node_sec_AddRef( void *iface )
{
    void *root = (void *)((BYTE *)iface - sizeof(void *));
    return device_output_AddRef( root );
}
static ULONG WINAPI devout_node_sec_Release( void *iface )
{
    void *root = (void *)((BYTE *)iface - sizeof(void *));
    return device_output_Release( root );
}

static const void *device_output_node_vtbl[] =
{
    devout_node_sec_QueryInterface,
    devout_node_sec_AddRef,
    devout_node_sec_Release,
    device_output_GetIids,
    device_output_GetRuntimeClassName,
    device_output_GetTrustLevel,
    node_common_get_EffectDefinitions,
    node_common_set_OutgoingGain,
    node_common_get_OutgoingGain,
    node_common_get_EncodingProperties,
    node_common_get_ConsumeInput,
    node_common_set_ConsumeInput,
    node_common_Start,
    node_common_Stop,
    node_common_Reset,
    node_common_DisableEffectsByDefinition,
    node_common_EnableEffectsByDefinition,
};

static HRESULT WINAPI devout_closable_sec_QueryInterface( void *iface, REFIID iid, void **out )
{
    void *root = (void *)((BYTE *)iface - 2 * sizeof(void *));
    return device_output_QueryInterface( root, iid, out );
}
static ULONG WINAPI devout_closable_sec_AddRef( void *iface )
{
    void *root = (void *)((BYTE *)iface - 2 * sizeof(void *));
    return device_output_AddRef( root );
}
static ULONG WINAPI devout_closable_sec_Release( void *iface )
{
    void *root = (void *)((BYTE *)iface - 2 * sizeof(void *));
    return device_output_Release( root );
}

static const void *device_output_closable_vtbl[] =
{
    devout_closable_sec_QueryInterface,
    devout_closable_sec_AddRef,
    devout_closable_sec_Release,
    device_output_GetIids,
    device_output_GetRuntimeClassName,
    device_output_GetTrustLevel,
    closable_common_Close,
};

static HRESULT create_device_output_node( void **out )
{
    struct device_output_node *impl = calloc( 1, sizeof(*impl) );
    if (!impl) return E_OUTOFMEMORY;
    impl->vtbl_device_output = device_output_vtbl;
    impl->vtbl_node          = device_output_node_vtbl;
    impl->vtbl_closable      = device_output_closable_vtbl;
    impl->ref = 1;
    *out = impl;
    return S_OK;
}

/* =========================================================================
 * AudioFileInputNode
 * ========================================================================= */

struct file_input_node
{
    const void *vtbl_file_input;
    const void *vtbl_input_node;
    const void *vtbl_node;
    const void *vtbl_closable;
    LONG ref;
    void *file;
    DOUBLE speed;
    DOUBLE gain;
};

static inline struct file_input_node *impl_from_file_input( void *iface )
{
    return (struct file_input_node *)iface;
}

static HRESULT WINAPI file_input_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct file_input_node *impl = impl_from_file_input( iface );
    if (!out) return E_POINTER;

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &guid_IAudioFileInputNode ))
    {
        *out = &impl->vtbl_file_input;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    if (IsEqualGUID( iid, &guid_IAudioInputNode ))
    {
        *out = &impl->vtbl_input_node;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    if (IsEqualGUID( iid, &guid_IAudioNode ))
    {
        *out = &impl->vtbl_node;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    if (IsEqualGUID( iid, &guid_IClosable ))
    {
        *out = &impl->vtbl_closable;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }

    WARN( "unsupported IID %s\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI file_input_AddRef( void *iface )
{
    struct file_input_node *impl = impl_from_file_input( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI file_input_Release( void *iface )
{
    struct file_input_node *impl = impl_from_file_input( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        if (impl->file) IUnknown_Release( (IUnknown *)impl->file );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI file_input_GetIids( void *iface, ULONG *count, IID **iids )
{
    if (count) *count = 0;
    if (iids) *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI file_input_GetRuntimeClassName( void *iface, HSTRING *class_name )
{
    return WindowsCreateString( L"Windows.Media.Audio.AudioFileInputNode", 37, class_name );
}

static HRESULT WINAPI file_input_GetTrustLevel( void *iface, TrustLevel *trust_level )
{
    if (trust_level) *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI file_input_set_PlaybackSpeedFactor( void *iface, DOUBLE value )
{
    struct file_input_node *impl = impl_from_file_input( iface );
    impl->speed = value;
    return S_OK;
}

static HRESULT WINAPI file_input_get_PlaybackSpeedFactor( void *iface, DOUBLE *value )
{
    struct file_input_node *impl = impl_from_file_input( iface );
    if (value) *value = impl->speed;
    return S_OK;
}

static HRESULT WINAPI file_input_get_Position( void *iface, INT64 *pos )
{
    if (pos) *pos = 0;
    return S_OK;
}

static HRESULT WINAPI file_input_Seek( void *iface, INT64 pos )
{
    TRACE( "iface %p, pos %lld.\n", iface, (long long)pos );
    return S_OK;
}

static HRESULT WINAPI file_input_get_StartTime( void *iface, void **value )
{
    if (value) *value = NULL;
    return S_OK;
}

static HRESULT WINAPI file_input_set_StartTime( void *iface, void *value )
{
    return S_OK;
}

static HRESULT WINAPI file_input_get_EndTime( void *iface, void **value )
{
    if (value) *value = NULL;
    return S_OK;
}

static HRESULT WINAPI file_input_set_EndTime( void *iface, void *value )
{
    return S_OK;
}

static HRESULT WINAPI file_input_get_LoopCount( void *iface, void **value )
{
    if (value) *value = NULL;
    return S_OK;
}

static HRESULT WINAPI file_input_set_LoopCount( void *iface, void *value )
{
    return S_OK;
}

static HRESULT WINAPI file_input_get_Duration( void *iface, INT64 *duration )
{
    if (duration) *duration = 100000000; /* 10 seconds in 100-ns units */
    return S_OK;
}

static HRESULT WINAPI file_input_get_SourceFile( void *iface, void **value )
{
    struct file_input_node *impl = impl_from_file_input( iface );
    if (!value) return E_POINTER;
    *value = impl->file;
    if (impl->file) IUnknown_AddRef( (IUnknown *)impl->file );
    return S_OK;
}

static HRESULT WINAPI file_input_add_FileCompleted( void *iface, void *handler, INT64 *token )
{
    if (token) *token = 1;
    return S_OK;
}

static HRESULT WINAPI file_input_remove_FileCompleted( void *iface, INT64 token )
{
    return S_OK;
}

static const void *file_input_vtbl[] =
{
    file_input_QueryInterface,
    file_input_AddRef,
    file_input_Release,
    file_input_GetIids,
    file_input_GetRuntimeClassName,
    file_input_GetTrustLevel,
    file_input_set_PlaybackSpeedFactor,
    file_input_get_PlaybackSpeedFactor,
    file_input_get_Position,
    file_input_Seek,
    file_input_get_StartTime,
    file_input_set_StartTime,
    file_input_get_EndTime,
    file_input_set_EndTime,
    file_input_get_LoopCount,
    file_input_set_LoopCount,
    file_input_get_Duration,
    file_input_get_SourceFile,
    file_input_add_FileCompleted,
    file_input_remove_FileCompleted,
};

static HRESULT WINAPI file_input_node_sec_QueryInterface( void *iface, REFIID iid, void **out )
{
    void *root = (void *)((BYTE *)iface - sizeof(void *));
    return file_input_QueryInterface( root, iid, out );
}
static ULONG WINAPI file_input_node_sec_AddRef( void *iface )
{
    void *root = (void *)((BYTE *)iface - sizeof(void *));
    return file_input_AddRef( root );
}
static ULONG WINAPI file_input_node_sec_Release( void *iface )
{
    void *root = (void *)((BYTE *)iface - sizeof(void *));
    return file_input_Release( root );
}

static const void *file_input_node_input_vtbl[] =
{
    file_input_node_sec_QueryInterface,
    file_input_node_sec_AddRef,
    file_input_node_sec_Release,
    file_input_GetIids,
    file_input_GetRuntimeClassName,
    file_input_GetTrustLevel,
    input_node_common_get_OutgoingConnections,
    input_node_common_AddOutgoingConnection,
    input_node_common_AddOutgoingConnectionWithGain,
    input_node_common_RemoveOutgoingConnection,
};

static HRESULT WINAPI file_node_sec_QueryInterface( void *iface, REFIID iid, void **out )
{
    void *root = (void *)((BYTE *)iface - 2 * sizeof(void *));
    return file_input_QueryInterface( root, iid, out );
}
static ULONG WINAPI file_node_sec_AddRef( void *iface )
{
    void *root = (void *)((BYTE *)iface - 2 * sizeof(void *));
    return file_input_AddRef( root );
}
static ULONG WINAPI file_node_sec_Release( void *iface )
{
    void *root = (void *)((BYTE *)iface - 2 * sizeof(void *));
    return file_input_Release( root );
}

static const void *file_input_node_node_vtbl[] =
{
    file_node_sec_QueryInterface,
    file_node_sec_AddRef,
    file_node_sec_Release,
    file_input_GetIids,
    file_input_GetRuntimeClassName,
    file_input_GetTrustLevel,
    node_common_get_EffectDefinitions,
    node_common_set_OutgoingGain,
    node_common_get_OutgoingGain,
    node_common_get_EncodingProperties,
    node_common_get_ConsumeInput,
    node_common_set_ConsumeInput,
    node_common_Start,
    node_common_Stop,
    node_common_Reset,
    node_common_DisableEffectsByDefinition,
    node_common_EnableEffectsByDefinition,
};

static HRESULT WINAPI file_closable_sec_QueryInterface( void *iface, REFIID iid, void **out )
{
    void *root = (void *)((BYTE *)iface - 3 * sizeof(void *));
    return file_input_QueryInterface( root, iid, out );
}
static ULONG WINAPI file_closable_sec_AddRef( void *iface )
{
    void *root = (void *)((BYTE *)iface - 3 * sizeof(void *));
    return file_input_AddRef( root );
}
static ULONG WINAPI file_closable_sec_Release( void *iface )
{
    void *root = (void *)((BYTE *)iface - 3 * sizeof(void *));
    return file_input_Release( root );
}

static const void *file_input_closable_vtbl[] =
{
    file_closable_sec_QueryInterface,
    file_closable_sec_AddRef,
    file_closable_sec_Release,
    file_input_GetIids,
    file_input_GetRuntimeClassName,
    file_input_GetTrustLevel,
    closable_common_Close,
};

static HRESULT create_file_input_node( void *file, void **out )
{
    struct file_input_node *impl = calloc( 1, sizeof(*impl) );
    if (!impl) return E_OUTOFMEMORY;
    impl->vtbl_file_input = file_input_vtbl;
    impl->vtbl_input_node = file_input_node_input_vtbl;
    impl->vtbl_node       = file_input_node_node_vtbl;
    impl->vtbl_closable   = file_input_closable_vtbl;
    impl->ref = 1;
    impl->speed = 1.0;
    impl->gain = 1.0;
    impl->file = file;
    if (file) IUnknown_AddRef( (IUnknown *)file );
    *out = impl;
    return S_OK;
}

/* =========================================================================
 * Result Objects
 * ========================================================================= */

struct node_result
{
    const void *vtbl_result;
    const void *vtbl_result2;
    LONG ref;
    void *node;
    const WCHAR *class_name;
    const GUID *guid_result;
    const GUID *guid_result2;
};

static inline struct node_result *impl_from_result( void *iface )
{
    return (struct node_result *)iface;
}

static HRESULT WINAPI result_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct node_result *impl = impl_from_result( iface );
    if (!out) return E_POINTER;

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, impl->guid_result ))
    {
        *out = &impl->vtbl_result;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    if (IsEqualGUID( iid, impl->guid_result2 ))
    {
        *out = &impl->vtbl_result2;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }

    WARN( "unsupported IID %s\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI result_AddRef( void *iface )
{
    struct node_result *impl = impl_from_result( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI result_Release( void *iface )
{
    struct node_result *impl = impl_from_result( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        if (impl->node) IUnknown_Release( (IUnknown *)impl->node );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI result_GetIids( void *iface, ULONG *count, IID **iids )
{
    if (count) *count = 0;
    if (iids) *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI result_GetRuntimeClassName( void *iface, HSTRING *class_name )
{
    struct node_result *impl = impl_from_result( iface );
    return WindowsCreateString( impl->class_name, wcslen( impl->class_name ), class_name );
}

static HRESULT WINAPI result_GetTrustLevel( void *iface, TrustLevel *trust_level )
{
    if (trust_level) *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI result_get_Status( void *iface, UINT32 *status )
{
    if (status) *status = 0; /* Success */
    return S_OK;
}

static HRESULT WINAPI result_get_Node( void *iface, void **node )
{
    struct node_result *impl = impl_from_result( iface );
    if (!node) return E_POINTER;
    *node = impl->node;
    if (impl->node) IUnknown_AddRef( (IUnknown *)impl->node );
    return S_OK;
}

static const void *node_result_vtbl[] =
{
    result_QueryInterface,
    result_AddRef,
    result_Release,
    result_GetIids,
    result_GetRuntimeClassName,
    result_GetTrustLevel,
    result_get_Status,
    result_get_Node,
};

static HRESULT WINAPI result2_sec_QueryInterface( void *iface, REFIID iid, void **out )
{
    void *root = (void *)((BYTE *)iface - sizeof(void *));
    return result_QueryInterface( root, iid, out );
}
static ULONG WINAPI result2_sec_AddRef( void *iface )
{
    void *root = (void *)((BYTE *)iface - sizeof(void *));
    return result_AddRef( root );
}
static ULONG WINAPI result2_sec_Release( void *iface )
{
    void *root = (void *)((BYTE *)iface - sizeof(void *));
    return result_Release( root );
}

static HRESULT WINAPI result2_get_ExtendedError( void *iface, HRESULT *error )
{
    if (error) *error = S_OK;
    return S_OK;
}

static const void *node_result2_vtbl[] =
{
    result2_sec_QueryInterface,
    result2_sec_AddRef,
    result2_sec_Release,
    result_GetIids,
    result_GetRuntimeClassName,
    result_GetTrustLevel,
    result2_get_ExtendedError,
};

static HRESULT create_node_result( void *node, const WCHAR *class_name,
                                  const GUID *guid_res, const GUID *guid_res2, void **out )
{
    struct node_result *impl = calloc( 1, sizeof(*impl) );
    if (!impl) return E_OUTOFMEMORY;
    impl->vtbl_result  = node_result_vtbl;
    impl->vtbl_result2 = node_result2_vtbl;
    impl->ref = 1;
    impl->node = node;
    impl->class_name = class_name;
    impl->guid_result = guid_res;
    impl->guid_result2 = guid_res2;
    *out = impl;
    return S_OK;
}

/* =========================================================================
 * AudioGraph
 * ========================================================================= */

struct audio_graph
{
    const void *vtbl_graph;
    const void *vtbl_closable;
    LONG ref;
};

static inline struct audio_graph *impl_from_graph( void *iface )
{
    return (struct audio_graph *)iface;
}

static HRESULT WINAPI graph_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct audio_graph *impl = impl_from_graph( iface );
    if (!out) return E_POINTER;

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &guid_IAudioGraph ))
    {
        *out = &impl->vtbl_graph;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    if (IsEqualGUID( iid, &guid_IClosable ))
    {
        *out = &impl->vtbl_closable;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }

    WARN( "unsupported IID %s\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI graph_AddRef( void *iface )
{
    struct audio_graph *impl = impl_from_graph( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI graph_Release( void *iface )
{
    struct audio_graph *impl = impl_from_graph( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref) free( impl );
    return ref;
}

static HRESULT WINAPI graph_GetIids( void *iface, ULONG *count, IID **iids )
{
    if (count) *count = 0;
    if (iids) *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI graph_GetRuntimeClassName( void *iface, HSTRING *class_name )
{
    return WindowsCreateString( L"Windows.Media.Audio.AudioGraph", 30, class_name );
}

static HRESULT WINAPI graph_GetTrustLevel( void *iface, TrustLevel *trust_level )
{
    if (trust_level) *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI graph_CreateFrameInputNode( void *iface, void **out )
{
    FIXME( "stub!\n" );
    return E_NOTIMPL;
}

static HRESULT WINAPI graph_CreateFrameInputNode2( void *iface, void *props, void **out )
{
    FIXME( "stub!\n" );
    return E_NOTIMPL;
}

static HRESULT WINAPI graph_CreateDeviceInputNodeAsync( void *iface, UINT32 category, void **operation )
{
    void *node = NULL, *res = NULL;
    HRESULT hr;

    TRACE( "iface %p, category %u, operation %p.\n", iface, category, operation );

    if ((hr = create_device_input_node( &node )) != S_OK) return hr;
    if ((hr = create_node_result( node, L"Windows.Media.Audio.CreateAudioDeviceInputNodeResult",
                                  &guid_ICreateAudioDeviceInputNodeResult,
                                  &guid_ICreateAudioDeviceInputNodeResult2, &res )) != S_OK)
    {
        IUnknown_Release( (IUnknown *)node );
        return hr;
    }

    return create_async_op( res, L"Windows.Foundation.IAsyncOperation`1<Windows.Media.Audio.CreateAudioDeviceInputNodeResult>", operation );
}

static HRESULT WINAPI graph_CreateDeviceInputNodeAsync2( void *iface, UINT32 category, void *props, void **operation )
{
    return graph_CreateDeviceInputNodeAsync( iface, category, operation );
}

static HRESULT WINAPI graph_CreateDeviceInputNodeAsync3( void *iface, UINT32 category, void *props, void *device, void **operation )
{
    return graph_CreateDeviceInputNodeAsync( iface, category, operation );
}

static HRESULT WINAPI graph_CreateFrameOutputNode( void *iface, void **out )
{
    FIXME( "stub!\n" );
    return E_NOTIMPL;
}

static HRESULT WINAPI graph_CreateFrameOutputNode2( void *iface, void *props, void **out )
{
    FIXME( "stub!\n" );
    return E_NOTIMPL;
}

static HRESULT WINAPI graph_CreateDeviceOutputNodeAsync( void *iface, void **operation )
{
    void *node = NULL, *res = NULL;
    HRESULT hr;

    TRACE( "iface %p, operation %p.\n", iface, operation );

    if ((hr = create_device_output_node( &node )) != S_OK) return hr;
    if ((hr = create_node_result( node, L"Windows.Media.Audio.CreateAudioDeviceOutputNodeResult",
                                  &guid_ICreateAudioDeviceOutputNodeResult,
                                  &guid_ICreateAudioDeviceOutputNodeResult2, &res )) != S_OK)
    {
        IUnknown_Release( (IUnknown *)node );
        return hr;
    }

    return create_async_op( res, L"Windows.Foundation.IAsyncOperation`1<Windows.Media.Audio.CreateAudioDeviceOutputNodeResult>", operation );
}

static HRESULT WINAPI graph_CreateFileInputNodeAsync( void *iface, void *file, void **operation )
{
    void *node = NULL, *res = NULL;
    HRESULT hr;

    TRACE( "iface %p, file %p, operation %p.\n", iface, file, operation );

    if ((hr = create_file_input_node( file, &node )) != S_OK) return hr;
    if ((hr = create_node_result( node, L"Windows.Media.Audio.CreateAudioFileInputNodeResult",
                                  &guid_ICreateAudioFileInputNodeResult,
                                  &guid_ICreateAudioFileInputNodeResult2, &res )) != S_OK)
    {
        IUnknown_Release( (IUnknown *)node );
        return hr;
    }

    return create_async_op( res, L"Windows.Foundation.IAsyncOperation`1<Windows.Media.Audio.CreateAudioFileInputNodeResult>", operation );
}

static HRESULT WINAPI graph_CreateFileOutputNodeAsync( void *iface, void *file, void **operation )
{
    FIXME( "stub!\n" );
    return E_NOTIMPL;
}

static HRESULT WINAPI graph_CreateFileOutputNodeAsync2( void *iface, void *file, void *profile, void **operation )
{
    FIXME( "stub!\n" );
    return E_NOTIMPL;
}

static HRESULT WINAPI graph_CreateSubmixNode( void *iface, void **out )
{
    FIXME( "stub!\n" );
    return E_NOTIMPL;
}

static HRESULT WINAPI graph_CreateSubmixNode2( void *iface, void *props, void **out )
{
    FIXME( "stub!\n" );
    return E_NOTIMPL;
}

static HRESULT WINAPI graph_Start( void *iface )
{
    TRACE( "iface %p starting AudioGraph processing.\n", iface );
    return S_OK;
}

static HRESULT WINAPI graph_Stop( void *iface )
{
    TRACE( "iface %p stopping AudioGraph processing.\n", iface );
    return S_OK;
}

static HRESULT WINAPI graph_ResetAllNodes( void *iface )
{
    TRACE( "iface %p resetting all nodes.\n", iface );
    return S_OK;
}

static HRESULT WINAPI graph_add_QuantumProcessed( void *iface, void *handler, INT64 *token )
{
    if (token) *token = 1;
    return S_OK;
}

static HRESULT WINAPI graph_remove_QuantumProcessed( void *iface, INT64 token )
{
    return S_OK;
}

static HRESULT WINAPI graph_add_QuantumStarted( void *iface, void *handler, INT64 *token )
{
    if (token) *token = 1;
    return S_OK;
}

static HRESULT WINAPI graph_remove_QuantumStarted( void *iface, INT64 token )
{
    return S_OK;
}

static HRESULT WINAPI graph_add_UnrecoverableErrorOccurred( void *iface, void *handler, INT64 *token )
{
    if (token) *token = 1;
    return S_OK;
}

static HRESULT WINAPI graph_remove_UnrecoverableErrorOccurred( void *iface, INT64 token )
{
    return S_OK;
}

static HRESULT WINAPI graph_get_CompletedQuantumCount( void *iface, UINT64 *count )
{
    if (count) *count = 0;
    return S_OK;
}

static HRESULT WINAPI graph_get_EncodingProperties( void *iface, void **props )
{
    if (props) *props = NULL;
    return S_OK;
}

static HRESULT WINAPI graph_get_LatencyInSamples( void *iface, INT32 *samples )
{
    if (samples) *samples = 480;
    return S_OK;
}

static HRESULT WINAPI graph_get_PrimaryRenderDevice( void *iface, void **device )
{
    if (device) *device = NULL;
    return S_OK;
}

static HRESULT WINAPI graph_get_RenderDeviceAudioProcessing( void *iface, UINT32 *proc )
{
    if (proc) *proc = 0;
    return S_OK;
}

static HRESULT WINAPI graph_get_SamplesPerQuantum( void *iface, INT32 *samples )
{
    if (samples) *samples = 480;
    return S_OK;
}

static const void *audio_graph_vtbl[] =
{
    graph_QueryInterface,
    graph_AddRef,
    graph_Release,
    graph_GetIids,
    graph_GetRuntimeClassName,
    graph_GetTrustLevel,
    graph_CreateFrameInputNode,
    graph_CreateFrameInputNode2,
    graph_CreateDeviceInputNodeAsync,
    graph_CreateDeviceInputNodeAsync2,
    graph_CreateDeviceInputNodeAsync3,
    graph_CreateFrameOutputNode,
    graph_CreateFrameOutputNode2,
    graph_CreateDeviceOutputNodeAsync,
    graph_CreateFileInputNodeAsync,
    graph_CreateFileOutputNodeAsync,
    graph_CreateFileOutputNodeAsync2,
    graph_CreateSubmixNode,
    graph_CreateSubmixNode2,
    graph_Start,
    graph_Stop,
    graph_ResetAllNodes,
    graph_add_QuantumProcessed,
    graph_remove_QuantumProcessed,
    graph_add_QuantumStarted,
    graph_remove_QuantumStarted,
    graph_add_UnrecoverableErrorOccurred,
    graph_remove_UnrecoverableErrorOccurred,
    graph_get_CompletedQuantumCount,
    graph_get_EncodingProperties,
    graph_get_LatencyInSamples,
    graph_get_PrimaryRenderDevice,
    graph_get_RenderDeviceAudioProcessing,
    graph_get_SamplesPerQuantum,
};

static HRESULT WINAPI graph_closable_sec_QueryInterface( void *iface, REFIID iid, void **out )
{
    void *root = (void *)((BYTE *)iface - sizeof(void *));
    return graph_QueryInterface( root, iid, out );
}
static ULONG WINAPI graph_closable_sec_AddRef( void *iface )
{
    void *root = (void *)((BYTE *)iface - sizeof(void *));
    return graph_AddRef( root );
}
static ULONG WINAPI graph_closable_sec_Release( void *iface )
{
    void *root = (void *)((BYTE *)iface - sizeof(void *));
    return graph_Release( root );
}

static const void *audio_graph_closable_vtbl[] =
{
    graph_closable_sec_QueryInterface,
    graph_closable_sec_AddRef,
    graph_closable_sec_Release,
    graph_GetIids,
    graph_GetRuntimeClassName,
    graph_GetTrustLevel,
    closable_common_Close,
};

static HRESULT create_audio_graph( void **out )
{
    struct audio_graph *impl = calloc( 1, sizeof(*impl) );
    if (!impl) return E_OUTOFMEMORY;
    impl->vtbl_graph    = audio_graph_vtbl;
    impl->vtbl_closable = audio_graph_closable_vtbl;
    impl->ref = 1;
    *out = impl;
    return S_OK;
}

/* =========================================================================
 * AudioGraphSettings
 * ========================================================================= */

struct audio_graph_settings
{
    const void *vtbl_settings;
    const void *vtbl_settings2;
    LONG ref;
    UINT32 category;
    UINT32 processing;
    INT32 samples;
};

static inline struct audio_graph_settings *impl_from_settings( void *iface )
{
    return (struct audio_graph_settings *)iface;
}

static HRESULT WINAPI settings_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct audio_graph_settings *impl = impl_from_settings( iface );
    if (!out) return E_POINTER;

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &guid_IAudioGraphSettings ))
    {
        *out = &impl->vtbl_settings;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    if (IsEqualGUID( iid, &guid_IAudioGraphSettings2 ))
    {
        *out = &impl->vtbl_settings2;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }

    WARN( "unsupported IID %s\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI settings_AddRef( void *iface )
{
    struct audio_graph_settings *impl = impl_from_settings( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI settings_Release( void *iface )
{
    struct audio_graph_settings *impl = impl_from_settings( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref) free( impl );
    return ref;
}

static HRESULT WINAPI settings_GetIids( void *iface, ULONG *count, IID **iids )
{
    if (count) *count = 0;
    if (iids) *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI settings_GetRuntimeClassName( void *iface, HSTRING *class_name )
{
    return WindowsCreateString( L"Windows.Media.Audio.AudioGraphSettings", 38, class_name );
}

static HRESULT WINAPI settings_GetTrustLevel( void *iface, TrustLevel *trust_level )
{
    if (trust_level) *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI settings_get_AudioRenderCategory( void *iface, UINT32 *value )
{
    struct audio_graph_settings *impl = impl_from_settings( iface );
    if (value) *value = impl->category;
    return S_OK;
}

static HRESULT WINAPI settings_set_AudioRenderCategory( void *iface, UINT32 value )
{
    struct audio_graph_settings *impl = impl_from_settings( iface );
    impl->category = value;
    return S_OK;
}

static HRESULT WINAPI settings_get_DesiredRenderDeviceAudioProcessing( void *iface, UINT32 *value )
{
    struct audio_graph_settings *impl = impl_from_settings( iface );
    if (value) *value = impl->processing;
    return S_OK;
}

static HRESULT WINAPI settings_set_DesiredRenderDeviceAudioProcessing( void *iface, UINT32 value )
{
    struct audio_graph_settings *impl = impl_from_settings( iface );
    impl->processing = value;
    return S_OK;
}

static HRESULT WINAPI settings_get_DesiredSamplesPerQuantum( void *iface, INT32 *value )
{
    struct audio_graph_settings *impl = impl_from_settings( iface );
    if (value) *value = impl->samples;
    return S_OK;
}

static HRESULT WINAPI settings_set_DesiredSamplesPerQuantum( void *iface, INT32 value )
{
    struct audio_graph_settings *impl = impl_from_settings( iface );
    impl->samples = value;
    return S_OK;
}

static HRESULT WINAPI settings_get_EncodingProperties( void *iface, void **value )
{
    if (value) *value = NULL;
    return S_OK;
}

static HRESULT WINAPI settings_set_EncodingProperties( void *iface, void *value )
{
    return S_OK;
}

static HRESULT WINAPI settings_get_PrimaryRenderDevice( void *iface, void **value )
{
    if (value) *value = NULL;
    return S_OK;
}

static HRESULT WINAPI settings_set_PrimaryRenderDevice( void *iface, void *value )
{
    return S_OK;
}

static HRESULT WINAPI settings_get_QuantumSizeSelectionMode( void *iface, UINT32 *value )
{
    if (value) *value = 0;
    return S_OK;
}

static HRESULT WINAPI settings_set_QuantumSizeSelectionMode( void *iface, UINT32 value )
{
    return S_OK;
}

static const void *audio_graph_settings_vtbl[] =
{
    settings_QueryInterface,
    settings_AddRef,
    settings_Release,
    settings_GetIids,
    settings_GetRuntimeClassName,
    settings_GetTrustLevel,
    settings_get_AudioRenderCategory,
    settings_set_AudioRenderCategory,
    settings_get_DesiredRenderDeviceAudioProcessing,
    settings_set_DesiredRenderDeviceAudioProcessing,
    settings_get_DesiredSamplesPerQuantum,
    settings_set_DesiredSamplesPerQuantum,
    settings_get_EncodingProperties,
    settings_set_EncodingProperties,
    settings_get_PrimaryRenderDevice,
    settings_set_PrimaryRenderDevice,
    settings_get_QuantumSizeSelectionMode,
    settings_set_QuantumSizeSelectionMode,
};

static HRESULT WINAPI settings2_sec_QueryInterface( void *iface, REFIID iid, void **out )
{
    void *root = (void *)((BYTE *)iface - sizeof(void *));
    return settings_QueryInterface( root, iid, out );
}
static ULONG WINAPI settings2_sec_AddRef( void *iface )
{
    void *root = (void *)((BYTE *)iface - sizeof(void *));
    return settings_AddRef( root );
}
static ULONG WINAPI settings2_sec_Release( void *iface )
{
    void *root = (void *)((BYTE *)iface - sizeof(void *));
    return settings_Release( root );
}

static HRESULT WINAPI settings2_get_MaxPlaybackSpeedFactor( void *iface, DOUBLE *value )
{
    if (value) *value = 2.0;
    return S_OK;
}

static HRESULT WINAPI settings2_set_MaxPlaybackSpeedFactor( void *iface, DOUBLE value )
{
    return S_OK;
}

static const void *audio_graph_settings2_vtbl[] =
{
    settings2_sec_QueryInterface,
    settings2_sec_AddRef,
    settings2_sec_Release,
    settings_GetIids,
    settings_GetRuntimeClassName,
    settings_GetTrustLevel,
    settings2_get_MaxPlaybackSpeedFactor,
    settings2_set_MaxPlaybackSpeedFactor,
};

static HRESULT create_audio_graph_settings( UINT32 category, void **out )
{
    struct audio_graph_settings *impl = calloc( 1, sizeof(*impl) );
    if (!impl) return E_OUTOFMEMORY;
    impl->vtbl_settings  = audio_graph_settings_vtbl;
    impl->vtbl_settings2 = audio_graph_settings2_vtbl;
    impl->ref = 1;
    impl->category = category;
    impl->samples = 480;
    *out = impl;
    return S_OK;
}

/* =========================================================================
 * AudioGraph Factory (IAudioGraphStatics)
 * ========================================================================= */

struct audio_graph_factory
{
    IActivationFactory IActivationFactory_iface;
    LONG ref;
};

static inline struct audio_graph_factory *impl_from_IActivationFactory( IActivationFactory *iface )
{
    return CONTAINING_RECORD( iface, struct audio_graph_factory, IActivationFactory_iface );
}

static HRESULT WINAPI graph_factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
    struct audio_graph_factory *impl = impl_from_IActivationFactory( iface );
    if (!out) return E_POINTER;

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ) ||
        IsEqualGUID( iid, &guid_IAudioGraphStatics ))
    {
        *out = &impl->IActivationFactory_iface;
        IActivationFactory_AddRef( iface );
        return S_OK;
    }

    WARN( "unsupported IID %s\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI graph_factory_AddRef( IActivationFactory *iface )
{
    struct audio_graph_factory *impl = impl_from_IActivationFactory( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI graph_factory_Release( IActivationFactory *iface )
{
    struct audio_graph_factory *impl = impl_from_IActivationFactory( iface );
    return InterlockedDecrement( &impl->ref );
}

static HRESULT WINAPI graph_factory_GetIids( IActivationFactory *iface, ULONG *count, IID **iids )
{
    if (count) *count = 0;
    if (iids) *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI graph_factory_GetRuntimeClassName( IActivationFactory *iface, HSTRING *class_name )
{
    return WindowsCreateString( L"Windows.Media.Audio.AudioGraph", 30, class_name );
}

static HRESULT WINAPI graph_factory_GetTrustLevel( IActivationFactory *iface, TrustLevel *trust_level )
{
    if (trust_level) *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI graph_factory_ActivateInstance( IActivationFactory *iface, IInspectable **instance )
{
    FIXME( "AudioGraph cannot be activated directly, use CreateAsync.\n" );
    return E_NOTIMPL;
}

/* Slot 6 of IAudioGraphStatics is CreateAsync */
static HRESULT WINAPI graph_factory_CreateAsync( void *iface, void *settings, void **operation )
{
    void *graph = NULL, *res = NULL;
    HRESULT hr;

    TRACE( "iface %p, settings %p, operation %p.\n", iface, settings, operation );

    if ((hr = create_audio_graph( &graph )) != S_OK) return hr;
    if ((hr = create_node_result( graph, L"Windows.Media.Audio.CreateAudioGraphResult",
                                  &guid_ICreateAudioGraphResult,
                                  &guid_ICreateAudioGraphResult2, &res )) != S_OK)
    {
        IUnknown_Release( (IUnknown *)graph );
        return hr;
    }

    return create_async_op( res, L"Windows.Foundation.IAsyncOperation`1<Windows.Media.Audio.CreateAudioGraphResult>", operation );
}

/* Vtable combining IActivationFactory and IAudioGraphStatics */
static const struct
{
    HRESULT (WINAPI *QueryInterface)(IActivationFactory *iface, REFIID iid, void **out);
    ULONG   (WINAPI *AddRef)(IActivationFactory *iface);
    ULONG   (WINAPI *Release)(IActivationFactory *iface);
    HRESULT (WINAPI *GetIids)(IActivationFactory *iface, ULONG *count, IID **iids);
    HRESULT (WINAPI *GetRuntimeClassName)(IActivationFactory *iface, HSTRING *class_name);
    HRESULT (WINAPI *GetTrustLevel)(IActivationFactory *iface, TrustLevel *trust_level);
    HRESULT (WINAPI *ActivateInstance)(IActivationFactory *iface, IInspectable **instance);
    HRESULT (WINAPI *CreateAsync)(void *iface, void *settings, void **operation);
} audio_graph_factory_vtbl =
{
    graph_factory_QueryInterface,
    graph_factory_AddRef,
    graph_factory_Release,
    graph_factory_GetIids,
    graph_factory_GetRuntimeClassName,
    graph_factory_GetTrustLevel,
    graph_factory_ActivateInstance,
    graph_factory_CreateAsync,
};

static struct audio_graph_factory default_audio_graph_factory =
{
    .IActivationFactory_iface.lpVtbl = (void *)&audio_graph_factory_vtbl,
    .ref = 1,
};

IActivationFactory *audio_graph_factory = &default_audio_graph_factory.IActivationFactory_iface;

/* =========================================================================
 * AudioGraphSettings Factory (IAudioGraphSettingsFactory)
 * ========================================================================= */

struct settings_factory
{
    IActivationFactory IActivationFactory_iface;
    LONG ref;
};

static inline struct settings_factory *impl_from_settings_factory( IActivationFactory *iface )
{
    return CONTAINING_RECORD( iface, struct settings_factory, IActivationFactory_iface );
}

static HRESULT WINAPI settings_factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
    struct settings_factory *impl = impl_from_settings_factory( iface );
    if (!out) return E_POINTER;

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ) ||
        IsEqualGUID( iid, &guid_IAudioGraphSettingsFactory ))
    {
        *out = &impl->IActivationFactory_iface;
        IActivationFactory_AddRef( iface );
        return S_OK;
    }

    WARN( "unsupported IID %s\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI settings_factory_AddRef( IActivationFactory *iface )
{
    struct settings_factory *impl = impl_from_settings_factory( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI settings_factory_Release( IActivationFactory *iface )
{
    struct settings_factory *impl = impl_from_settings_factory( iface );
    return InterlockedDecrement( &impl->ref );
}

static HRESULT WINAPI settings_factory_GetIids( IActivationFactory *iface, ULONG *count, IID **iids )
{
    if (count) *count = 0;
    if (iids) *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI settings_factory_GetRuntimeClassName( IActivationFactory *iface, HSTRING *class_name )
{
    return WindowsCreateString( L"Windows.Media.Audio.AudioGraphSettings", 38, class_name );
}

static HRESULT WINAPI settings_factory_GetTrustLevel( IActivationFactory *iface, TrustLevel *trust_level )
{
    if (trust_level) *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI settings_factory_ActivateInstance( IActivationFactory *iface, IInspectable **instance )
{
    return create_audio_graph_settings( 0, (void **)instance );
}

static HRESULT WINAPI settings_factory_Create( void *iface, UINT32 category, void **out )
{
    TRACE( "iface %p, category %u, out %p.\n", iface, category, out );
    return create_audio_graph_settings( category, out );
}

static const struct
{
    HRESULT (WINAPI *QueryInterface)(IActivationFactory *iface, REFIID iid, void **out);
    ULONG   (WINAPI *AddRef)(IActivationFactory *iface);
    ULONG   (WINAPI *Release)(IActivationFactory *iface);
    HRESULT (WINAPI *GetIids)(IActivationFactory *iface, ULONG *count, IID **iids);
    HRESULT (WINAPI *GetRuntimeClassName)(IActivationFactory *iface, HSTRING *class_name);
    HRESULT (WINAPI *GetTrustLevel)(IActivationFactory *iface, TrustLevel *trust_level);
    HRESULT (WINAPI *ActivateInstance)(IActivationFactory *iface, IInspectable **instance);
    HRESULT (WINAPI *Create)(void *iface, UINT32 category, void **out);
} audio_graph_settings_factory_vtbl =
{
    settings_factory_QueryInterface,
    settings_factory_AddRef,
    settings_factory_Release,
    settings_factory_GetIids,
    settings_factory_GetRuntimeClassName,
    settings_factory_GetTrustLevel,
    settings_factory_ActivateInstance,
    settings_factory_Create,
};

static struct settings_factory default_settings_factory =
{
    .IActivationFactory_iface.lpVtbl = (void *)&audio_graph_settings_factory_vtbl,
    .ref = 1,
};

IActivationFactory *audio_graph_settings_factory = &default_settings_factory.IActivationFactory_iface;
