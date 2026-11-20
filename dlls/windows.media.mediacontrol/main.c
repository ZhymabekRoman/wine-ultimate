/* WinRT Windows.Media.MediaControl Implementation
 *
 * Copyright (C) 2023 Mohamad Al-Jaf
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

#include "initguid.h"
#include "private.h"
#include "winuser.h"
#include "unixlib.h"

#include "ntstatus.h"
#define WIN32_NO_STATUS
#include "wine/debug.h"
#include "wine/unixlib.h"

WINE_DEFAULT_DEBUG_CHANNEL(mediacontrol);

static EventRegistrationToken dummy_token = {.value = 0xdeadbeef};

struct media_control_statics
{
    IActivationFactory IActivationFactory_iface;
    ISystemMediaTransportControlsInterop ISystemMediaTransportControlsInterop_iface;
    ISystemMediaTransportControlsStatics ISystemMediaTransportControlsStatics_iface;
    LONG ref;
};

static inline struct media_control_statics *impl_from_IActivationFactory( IActivationFactory *iface )
{
    return CONTAINING_RECORD( iface, struct media_control_statics, IActivationFactory_iface );
}

static HRESULT WINAPI factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
    struct media_control_statics *impl = impl_from_IActivationFactory( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ))
    {
        *out = &impl->IActivationFactory_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_ISystemMediaTransportControlsInterop ))
    {
        *out = &impl->ISystemMediaTransportControlsInterop_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_ISystemMediaTransportControlsStatics ))
    {
        *out = &impl->ISystemMediaTransportControlsStatics_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI factory_AddRef( IActivationFactory *iface )
{
    struct media_control_statics *impl = impl_from_IActivationFactory( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "iface %p increasing refcount to %lu.\n", iface, ref );
    return ref;
}

static ULONG WINAPI factory_Release( IActivationFactory *iface )
{
    struct media_control_statics *impl = impl_from_IActivationFactory( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    TRACE( "iface %p decreasing refcount to %lu.\n", iface, ref );
    return ref;
}

static HRESULT WINAPI factory_GetIids( IActivationFactory *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI factory_GetRuntimeClassName( IActivationFactory *iface, HSTRING *class_name )
{
    FIXME( "iface %p, class_name %p stub!\n", iface, class_name );
    return E_NOTIMPL;
}

static HRESULT WINAPI factory_GetTrustLevel( IActivationFactory *iface, TrustLevel *trust_level )
{
    FIXME( "iface %p, trust_level %p stub!\n", iface, trust_level );
    return E_NOTIMPL;
}

static HRESULT media_control_activate( IInspectable **instance );

static HRESULT WINAPI factory_ActivateInstance( IActivationFactory *iface, IInspectable **instance )
{
    TRACE( "iface %p, instance %p.\n", iface, instance );

    return media_control_activate( instance );
}

static const struct IActivationFactoryVtbl factory_vtbl =
{
    factory_QueryInterface,
    factory_AddRef,
    factory_Release,
    /* IInspectable methods */
    factory_GetIids,
    factory_GetRuntimeClassName,
    factory_GetTrustLevel,
    /* IActivationFactory methods */
    factory_ActivateInstance,
};

struct music_properties
{
    IMusicDisplayProperties IMusicDisplayProperties_iface;
    IMusicDisplayProperties2 IMusicDisplayProperties2_iface;
    LONG ref;

    HSTRING album_title;
    HSTRING album_artist;
    HSTRING artist;
    HSTRING title;
};

static inline struct music_properties *impl_from_IMusicDisplayProperties( IMusicDisplayProperties *iface )
{
    return CONTAINING_RECORD( iface, struct music_properties, IMusicDisplayProperties_iface );
}

static HRESULT WINAPI music_properties_QueryInterface( IMusicDisplayProperties *iface, REFIID iid, void **out )
{
    struct music_properties *impl = impl_from_IMusicDisplayProperties( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IMusicDisplayProperties ))
    {
        *out = &impl->IMusicDisplayProperties_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IMusicDisplayProperties2 ))
    {
        *out = &impl->IMusicDisplayProperties2_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI music_properties_AddRef( IMusicDisplayProperties *iface )
{
    struct music_properties *impl = impl_from_IMusicDisplayProperties( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "iface %p increasing refcount to %lu.\n", iface, ref );
    return ref;
}

static ULONG WINAPI music_properties_Release( IMusicDisplayProperties *iface )
{
    struct music_properties *impl = impl_from_IMusicDisplayProperties( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    TRACE( "iface %p decreasing refcount to %lu.\n", iface, ref );

    if (!ref)
    {
        WindowsDeleteString( impl->album_title );
        WindowsDeleteString( impl->album_artist );
        WindowsDeleteString( impl->artist );
        WindowsDeleteString( impl->title );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI music_properties_GetIids( IMusicDisplayProperties *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI music_properties_GetRuntimeClassName( IMusicDisplayProperties *iface, HSTRING *class_name )
{
    FIXME( "iface %p, class_name %p stub!\n", iface, class_name );
    return E_NOTIMPL;
}

static HRESULT WINAPI music_properties_GetTrustLevel( IMusicDisplayProperties *iface, TrustLevel *trust_level )
{
    FIXME( "iface %p, trust_level %p stub!\n", iface, trust_level );
    return E_NOTIMPL;
}

static HRESULT WINAPI music_properties_get_Title( IMusicDisplayProperties *iface, HSTRING *value )
{
    struct music_properties *impl = impl_from_IMusicDisplayProperties( iface );
    TRACE( "iface %p, value %p\n", iface, value );
    return WindowsDuplicateString( impl->title, value );
}

static HRESULT WINAPI music_properties_put_Title( IMusicDisplayProperties *iface, HSTRING value )
{
    struct music_properties *impl = impl_from_IMusicDisplayProperties( iface );
    TRACE( "iface %p, value %p\n", iface, value );
    WindowsDeleteString( impl->title );
    return WindowsDuplicateString( value, &impl->title );
}

static HRESULT WINAPI music_properties_get_AlbumArtist( IMusicDisplayProperties *iface, HSTRING *value )
{
    struct music_properties *impl = impl_from_IMusicDisplayProperties( iface );
    TRACE( "iface %p, value %p\n", iface, value );
    return WindowsDuplicateString( impl->album_artist, value );
}

static HRESULT WINAPI music_properties_put_AlbumArtist( IMusicDisplayProperties *iface, HSTRING value )
{
    struct music_properties *impl = impl_from_IMusicDisplayProperties( iface );
    TRACE( "iface %p, value %s\n", iface, debugstr_hstring(value) );
    WindowsDeleteString( impl->album_artist );
    return WindowsDuplicateString( value, &impl->album_artist );
}

static HRESULT WINAPI music_properties_get_Artist( IMusicDisplayProperties *iface, HSTRING *value )
{
    struct music_properties *impl = impl_from_IMusicDisplayProperties( iface );
    TRACE( "iface %p, value %p\n", iface, value );
    return WindowsDuplicateString( impl->artist, value );
}

static HRESULT WINAPI music_properties_put_Artist( IMusicDisplayProperties *iface, HSTRING value )
{
    struct music_properties *impl = impl_from_IMusicDisplayProperties( iface );
    TRACE( "iface %p, value %p\n", iface, value );
    WindowsDeleteString( impl->artist );
    return WindowsDuplicateString( value, &impl->artist );
}

static const IMusicDisplayPropertiesVtbl music_properties_vtbl =
{
    music_properties_QueryInterface,
    music_properties_AddRef,
    music_properties_Release,
    /* IInspectable methods */
    music_properties_GetIids,
    music_properties_GetRuntimeClassName,
    music_properties_GetTrustLevel,
    /* IMusicDisplayProperties methods */
    music_properties_get_Title,
    music_properties_put_Title,
    music_properties_get_AlbumArtist,
    music_properties_put_AlbumArtist,
    music_properties_get_Artist,
    music_properties_put_Artist,
};

DEFINE_IINSPECTABLE( music_properties2, IMusicDisplayProperties2, struct music_properties, IMusicDisplayProperties_iface )

static HRESULT STDMETHODCALLTYPE music_properties2_get_AlbumTitle( IMusicDisplayProperties2 *iface, HSTRING *value )
{
    struct music_properties *impl = impl_from_IMusicDisplayProperties2( iface );
    TRACE( "iface %p, value %p\n", iface, value );
    return WindowsDuplicateString( impl->album_title, value );
}

static HRESULT STDMETHODCALLTYPE music_properties2_put_AlbumTitle( IMusicDisplayProperties2 *iface, HSTRING value )
{
    struct music_properties *impl = impl_from_IMusicDisplayProperties2( iface );
    TRACE( "iface %p, value %p\n", iface, value );
    WindowsDeleteString( impl->album_title );
    return WindowsDuplicateString( value, &impl->album_title );
}

static HRESULT STDMETHODCALLTYPE music_properties2_get_TrackNumber( IMusicDisplayProperties2 *iface, UINT32 *value )
{
    FIXME( "iface %p, value %p stub\n", iface, value );
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE music_properties2_put_TrackNumber( IMusicDisplayProperties2 *iface, UINT32 value )
{
    FIXME( "iface %p, value %u stub\n", iface, value );
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE music_properties2_get_Genres( IMusicDisplayProperties2 *iface, IVector_HSTRING **value )
{
    FIXME( "iface %p, value %p stub\n", iface, value );
    return E_NOTIMPL;
}

static const struct IMusicDisplayProperties2Vtbl music_properties2_vtbl =
{
    music_properties2_QueryInterface,
    music_properties2_AddRef,
    music_properties2_Release,
    /* IInspectable methods */
    music_properties2_GetIids,
    music_properties2_GetRuntimeClassName,
    music_properties2_GetTrustLevel,
    /* IMusicDisplayProperties2 methods */
    music_properties2_get_AlbumTitle,
    music_properties2_put_AlbumTitle,
    music_properties2_get_TrackNumber,
    music_properties2_put_TrackNumber,
    music_properties2_get_Genres,
};

struct video_properties
{
    IVideoDisplayProperties IVideoDisplayProperties_iface;
    LONG ref;

    HSTRING subtitle;
    HSTRING title;
};

static inline struct video_properties *impl_from_IVideoDisplayProperties( IVideoDisplayProperties *iface )
{
    return CONTAINING_RECORD( iface, struct video_properties, IVideoDisplayProperties_iface );
}

static HRESULT WINAPI video_properties_QueryInterface( IVideoDisplayProperties *iface, REFIID iid, void **out )
{
    struct video_properties *impl = impl_from_IVideoDisplayProperties( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IVideoDisplayProperties ))
    {
        *out = &impl->IVideoDisplayProperties_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI video_properties_AddRef( IVideoDisplayProperties *iface )
{
    struct video_properties *impl = impl_from_IVideoDisplayProperties( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "iface %p increasing refcount to %lu.\n", iface, ref );
    return ref;
}

static ULONG WINAPI video_properties_Release( IVideoDisplayProperties *iface )
{
    struct video_properties *impl = impl_from_IVideoDisplayProperties( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    TRACE( "iface %p decreasing refcount to %lu.\n", iface, ref );

    if (!ref)
    {
        WindowsDeleteString( impl->subtitle );
        WindowsDeleteString( impl->title );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI video_properties_GetIids( IVideoDisplayProperties *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI video_properties_GetRuntimeClassName( IVideoDisplayProperties *iface, HSTRING *class_name )
{
    FIXME( "iface %p, class_name %p stub!\n", iface, class_name );
    return E_NOTIMPL;
}

static HRESULT WINAPI video_properties_GetTrustLevel( IVideoDisplayProperties *iface, TrustLevel *trust_level )
{
    FIXME( "iface %p, trust_level %p stub!\n", iface, trust_level );
    return E_NOTIMPL;
}

static HRESULT WINAPI video_properties_get_Title( IVideoDisplayProperties *iface, HSTRING *value )
{
    struct video_properties *impl = impl_from_IVideoDisplayProperties( iface );
    TRACE( "iface %p, value %p\n", iface, value );
    return WindowsDuplicateString( impl->title, value );
}

static HRESULT WINAPI video_properties_put_Title( IVideoDisplayProperties *iface, HSTRING value )
{
    struct video_properties *impl = impl_from_IVideoDisplayProperties( iface );
    TRACE( "iface %p, value %p\n", iface, value );
    WindowsDeleteString( impl->title );
    return WindowsDuplicateString( value, &impl->title );
}

static HRESULT WINAPI video_properties_get_Subtitle( IVideoDisplayProperties *iface, HSTRING *value )
{
    struct video_properties *impl = impl_from_IVideoDisplayProperties( iface );
    TRACE( "iface %p, value %p\n", iface, value );
    return WindowsDuplicateString( impl->subtitle, value );
}

static HRESULT WINAPI video_properties_put_Subtitle( IVideoDisplayProperties *iface, HSTRING value )
{
    struct video_properties *impl = impl_from_IVideoDisplayProperties( iface );
    TRACE( "iface %p, value %p\n", iface, value );
    WindowsDeleteString( impl->subtitle );
    return WindowsDuplicateString( value, &impl->subtitle );
}

static const struct IVideoDisplayPropertiesVtbl video_properties_vtbl =
{
    video_properties_QueryInterface,
    video_properties_AddRef,
    video_properties_Release,
    /* IInspectable methods */
    video_properties_GetIids,
    video_properties_GetRuntimeClassName,
    video_properties_GetTrustLevel,
    /* IVideoDisplayProperties methods */
    video_properties_get_Title,
    video_properties_put_Title,
    video_properties_get_Subtitle,
    video_properties_put_Subtitle,
};

struct display_updater
{
    ISystemMediaTransportControlsDisplayUpdater ISystemMediaTransportControlsDisplayUpdater_iface;
    LONG ref;

    MediaPlaybackType playback_type;
    struct music_properties *music;
    struct video_properties *video;
};

static inline struct display_updater *impl_from_ISystemMediaTransportControlsDisplayUpdater( ISystemMediaTransportControlsDisplayUpdater *iface )
{
    return CONTAINING_RECORD( iface, struct display_updater, ISystemMediaTransportControlsDisplayUpdater_iface );
}

static HRESULT WINAPI display_updater_QueryInterface( ISystemMediaTransportControlsDisplayUpdater *iface, REFIID iid, void **out )
{
    struct display_updater *impl = impl_from_ISystemMediaTransportControlsDisplayUpdater( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_ISystemMediaTransportControlsDisplayUpdater ))
    {
        *out = &impl->ISystemMediaTransportControlsDisplayUpdater_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI display_updater_AddRef( ISystemMediaTransportControlsDisplayUpdater *iface )
{
    struct display_updater *impl = impl_from_ISystemMediaTransportControlsDisplayUpdater( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "iface %p increasing refcount to %lu.\n", iface, ref );
    return ref;
}

static ULONG WINAPI display_updater_Release( ISystemMediaTransportControlsDisplayUpdater *iface )
{
    struct display_updater *impl = impl_from_ISystemMediaTransportControlsDisplayUpdater( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    TRACE( "iface %p decreasing refcount to %lu.\n", iface, ref );

    if (!ref)
    {
        if (impl->music) IMusicDisplayProperties_Release( &impl->music->IMusicDisplayProperties_iface );
        if (impl->video) IVideoDisplayProperties_Release( &impl->video->IVideoDisplayProperties_iface );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI display_updater_GetIids( ISystemMediaTransportControlsDisplayUpdater *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI display_updater_GetRuntimeClassName( ISystemMediaTransportControlsDisplayUpdater *iface, HSTRING *class_name )
{
    FIXME( "iface %p, class_name %p stub!\n", iface, class_name );
    return E_NOTIMPL;
}

static HRESULT WINAPI display_updater_GetTrustLevel( ISystemMediaTransportControlsDisplayUpdater *iface, TrustLevel *trust_level )
{
    FIXME( "iface %p, trust_level %p stub!\n", iface, trust_level );
    return E_NOTIMPL;
}

static HRESULT WINAPI display_updater_get_Type( ISystemMediaTransportControlsDisplayUpdater *iface, MediaPlaybackType *value )
{
    struct display_updater *impl = impl_from_ISystemMediaTransportControlsDisplayUpdater( iface );

    TRACE( "iface %p, value %p\n", iface, value );

    *value = impl->playback_type;
    return S_OK;
}

static HRESULT WINAPI display_updater_put_Type( ISystemMediaTransportControlsDisplayUpdater *iface, MediaPlaybackType value )
{
    struct display_updater *impl = impl_from_ISystemMediaTransportControlsDisplayUpdater( iface );

    TRACE( "iface %p, value %d\n", iface, value );

    if (value < MediaPlaybackType_Unknown || value > MediaPlaybackType_Image) return E_INVALIDARG;
    impl->playback_type = value;
    return S_OK;
}

static HRESULT WINAPI display_updater_get_AppMediaId( ISystemMediaTransportControlsDisplayUpdater *iface, HSTRING *value )
{
    FIXME( "iface %p, value %p stub!\n", iface, value );
    return E_NOTIMPL;
}

static HRESULT WINAPI display_updater_put_AppMediaId( ISystemMediaTransportControlsDisplayUpdater *iface, HSTRING value )
{
    FIXME( "iface %p, value %s stub!\n", iface, debugstr_hstring( value ) );
    return E_NOTIMPL;
}

static HRESULT WINAPI display_updater_get_Thumbnail( ISystemMediaTransportControlsDisplayUpdater *iface, IRandomAccessStreamReference **value )
{
    FIXME( "iface %p, value %p stub!\n", iface, value );

    *value = NULL;
    return S_OK;
}

static HRESULT WINAPI display_updater_put_Thumbnail( ISystemMediaTransportControlsDisplayUpdater *iface, IRandomAccessStreamReference *value )
{
    FIXME( "iface %p, value %p stub!\n", iface, value );
    return S_OK;
}

static HRESULT WINAPI display_updater_get_MusicProperties( ISystemMediaTransportControlsDisplayUpdater *iface, IMusicDisplayProperties **value )
{
    struct display_updater *impl = impl_from_ISystemMediaTransportControlsDisplayUpdater( iface );

    TRACE( "iface %p, value %p\n", iface, value );

    if (!impl->music)
    {
        struct music_properties *props;

        if (!(props = calloc( 1, sizeof(*props) ))) return E_OUTOFMEMORY;
        props->IMusicDisplayProperties_iface.lpVtbl = &music_properties_vtbl;
        props->IMusicDisplayProperties2_iface.lpVtbl = &music_properties2_vtbl;
        props->ref = 1;
        impl->music = props;
    }

    *value = &impl->music->IMusicDisplayProperties_iface;
    IInspectable_AddRef( (IInspectable *)*value );
    return S_OK;
}

static HRESULT WINAPI display_updater_get_VideoProperties( ISystemMediaTransportControlsDisplayUpdater *iface, __x_ABI_CWindows_CMedia_CIVideoDisplayProperties **value )
{
    struct display_updater *impl = impl_from_ISystemMediaTransportControlsDisplayUpdater( iface );

    TRACE( "iface %p, value %p\n", iface, value );

    if (!impl->video)
    {
        struct video_properties *props;

        if (!(props = calloc( 1, sizeof(*props) ))) return E_OUTOFMEMORY;
        props->IVideoDisplayProperties_iface.lpVtbl = &video_properties_vtbl;
        props->ref = 1;
        impl->video = props;
    }

    *value = &impl->video->IVideoDisplayProperties_iface;
    IInspectable_AddRef( (IInspectable *)*value );
    return S_OK;
}

static HRESULT WINAPI display_updater_get_ImageProperties( ISystemMediaTransportControlsDisplayUpdater *iface, __x_ABI_CWindows_CMedia_CIImageDisplayProperties **value )
{
    FIXME( "iface %p, value %p stub!\n", iface, value );
    return E_NOTIMPL;
}

static HRESULT WINAPI display_updater_CopyFromFileAsync( ISystemMediaTransportControlsDisplayUpdater *iface, MediaPlaybackType type, IStorageFile *source,
    IAsyncOperation_boolean **operation )
{
    FIXME( "iface %p, type %d, source %p, operation %p stub!\n", iface, type, source, operation );
    return E_NOTIMPL;
}

static void push_metadata( struct display_updater *impl )
{
    struct mpris_metadata_params params = { 0 };
    const WCHAR *title = NULL;

    if (impl->music)
    {
        title = WindowsGetStringRawBuffer( impl->music->title, NULL );
        params.artist = WindowsGetStringRawBuffer( impl->music->artist, NULL );
        params.album = WindowsGetStringRawBuffer( impl->music->album_title, NULL );
        params.album_artist = WindowsGetStringRawBuffer( impl->music->album_artist, NULL );
    }
    if (impl->video && (!title || !*title))
        title = WindowsGetStringRawBuffer( impl->video->title, NULL );
    params.title = title;

    MEDIACONTROL_CALL( mpris_set_metadata, &params );
}

static HRESULT WINAPI display_updater_ClearAll( ISystemMediaTransportControlsDisplayUpdater *iface )
{
    struct display_updater *impl = impl_from_ISystemMediaTransportControlsDisplayUpdater( iface );

    TRACE( "iface %p stub!\n", iface );

    if (impl->music)
    {
        WindowsDeleteString( impl->music->title );
        impl->music->title = 0;
        WindowsDeleteString( impl->music->artist );
        impl->music->artist = 0;
        WindowsDeleteString( impl->music->album_title );
        impl->music->album_title = 0;
        WindowsDeleteString( impl->music->album_artist );
        impl->music->album_artist = 0;
    }
    if (impl->video)
    {
        WindowsDeleteString( impl->video->title );
        impl->video->title = 0;
        WindowsDeleteString( impl->video->subtitle );
        impl->video->subtitle = 0;
    }
    impl->playback_type = MediaPlaybackType_Unknown;
    push_metadata( impl );
    return S_OK;
}

static HRESULT WINAPI display_updater_Update( ISystemMediaTransportControlsDisplayUpdater *iface )
{
    struct display_updater *impl = impl_from_ISystemMediaTransportControlsDisplayUpdater( iface );

    TRACE( "iface %p\n", iface );

    push_metadata( impl );
    return S_OK;
}

static const struct ISystemMediaTransportControlsDisplayUpdaterVtbl display_updater_vtbl =
{
    display_updater_QueryInterface,
    display_updater_AddRef,
    display_updater_Release,
    /* IInspectable methods */
    display_updater_GetIids,
    display_updater_GetRuntimeClassName,
    display_updater_GetTrustLevel,
    /* ISystemMediaTransportControlsDisplayUpdater methods */
    display_updater_get_Type,
    display_updater_put_Type,
    display_updater_get_AppMediaId,
    display_updater_put_AppMediaId,
    display_updater_get_Thumbnail,
    display_updater_put_Thumbnail,
    display_updater_get_MusicProperties,
    display_updater_get_VideoProperties,
    display_updater_get_ImageProperties,
    display_updater_CopyFromFileAsync,
    display_updater_ClearAll,
    display_updater_Update,
};

struct button_pressed_args
{
    ISystemMediaTransportControlsButtonPressedEventArgs ISystemMediaTransportControlsButtonPressedEventArgs_iface;
    LONG ref;

    SystemMediaTransportControlsButton button;
};

static inline struct button_pressed_args *impl_from_ISystemMediaTransportControlsButtonPressedEventArgs(
        ISystemMediaTransportControlsButtonPressedEventArgs *iface )
{
    return CONTAINING_RECORD( iface, struct button_pressed_args, ISystemMediaTransportControlsButtonPressedEventArgs_iface );
}

static HRESULT WINAPI button_pressed_args_QueryInterface( ISystemMediaTransportControlsButtonPressedEventArgs *iface,
        REFIID iid, void **out )
{
    struct button_pressed_args *impl = impl_from_ISystemMediaTransportControlsButtonPressedEventArgs( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_ISystemMediaTransportControlsButtonPressedEventArgs ))
    {
        *out = &impl->ISystemMediaTransportControlsButtonPressedEventArgs_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI button_pressed_args_AddRef( ISystemMediaTransportControlsButtonPressedEventArgs *iface )
{
    struct button_pressed_args *impl = impl_from_ISystemMediaTransportControlsButtonPressedEventArgs( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "iface %p increasing refcount to %lu.\n", iface, ref );
    return ref;
}

static ULONG WINAPI button_pressed_args_Release( ISystemMediaTransportControlsButtonPressedEventArgs *iface )
{
    struct button_pressed_args *impl = impl_from_ISystemMediaTransportControlsButtonPressedEventArgs( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    TRACE( "iface %p decreasing refcount to %lu.\n", iface, ref );

    if (!ref) free( impl );
    return ref;
}

static HRESULT WINAPI button_pressed_args_GetIids( ISystemMediaTransportControlsButtonPressedEventArgs *iface,
        ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI button_pressed_args_GetRuntimeClassName( ISystemMediaTransportControlsButtonPressedEventArgs *iface,
        HSTRING *class_name )
{
    FIXME( "iface %p, class_name %p stub!\n", iface, class_name );
    return E_NOTIMPL;
}

static HRESULT WINAPI button_pressed_args_GetTrustLevel( ISystemMediaTransportControlsButtonPressedEventArgs *iface,
        TrustLevel *trust_level )
{
    FIXME( "iface %p, trust_level %p stub!\n", iface, trust_level );
    return E_NOTIMPL;
}

static HRESULT WINAPI button_pressed_args_get_Button( ISystemMediaTransportControlsButtonPressedEventArgs *iface,
        SystemMediaTransportControlsButton *value )
{
    struct button_pressed_args *impl = impl_from_ISystemMediaTransportControlsButtonPressedEventArgs( iface );

    TRACE( "iface %p, value %p\n", iface, value );

    *value = impl->button;
    return S_OK;
}

static const struct ISystemMediaTransportControlsButtonPressedEventArgsVtbl button_pressed_args_vtbl =
{
    button_pressed_args_QueryInterface,
    button_pressed_args_AddRef,
    button_pressed_args_Release,
    /* IInspectable methods */
    button_pressed_args_GetIids,
    button_pressed_args_GetRuntimeClassName,
    button_pressed_args_GetTrustLevel,
    /* ISystemMediaTransportControlsButtonPressedEventArgs methods */
    button_pressed_args_get_Button,
};

struct media_control
{
    ISystemMediaTransportControls ISystemMediaTransportControls_iface;
    LONG ref;

    HWND window;
    struct display_updater *updater;
    MediaPlaybackStatus media_playback_status;
    boolean is_play_enabled;
    boolean is_stop_enabled;
    boolean is_pause_enabled;
    boolean is_previous_enabled;
    boolean is_next_enabled;
    boolean is_enabled;
};

static inline struct media_control *impl_from_ISystemMediaTransportControls( ISystemMediaTransportControls *iface )
{
    return CONTAINING_RECORD( iface, struct media_control, ISystemMediaTransportControls_iface );
}

static void push_caps( struct media_control *impl )
{
    struct mpris_caps_params params;

    params.can_play = impl->is_play_enabled;
    params.can_pause = impl->is_pause_enabled;
    params.can_next = impl->is_next_enabled;
    params.can_prev = impl->is_previous_enabled;
    params.can_control = impl->is_enabled;
    MEDIACONTROL_CALL( mpris_set_caps, &params );
}

static HRESULT WINAPI media_control_QueryInterface( ISystemMediaTransportControls *iface, REFIID iid, void **out )
{
    struct media_control *impl = impl_from_ISystemMediaTransportControls( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_ISystemMediaTransportControls ))
    {
        *out = &impl->ISystemMediaTransportControls_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI media_control_AddRef( ISystemMediaTransportControls *iface )
{
    struct media_control *impl = impl_from_ISystemMediaTransportControls( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "iface %p increasing refcount to %lu.\n", iface, ref );
    return ref;
}

static ULONG WINAPI media_control_Release( ISystemMediaTransportControls *iface )
{
    struct media_control *impl = impl_from_ISystemMediaTransportControls( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    TRACE( "iface %p decreasing refcount to %lu.\n", iface, ref );

    if (!ref)
    {
        if (impl->updater)
            ISystemMediaTransportControlsDisplayUpdater_Release( &impl->updater->ISystemMediaTransportControlsDisplayUpdater_iface );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI media_control_GetIids( ISystemMediaTransportControls *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI media_control_GetRuntimeClassName( ISystemMediaTransportControls *iface, HSTRING *class_name )
{
    FIXME( "iface %p, class_name %p stub!\n", iface, class_name );
    return E_NOTIMPL;
}

static HRESULT WINAPI media_control_GetTrustLevel( ISystemMediaTransportControls *iface, TrustLevel *trust_level )
{
    FIXME( "iface %p, trust_level %p stub!\n", iface, trust_level );
    return E_NOTIMPL;
}

static HRESULT WINAPI media_control_get_PlaybackStatus( ISystemMediaTransportControls *iface, MediaPlaybackStatus *value )
{
    struct media_control *impl = impl_from_ISystemMediaTransportControls( iface );

    TRACE( "iface %p, value %p\n", iface, value );

    *value = impl->media_playback_status;
    return S_OK;
}

static HRESULT WINAPI media_control_put_PlaybackStatus( ISystemMediaTransportControls *iface, MediaPlaybackStatus value )
{
    struct media_control *impl = impl_from_ISystemMediaTransportControls( iface );

    TRACE( "iface %p, value %d\n", iface, value );

    if (value < MediaPlaybackStatus_Closed || value > MediaPlaybackStatus_Paused) return E_INVALIDARG;
    impl->media_playback_status = value;
    {
        struct mpris_status_params params = { value };
        MEDIACONTROL_CALL( mpris_set_status, &params );
    }
    return S_OK;
}

static HRESULT media_control_get_updater( struct media_control *impl, ISystemMediaTransportControlsDisplayUpdater **value )
{
    if (!impl->updater)
    {
        struct display_updater *updater;

        if (!(updater = calloc( 1, sizeof(*updater) ))) return E_OUTOFMEMORY;
        updater->ISystemMediaTransportControlsDisplayUpdater_iface.lpVtbl = &display_updater_vtbl;
        updater->ref = 1;
        impl->updater = updater;
    }

    *value = &impl->updater->ISystemMediaTransportControlsDisplayUpdater_iface;
    IInspectable_AddRef( (IInspectable *)*value );
    return S_OK;
}

static HRESULT WINAPI media_control_get_DisplayUpdater( ISystemMediaTransportControls *iface, ISystemMediaTransportControlsDisplayUpdater **value )
{
    struct media_control *impl = impl_from_ISystemMediaTransportControls( iface );

    TRACE( "iface %p, value %p\n", iface, value );

    return media_control_get_updater( impl, value );
}

static HRESULT WINAPI media_control_get_SoundLevel( ISystemMediaTransportControls *iface, SoundLevel *value )
{
    FIXME( "iface %p, value %p stub!\n", iface, value );
    return E_NOTIMPL;
}

static HRESULT WINAPI media_control_get_IsEnabled( ISystemMediaTransportControls *iface, boolean *value )
{
    struct media_control *impl = impl_from_ISystemMediaTransportControls( iface );

    TRACE( "iface %p, value %p\n", iface, value );

    *value = impl->is_enabled;
    return S_OK;
}

static HRESULT WINAPI media_control_put_IsEnabled( ISystemMediaTransportControls *iface, boolean value )
{
    struct media_control *impl = impl_from_ISystemMediaTransportControls( iface );

    TRACE( "iface %p, value %d\n", iface, value );

    impl->is_enabled = value;
    if (value) MEDIACONTROL_CALL( mpris_claim, NULL );
    else MEDIACONTROL_CALL( mpris_release, NULL );
    push_caps( impl );
    return S_OK;
}

static HRESULT WINAPI media_control_get_IsPlayEnabled( ISystemMediaTransportControls *iface, boolean *value )
{
    struct media_control *impl = impl_from_ISystemMediaTransportControls( iface );

    TRACE( "iface %p, value %p\n", iface, value );

    *value = impl->is_play_enabled;
    return S_OK;
}

static HRESULT WINAPI media_control_put_IsPlayEnabled( ISystemMediaTransportControls *iface, boolean value )
{
    struct media_control *impl = impl_from_ISystemMediaTransportControls( iface );

    TRACE( "iface %p, value %d\n", iface, value );

    impl->is_play_enabled = value;
    push_caps( impl );
    return S_OK;
}

static HRESULT WINAPI media_control_get_IsStopEnabled( ISystemMediaTransportControls *iface, boolean *value )
{
    struct media_control *impl = impl_from_ISystemMediaTransportControls( iface );

    TRACE( "iface %p, value %p\n", iface, value );

    *value = impl->is_stop_enabled;
    return S_OK;
}

static HRESULT WINAPI media_control_put_IsStopEnabled( ISystemMediaTransportControls *iface, boolean value )
{
    struct media_control *impl = impl_from_ISystemMediaTransportControls( iface );

    TRACE( "iface %p, value %d\n", iface, value );

    impl->is_stop_enabled = value;
    push_caps( impl );
    return S_OK;
}

static HRESULT WINAPI media_control_get_IsPauseEnabled( ISystemMediaTransportControls *iface, boolean *value )
{
    struct media_control *impl = impl_from_ISystemMediaTransportControls( iface );

    TRACE( "iface %p, value %p\n", iface, value );

    *value = impl->is_pause_enabled;
    return S_OK;
}

static HRESULT WINAPI media_control_put_IsPauseEnabled( ISystemMediaTransportControls *iface, boolean value )
{
    struct media_control *impl = impl_from_ISystemMediaTransportControls( iface );

    TRACE( "iface %p, value %d\n", iface, value );

    impl->is_pause_enabled = value;
    push_caps( impl );
    return S_OK;
}

static HRESULT WINAPI media_control_get_IsRecordEnabled( ISystemMediaTransportControls *iface, boolean *value )
{
    FIXME( "iface %p, value %p stub!\n", iface, value );
    return E_NOTIMPL;
}

static HRESULT WINAPI media_control_put_IsRecordEnabled( ISystemMediaTransportControls *iface, boolean value )
{
    FIXME( "iface %p, value %d stub!\n", iface, value );
    return E_NOTIMPL;
}

static HRESULT WINAPI media_control_get_IsFastForwardEnabled( ISystemMediaTransportControls *iface, boolean *value )
{
    FIXME( "iface %p, value %p stub!\n", iface, value );
    return E_NOTIMPL;
}

static HRESULT WINAPI media_control_put_IsFastForwardEnabled( ISystemMediaTransportControls *iface, boolean value )
{
    FIXME( "iface %p, value %d stub!\n", iface, value );
    return E_NOTIMPL;
}

static HRESULT WINAPI media_control_get_IsRewindEnabled( ISystemMediaTransportControls *iface, boolean *value )
{
    FIXME( "iface %p, value %p stub!\n", iface, value );
    return E_NOTIMPL;
}

static HRESULT WINAPI media_control_put_IsRewindEnabled( ISystemMediaTransportControls *iface, boolean value )
{
    FIXME( "iface %p, value %d stub!\n", iface, value );
    return E_NOTIMPL;
}

static HRESULT WINAPI media_control_get_IsPreviousEnabled( ISystemMediaTransportControls *iface, boolean *value )
{
    struct media_control *impl = impl_from_ISystemMediaTransportControls( iface );

    TRACE( "iface %p, value %p\n", iface, value );

    *value = impl->is_previous_enabled;
    return S_OK;
}

static HRESULT WINAPI media_control_put_IsPreviousEnabled( ISystemMediaTransportControls *iface, boolean value )
{
    struct media_control *impl = impl_from_ISystemMediaTransportControls( iface );

    TRACE( "iface %p, value %d\n", iface, value );

    impl->is_previous_enabled = value;
    push_caps( impl );
    return S_OK;
}

static HRESULT WINAPI media_control_get_IsNextEnabled( ISystemMediaTransportControls *iface, boolean *value )
{
    struct media_control *impl = impl_from_ISystemMediaTransportControls( iface );

    TRACE( "iface %p, value %p\n", iface, value );

    *value = impl->is_next_enabled;
    return S_OK;
}

static HRESULT WINAPI media_control_put_IsNextEnabled( ISystemMediaTransportControls *iface, boolean value )
{
    struct media_control *impl = impl_from_ISystemMediaTransportControls( iface );

    TRACE( "iface %p, value %d\n", iface, value );

    impl->is_next_enabled = value;
    push_caps( impl );
    return S_OK;
}

static HRESULT WINAPI media_control_get_IsChannelUpEnabled( ISystemMediaTransportControls *iface, boolean *value )
{
    FIXME( "iface %p, value %p stub!\n", iface, value );
    return E_NOTIMPL;
}

static HRESULT WINAPI media_control_put_IsChannelUpEnabled( ISystemMediaTransportControls *iface, boolean value )
{
    FIXME( "iface %p, value %d stub!\n", iface, value );
    return E_NOTIMPL;
}

static HRESULT WINAPI media_control_get_IsChannelDownEnabled( ISystemMediaTransportControls *iface, boolean *value )
{
    FIXME( "iface %p, value %p stub!\n", iface, value );
    return E_NOTIMPL;
}

static HRESULT WINAPI media_control_put_IsChannelDownEnabled( ISystemMediaTransportControls *iface, boolean value )
{
    FIXME( "iface %p, value %d stub!\n", iface, value );
    return E_NOTIMPL;
}

typedef ITypedEventHandler_SystemMediaTransportControls_SystemMediaTransportControlsButtonPressedEventArgs button_pressed_handler;

struct button_registration
{
    EventRegistrationToken token;
    button_pressed_handler *handler;
    struct media_control *control;
};

static struct button_registration *button_registrations;
static ULONG button_registration_count;
static ULONG button_registration_size;
static CRITICAL_SECTION button_cs;
static LONGLONG button_token_counter = 1;

static void dispatch_button( SystemMediaTransportControlsButton button )
{
    struct button_registration *snapshot = NULL;
    struct button_pressed_args *args;
    ULONG count = 0, i;

    EnterCriticalSection( &button_cs );
    if (button_registration_count && (snapshot = calloc( button_registration_count, sizeof(*snapshot) )))
    {
        for (i = 0; i < button_registration_count; i++)
        {
            snapshot[i] = button_registrations[i];
            IInspectable_AddRef( (IInspectable *)&snapshot[i].control->ISystemMediaTransportControls_iface );
            ITypedEventHandler_SystemMediaTransportControls_SystemMediaTransportControlsButtonPressedEventArgs_AddRef( snapshot[i].handler );
        }
        count = button_registration_count;
    }
    LeaveCriticalSection( &button_cs );

    if (!count) return;

    if ((args = calloc( 1, sizeof(*args) )))
    {
        ISystemMediaTransportControlsButtonPressedEventArgs *event_args;

        args->ISystemMediaTransportControlsButtonPressedEventArgs_iface.lpVtbl = &button_pressed_args_vtbl;
        args->ref = 1;
        args->button = button;
        event_args = &args->ISystemMediaTransportControlsButtonPressedEventArgs_iface;

        for (i = 0; i < count; i++)
        {
            ISystemMediaTransportControls *sender = &snapshot[i].control->ISystemMediaTransportControls_iface;
            ITypedEventHandler_SystemMediaTransportControls_SystemMediaTransportControlsButtonPressedEventArgs_Invoke(
                    snapshot[i].handler, sender, event_args );
        }
        ISystemMediaTransportControlsButtonPressedEventArgs_Release( event_args );
    }

    for (i = 0; i < count; i++)
    {
        IInspectable_Release( (IInspectable *)&snapshot[i].control->ISystemMediaTransportControls_iface );
        ITypedEventHandler_SystemMediaTransportControls_SystemMediaTransportControlsButtonPressedEventArgs_Release( snapshot[i].handler );
    }
    free( snapshot );
}

static HRESULT WINAPI media_control_add_ButtonPressed( ISystemMediaTransportControls *iface,
    button_pressed_handler *handler, EventRegistrationToken *token )
{
    struct media_control *impl = impl_from_ISystemMediaTransportControls( iface );
    struct button_registration *regs;
    ULONG size;

    TRACE( "iface %p, handler %p, token %p.\n", iface, handler, token );

    if (!handler || !token) return E_POINTER;

    EnterCriticalSection( &button_cs );
    size = button_registration_size;
    if (button_registration_count == size && size < 0x1000)
    {
        size = size ? size * 2 : 8;
        if ((regs = realloc( button_registrations, size * sizeof(*regs) )))
        {
            button_registrations = regs;
            button_registration_size = size;
        }
    }
    if (button_registration_count < size)
    {
        regs = &button_registrations[button_registration_count++];
        regs->token.value = InterlockedIncrement64( &button_token_counter );
        regs->handler = handler;
        regs->control = impl;
        IInspectable_AddRef( (IInspectable *)&impl->ISystemMediaTransportControls_iface );
        ITypedEventHandler_SystemMediaTransportControls_SystemMediaTransportControlsButtonPressedEventArgs_AddRef( handler );
        *token = regs->token;
        LeaveCriticalSection( &button_cs );
        return S_OK;
    }
    LeaveCriticalSection( &button_cs );

    *token = dummy_token;
    return E_OUTOFMEMORY;
}

static HRESULT WINAPI media_control_remove_ButtonPressed( ISystemMediaTransportControls *iface, EventRegistrationToken token )
{
    ULONG i;

    TRACE( "iface %p, token %#I64x.\n", iface, token.value );

    EnterCriticalSection( &button_cs );
    for (i = 0; i < button_registration_count; i++)
    {
        if (button_registrations[i].token.value != token.value) continue;

        ITypedEventHandler_SystemMediaTransportControls_SystemMediaTransportControlsButtonPressedEventArgs_Release( button_registrations[i].handler );
        IInspectable_Release( (IInspectable *)&button_registrations[i].control->ISystemMediaTransportControls_iface );
        button_registrations[i] = button_registrations[--button_registration_count];
        break;
    }
    LeaveCriticalSection( &button_cs );

    return S_OK;
}

static HRESULT WINAPI media_control_add_PropertyChanged( ISystemMediaTransportControls *iface,
    ITypedEventHandler_SystemMediaTransportControls_SystemMediaTransportControlsPropertyChangedEventArgs *handler, EventRegistrationToken *token )
{
    FIXME( "iface %p, handler %p, token %p stub!\n", iface, handler, token );
    return E_NOTIMPL;
}

static HRESULT WINAPI media_control_remove_PropertyChanged( ISystemMediaTransportControls *iface, EventRegistrationToken token )
{
    FIXME( "iface %p, token %#I64x stub!\n", iface, token.value );
    return E_NOTIMPL;
}

static const struct ISystemMediaTransportControlsVtbl media_control_vtbl =
{
    media_control_QueryInterface,
    media_control_AddRef,
    media_control_Release,
    /* IInspectable methods */
    media_control_GetIids,
    media_control_GetRuntimeClassName,
    media_control_GetTrustLevel,
    /* ISystemMediaTransportControls methods */
    media_control_get_PlaybackStatus,
    media_control_put_PlaybackStatus,
    media_control_get_DisplayUpdater,
    media_control_get_SoundLevel,
    media_control_get_IsEnabled,
    media_control_put_IsEnabled,
    media_control_get_IsPlayEnabled,
    media_control_put_IsPlayEnabled,
    media_control_get_IsStopEnabled,
    media_control_put_IsStopEnabled,
    media_control_get_IsPauseEnabled,
    media_control_put_IsPauseEnabled,
    media_control_get_IsRecordEnabled,
    media_control_put_IsRecordEnabled,
    media_control_get_IsFastForwardEnabled,
    media_control_put_IsFastForwardEnabled,
    media_control_get_IsRewindEnabled,
    media_control_put_IsRewindEnabled,
    media_control_get_IsPreviousEnabled,
    media_control_put_IsPreviousEnabled,
    media_control_get_IsNextEnabled,
    media_control_put_IsNextEnabled,
    media_control_get_IsChannelUpEnabled,
    media_control_put_IsChannelUpEnabled,
    media_control_get_IsChannelDownEnabled,
    media_control_put_IsChannelDownEnabled,
    media_control_add_ButtonPressed,
    media_control_remove_ButtonPressed,
    media_control_add_PropertyChanged,
    media_control_remove_PropertyChanged,
};

static void ensure_mpris_thread( void );

static void push_identity( void )
{
    static LONG claimed;
    static WCHAR path[MAX_PATH];
    struct mpris_claim_params params = { 0 };
    WCHAR *p;

    if (InterlockedExchange( &claimed, 1 )) return;

    params.identity = L"Wine Application";
    if (GetModuleFileNameW( 0, path, MAX_PATH ))
    {
        if ((p = wcsrchr( path, L'\\' ))) params.identity = p + 1;
        else if (path[0]) params.identity = path;
    }
    MEDIACONTROL_CALL( mpris_claim, &params );
    ensure_mpris_thread();
}

static struct media_control *create_media_control( HWND window )
{
    struct media_control *impl;

    if (!(impl = calloc( 1, sizeof(*impl) ))) return NULL;

    impl->ISystemMediaTransportControls_iface.lpVtbl = &media_control_vtbl;
    impl->ref = 1;
    impl->window = window;
    impl->media_playback_status = MediaPlaybackStatus_Closed;

    push_identity();
    return impl;
}

static struct media_control *get_current_view_control( void )
{
    static void *control; /* leaked on purpose, mirrors the WinRT per-view singleton */
    struct media_control *created;

    if (!(created = InterlockedCompareExchangePointer( &control, NULL, NULL )))
    {
        if (!(created = create_media_control( GetActiveWindow() ))) return NULL;
        if (InterlockedCompareExchangePointer( &control, created, NULL )) free( created );
        created = control;
    }
    IInspectable_AddRef( (IInspectable *)&created->ISystemMediaTransportControls_iface );
    return created;
}

static HRESULT media_control_activate( IInspectable **instance )
{
    struct media_control *control;

    if (!(control = get_current_view_control())) return E_OUTOFMEMORY;
    *instance = (IInspectable *)&control->ISystemMediaTransportControls_iface;
    return S_OK;
}

DEFINE_IINSPECTABLE( media_control_statics, ISystemMediaTransportControlsInterop, struct media_control_statics, IActivationFactory_iface )

static HRESULT WINAPI media_control_statics_GetForWindow( ISystemMediaTransportControlsInterop *iface, HWND window, REFIID riid, void **control )
{
    struct media_control *impl;
    HRESULT hr;

    TRACE( "iface %p, window %p, riid %s, control %p\n", iface, window, debugstr_guid( riid ), control );

    if (!window) return E_POINTER;
    if (!(impl = create_media_control( window ))) return E_OUTOFMEMORY;

    TRACE( "created ISystemMediaTransportControls %p.\n", impl );

    impl->ref = 2;
    hr = ISystemMediaTransportControls_QueryInterface( &impl->ISystemMediaTransportControls_iface, riid, control );
    ISystemMediaTransportControls_Release( &impl->ISystemMediaTransportControls_iface );
    return hr;
}

static const struct ISystemMediaTransportControlsInteropVtbl media_control_statics_vtbl =
{
    media_control_statics_QueryInterface,
    media_control_statics_AddRef,
    media_control_statics_Release,
    /* IInspectable methods */
    media_control_statics_GetIids,
    media_control_statics_GetRuntimeClassName,
    media_control_statics_GetTrustLevel,
    /* ISystemMediaTransportControlsInterop methods */
    media_control_statics_GetForWindow,
};

DEFINE_IINSPECTABLE( media_control_statics2, ISystemMediaTransportControlsStatics, struct media_control_statics, IActivationFactory_iface )

static HRESULT WINAPI media_control_statics2_GetForCurrentView( ISystemMediaTransportControlsStatics *iface,
        ISystemMediaTransportControls **controls )
{
    struct media_control *control;

    TRACE( "iface %p, controls %p\n", iface, controls );

    if (!(control = get_current_view_control())) return E_OUTOFMEMORY;
    *controls = &control->ISystemMediaTransportControls_iface;
    return S_OK;
}

static const struct ISystemMediaTransportControlsStaticsVtbl media_control_statics2_vtbl =
{
    media_control_statics2_QueryInterface,
    media_control_statics2_AddRef,
    media_control_statics2_Release,
    /* IInspectable methods */
    media_control_statics2_GetIids,
    media_control_statics2_GetRuntimeClassName,
    media_control_statics2_GetTrustLevel,
    /* ISystemMediaTransportControlsStatics methods */
    media_control_statics2_GetForCurrentView,
};

static struct media_control_statics media_control_statics =
{
    {&factory_vtbl},
    {&media_control_statics_vtbl},
    {&media_control_statics2_vtbl},
    1,
};

static IActivationFactory *media_control_factory = &media_control_statics.IActivationFactory_iface;

static HANDLE mpris_thread;
static volatile LONG mpris_thread_running;
static volatile LONG mpris_thread_started;

static DWORD WINAPI mpris_worker( LPVOID arg )
{
    struct mpris_event_params params = { 200, 0, FALSE };

    while (mpris_thread_running)
    {
        NTSTATUS status = MEDIACONTROL_CALL( mpris_poll, &params );

        if (status == STATUS_NO_SUCH_DEVICE)
        {
            Sleep( 500 );
            continue;
        }
        if (params.got_event) dispatch_button( (SystemMediaTransportControlsButton)params.button );
    }
    return 0;
}

static void ensure_mpris_thread( void )
{
    if (!InterlockedCompareExchange( &mpris_thread_started, 1, 0 ))
    {
        mpris_thread_running = 1;
        mpris_thread = CreateThread( NULL, 0, mpris_worker, NULL, 0, NULL );
    }
}

BOOL WINAPI DllMain( HINSTANCE inst, DWORD reason, LPVOID reserved )
{
    switch (reason)
    {
    case DLL_PROCESS_ATTACH:
        __wine_init_unix_call();
        InitializeCriticalSection( &button_cs );
        break;
    case DLL_PROCESS_DETACH:
        mpris_thread_running = 0;
        if (!reserved && mpris_thread)
        {
            WaitForSingleObject( mpris_thread, 2000 );
            CloseHandle( mpris_thread );
            mpris_thread = NULL;
        }
        MEDIACONTROL_CALL( mpris_release, NULL );
        break;
    }
    return TRUE;
}

HRESULT WINAPI DllGetClassObject( REFCLSID clsid, REFIID riid, void **out )
{
    FIXME( "clsid %s, riid %s, out %p stub!\n", debugstr_guid( clsid ), debugstr_guid( riid ), out );
    return CLASS_E_CLASSNOTAVAILABLE;
}

HRESULT WINAPI DllGetActivationFactory( HSTRING classid, IActivationFactory **factory )
{
    const WCHAR *buffer = WindowsGetStringRawBuffer( classid, NULL );

    TRACE( "class %s, factory %p.\n", debugstr_hstring( classid ), factory );

    *factory = NULL;

    if (!wcscmp( buffer, RuntimeClass_Windows_Media_SystemMediaTransportControls ))
        IActivationFactory_QueryInterface( media_control_factory, &IID_IActivationFactory, (void **)factory );

    if (*factory) return S_OK;
    return E_NOINTERFACE;
}
