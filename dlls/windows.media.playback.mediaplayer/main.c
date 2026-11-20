/* WinRT Windows.Media.Playback.MediaPlayer Implementation
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

#include "initguid.h"
#include "private.h"

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(mediaplayer);

static const GUID IID_IClosable_local =
    { 0x30d5a829, 0x7fa4, 0x4026, {0x83, 0xbb, 0xd7, 0x5b, 0xae, 0x4e, 0xa9, 0x9e} };
static const GUID IID_IMediaPlayerSource2_local =
    { 0x82449b9f, 0x7322, 0x4c0b, {0xb0, 0x3b, 0x3e, 0x69, 0xa4, 0x82, 0x60, 0xc5} };

struct media_player
{
    IMediaPlayer IMediaPlayer_iface;
    IMediaPlayer2 IMediaPlayer2_iface;
    const void *IClosable_vtbl;
    const void *IMediaPlayerSource2_vtbl;
    LONG ref;

    ISystemMediaTransportControls *controls;
    HWND window;
    boolean auto_play;
    boolean is_looping;
    boolean is_muted;
    double volume;
    double playback_rate;
    IUnknown *source;
    INT64 next_token;
};

static inline struct media_player *impl_from_IMediaPlayer( IMediaPlayer *iface )
{
    return CONTAINING_RECORD( iface, struct media_player, IMediaPlayer_iface );
}

static HRESULT WINAPI media_player_QueryInterface( IMediaPlayer *iface, REFIID iid, void **out )
{
    struct media_player *impl = impl_from_IMediaPlayer( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IMediaPlayer ))
    {
        *out = &impl->IMediaPlayer_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IMediaPlayer2 ))
    {
        *out = &impl->IMediaPlayer2_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IClosable_local ))
    {
        *out = &impl->IClosable_vtbl;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IMediaPlayerSource2_local ))
    {
        *out = &impl->IMediaPlayerSource2_vtbl;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI media_player_AddRef( IMediaPlayer *iface )
{
    struct media_player *impl = impl_from_IMediaPlayer( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "iface %p increasing refcount to %lu.\n", iface, ref );
    return ref;
}

static ULONG WINAPI media_player_Release( IMediaPlayer *iface )
{
    struct media_player *impl = impl_from_IMediaPlayer( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    TRACE( "iface %p decreasing refcount to %lu.\n", iface, ref );

    if (!ref)
    {
        if (impl->source) IUnknown_Release( impl->source );
        ISystemMediaTransportControls_Release( impl->controls );
        DestroyWindow( impl->window );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI media_player_GetIids( IMediaPlayer *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI media_player_GetRuntimeClassName( IMediaPlayer *iface, HSTRING *class_name )
{
    FIXME( "iface %p, class_name %p stub!\n", iface, class_name );
    return E_NOTIMPL;
}

static HRESULT WINAPI media_player_GetTrustLevel( IMediaPlayer *iface, TrustLevel *trust_level )
{
    FIXME( "iface %p, trust_level %p stub!\n", iface, trust_level );
    return E_NOTIMPL;
}

static HRESULT WINAPI media_player_get_AutoPlay( IMediaPlayer *iface, boolean *value )
{
    struct media_player *impl = impl_from_IMediaPlayer( iface );
    if (!value) return E_POINTER;
    *value = impl->auto_play;
    return S_OK;
}

static HRESULT WINAPI media_player_put_AutoPlay( IMediaPlayer *iface, boolean value )
{
    struct media_player *impl = impl_from_IMediaPlayer( iface );
    impl->auto_play = value;
    return S_OK;
}

static HRESULT WINAPI media_player_get_NaturalDuration( IMediaPlayer *iface, TimeSpan *value )
{
    if (!value) return E_POINTER;
    value->Duration = 0;
    return S_OK;
}

static HRESULT WINAPI media_player_get_Position( IMediaPlayer *iface, TimeSpan *value )
{
    if (!value) return E_POINTER;
    value->Duration = 0;
    return S_OK;
}

static HRESULT WINAPI media_player_put_Position( IMediaPlayer *iface, TimeSpan value )
{
    return S_OK;
}

static HRESULT WINAPI media_player_get_BufferingProgress( IMediaPlayer *iface, DOUBLE *value )
{
    if (!value) return E_POINTER;
    *value = 1.0;
    return S_OK;
}

static HRESULT WINAPI media_player_get_CurrentState( IMediaPlayer *iface, MediaPlayerState *value )
{
    if (!value) return E_POINTER;
    *value = MediaPlayerState_Closed;
    return S_OK;
}

static HRESULT WINAPI media_player_get_CanSeek( IMediaPlayer *iface, boolean *value )
{
    if (!value) return E_POINTER;
    *value = FALSE;
    return S_OK;
}

static HRESULT WINAPI media_player_get_CanPause( IMediaPlayer *iface, boolean *value )
{
    if (!value) return E_POINTER;
    *value = FALSE;
    return S_OK;
}

static HRESULT WINAPI media_player_get_IsLoopingEnabled( IMediaPlayer *iface, boolean *value )
{
    struct media_player *impl = impl_from_IMediaPlayer( iface );
    if (!value) return E_POINTER;
    *value = impl->is_looping;
    return S_OK;
}

static HRESULT WINAPI media_player_put_IsLoopingEnabled( IMediaPlayer *iface, boolean value )
{
    struct media_player *impl = impl_from_IMediaPlayer( iface );
    impl->is_looping = value;
    return S_OK;
}

static HRESULT WINAPI media_player_get_IsProtected( IMediaPlayer *iface, boolean *value )
{
    if (!value) return E_POINTER;
    *value = FALSE;
    return S_OK;
}

static HRESULT WINAPI media_player_get_IsMuted( IMediaPlayer *iface, boolean *value )
{
    struct media_player *impl = impl_from_IMediaPlayer( iface );
    if (!value) return E_POINTER;
    *value = impl->is_muted;
    return S_OK;
}

static HRESULT WINAPI media_player_put_IsMuted( IMediaPlayer *iface, boolean value )
{
    struct media_player *impl = impl_from_IMediaPlayer( iface );
    impl->is_muted = value;
    return S_OK;
}

static HRESULT WINAPI media_player_get_PlaybackRate( IMediaPlayer *iface, DOUBLE *value )
{
    struct media_player *impl = impl_from_IMediaPlayer( iface );
    if (!value) return E_POINTER;
    *value = impl->playback_rate;
    return S_OK;
}

static HRESULT WINAPI media_player_put_PlaybackRate( IMediaPlayer *iface, DOUBLE value )
{
    struct media_player *impl = impl_from_IMediaPlayer( iface );
    impl->playback_rate = value;
    return S_OK;
}

static HRESULT WINAPI media_player_get_Volume( IMediaPlayer *iface, DOUBLE *value )
{
    struct media_player *impl = impl_from_IMediaPlayer( iface );
    if (!value) return E_POINTER;
    *value = impl->volume;
    return S_OK;
}

static HRESULT WINAPI media_player_put_Volume( IMediaPlayer *iface, DOUBLE value )
{
    struct media_player *impl = impl_from_IMediaPlayer( iface );
    impl->volume = value;
    return S_OK;
}

static HRESULT WINAPI media_player_get_PlaybackMediaMarkers( IMediaPlayer *iface, IPlaybackMediaMarkerSequence **value )
{
    if (value) *value = NULL;
    return S_OK;
}

static HRESULT WINAPI media_player_add_MediaOpened( IMediaPlayer *iface, ITypedEventHandler_MediaPlayer_IInspectable *value, EventRegistrationToken *token )
{
    struct media_player *impl = impl_from_IMediaPlayer( iface );
    if (token) token->value = InterlockedIncrement64( &impl->next_token );
    return S_OK;
}

static HRESULT WINAPI media_player_remove_MediaOpened( IMediaPlayer *iface, EventRegistrationToken token )
{
    return S_OK;
}

static HRESULT WINAPI media_player_add_MediaEnded( IMediaPlayer *iface, ITypedEventHandler_MediaPlayer_IInspectable *value, EventRegistrationToken *token )
{
    struct media_player *impl = impl_from_IMediaPlayer( iface );
    if (token) token->value = InterlockedIncrement64( &impl->next_token );
    return S_OK;
}

static HRESULT WINAPI media_player_remove_MediaEnded( IMediaPlayer *iface, EventRegistrationToken token )
{
    return S_OK;
}

static HRESULT WINAPI media_player_add_MediaFailed( IMediaPlayer *iface, ITypedEventHandler_MediaPlayer_MediaPlayerFailedEventArgs *value,
                                                    EventRegistrationToken *token )
{
    struct media_player *impl = impl_from_IMediaPlayer( iface );
    if (token) token->value = InterlockedIncrement64( &impl->next_token );
    return S_OK;
}

static HRESULT WINAPI media_player_remove_MediaFailed( IMediaPlayer *iface, EventRegistrationToken token )
{
    return S_OK;
}

static HRESULT WINAPI media_player_add_CurrentStateChanged( IMediaPlayer *iface, ITypedEventHandler_MediaPlayer_IInspectable *value,
                                                            EventRegistrationToken *token )
{
    struct media_player *impl = impl_from_IMediaPlayer( iface );
    if (token) token->value = InterlockedIncrement64( &impl->next_token );
    return S_OK;
}

static HRESULT WINAPI media_player_remove_CurrentStateChanged( IMediaPlayer *iface, EventRegistrationToken token )
{
    return S_OK;
}

static HRESULT WINAPI media_player_add_PlaybackMediaMarkerReached( IMediaPlayer *iface, ITypedEventHandler_MediaPlayer_PlaybackMediaMarkerReachedEventArgs *value,
                                                                   EventRegistrationToken *token )
{
    struct media_player *impl = impl_from_IMediaPlayer( iface );
    if (token) token->value = InterlockedIncrement64( &impl->next_token );
    return S_OK;
}

static HRESULT WINAPI media_player_remove_PlaybackMediaMarkerReached( IMediaPlayer *iface, EventRegistrationToken token )
{
    return S_OK;
}

static HRESULT WINAPI media_player_add_MediaPlayerRateChanged( IMediaPlayer *iface, ITypedEventHandler_MediaPlayer_MediaPlayerRateChangedEventArgs *value,
                                                               EventRegistrationToken *token )
{
    struct media_player *impl = impl_from_IMediaPlayer( iface );
    if (token) token->value = InterlockedIncrement64( &impl->next_token );
    return S_OK;
}

static HRESULT WINAPI media_player_remove_MediaPlayerRateChanged( IMediaPlayer *iface, EventRegistrationToken token )
{
    return S_OK;
}

static HRESULT WINAPI media_player_add_VolumeChanged( IMediaPlayer *iface, ITypedEventHandler_MediaPlayer_IInspectable *value, EventRegistrationToken *token )
{
    struct media_player *impl = impl_from_IMediaPlayer( iface );
    if (token) token->value = InterlockedIncrement64( &impl->next_token );
    return S_OK;
}

static HRESULT WINAPI media_player_remove_VolumeChanged( IMediaPlayer *iface, EventRegistrationToken token )
{
    return S_OK;
}

static HRESULT WINAPI media_player_add_SeekCompleted( IMediaPlayer *iface, ITypedEventHandler_MediaPlayer_IInspectable *value, EventRegistrationToken *token )
{
    struct media_player *impl = impl_from_IMediaPlayer( iface );
    if (token) token->value = InterlockedIncrement64( &impl->next_token );
    return S_OK;
}

static HRESULT WINAPI media_player_remove_SeekCompleted( IMediaPlayer *iface, EventRegistrationToken token )
{
    return S_OK;
}

static HRESULT WINAPI media_player_add_BufferingStarted( IMediaPlayer *iface, ITypedEventHandler_MediaPlayer_IInspectable *value, EventRegistrationToken *token )
{
    struct media_player *impl = impl_from_IMediaPlayer( iface );
    if (token) token->value = InterlockedIncrement64( &impl->next_token );
    return S_OK;
}

static HRESULT WINAPI media_player_remove_BufferingStarted( IMediaPlayer *iface, EventRegistrationToken token )
{
    return S_OK;
}

static HRESULT WINAPI media_player_add_BufferingEnded( IMediaPlayer *iface, ITypedEventHandler_MediaPlayer_IInspectable *value, EventRegistrationToken *token )
{
    struct media_player *impl = impl_from_IMediaPlayer( iface );
    if (token) token->value = InterlockedIncrement64( &impl->next_token );
    return S_OK;
}

static HRESULT WINAPI media_player_remove_BufferingEnded( IMediaPlayer *iface, EventRegistrationToken token )
{
    return S_OK;
}

static HRESULT WINAPI media_player_Play( IMediaPlayer *iface )
{
    return S_OK;
}

static HRESULT WINAPI media_player_Pause( IMediaPlayer *iface )
{
    return S_OK;
}

static HRESULT WINAPI media_player_SetUriSource( IMediaPlayer *iface, IUriRuntimeClass *value )
{
    FIXME( "iface %p, value %p stub!\n", iface, value );
    return E_NOTIMPL;
}

static const struct IMediaPlayerVtbl media_player_vtbl =
{
    /* IUnknown methods */
    media_player_QueryInterface,
    media_player_AddRef,
    media_player_Release,
    /* IInspectable methods */
    media_player_GetIids,
    media_player_GetRuntimeClassName,
    media_player_GetTrustLevel,
    /* IMediaPlayer methods */
    media_player_get_AutoPlay,
    media_player_put_AutoPlay,
    media_player_get_NaturalDuration,
    media_player_get_Position,
    media_player_put_Position,
    media_player_get_BufferingProgress,
    media_player_get_CurrentState,
    media_player_get_CanSeek,
    media_player_get_CanPause,
    media_player_get_IsLoopingEnabled,
    media_player_put_IsLoopingEnabled,
    media_player_get_IsProtected,
    media_player_get_IsMuted,
    media_player_put_IsMuted,
    media_player_get_PlaybackRate,
    media_player_put_PlaybackRate,
    media_player_get_Volume,
    media_player_put_Volume,
    media_player_get_PlaybackMediaMarkers,
    media_player_add_MediaOpened,
    media_player_remove_MediaOpened,
    media_player_add_MediaEnded,
    media_player_remove_MediaEnded,
    media_player_add_MediaFailed,
    media_player_remove_MediaFailed,
    media_player_add_CurrentStateChanged,
    media_player_remove_CurrentStateChanged,
    media_player_add_PlaybackMediaMarkerReached,
    media_player_remove_PlaybackMediaMarkerReached,
    media_player_add_MediaPlayerRateChanged,
    media_player_remove_MediaPlayerRateChanged,
    media_player_add_VolumeChanged,
    media_player_remove_VolumeChanged,
    media_player_add_SeekCompleted,
    media_player_remove_SeekCompleted,
    media_player_add_BufferingStarted,
    media_player_remove_BufferingStarted,
    media_player_add_BufferingEnded,
    media_player_remove_BufferingEnded,
    media_player_Play,
    media_player_Pause,
    media_player_SetUriSource,
};

DEFINE_IINSPECTABLE( media_player2, IMediaPlayer2, struct media_player, IMediaPlayer_iface )

static HRESULT WINAPI media_player2_get_SystemMediaTransportControls( IMediaPlayer2 *iface, ISystemMediaTransportControls **value )
{
    struct media_player *impl = impl_from_IMediaPlayer2( iface );

    TRACE( "iface %p, value %p\n", iface, value );

    *value = impl->controls;
    ISystemMediaTransportControls_AddRef( *value );
    return S_OK;
}

static HRESULT WINAPI media_player2_get_AudioCategory( IMediaPlayer2 *iface, MediaPlayerAudioCategory *value )
{
    FIXME( "iface %p, value %p stub!\n", iface, value );
    return E_NOTIMPL;
}

static HRESULT WINAPI media_player2_put_AudioCategory( IMediaPlayer2 *iface, MediaPlayerAudioCategory value )
{
    FIXME( "iface %p, value %#x stub!\n", iface, value );
    return E_NOTIMPL;
}

static HRESULT WINAPI media_player2_get_AudioDeviceType( IMediaPlayer2 *iface, MediaPlayerAudioDeviceType *value )
{
    FIXME( "iface %p, value %p stub!\n", iface, value );
    return E_NOTIMPL;
}

static HRESULT WINAPI media_player2_put_AudioDeviceType( IMediaPlayer2 *iface, MediaPlayerAudioDeviceType value )
{
    FIXME( "iface %p, value %#x stub!\n", iface, value );
    return E_NOTIMPL;
}

static const struct IMediaPlayer2Vtbl media_player2_vtbl =
{
    /* IUnknown methods */
    media_player2_QueryInterface,
    media_player2_AddRef,
    media_player2_Release,
    /* IInspectable methods */
    media_player2_GetIids,
    media_player2_GetRuntimeClassName,
    media_player2_GetTrustLevel,
    /* IMediaPlayer2 methods */
    media_player2_get_SystemMediaTransportControls,
    media_player2_get_AudioCategory,
    media_player2_put_AudioCategory,
    media_player2_get_AudioDeviceType,
    media_player2_put_AudioDeviceType,
};

struct media_player_statics
{
    IActivationFactory IActivationFactory_iface;
    LONG ref;
};

static inline struct media_player_statics *impl_from_IActivationFactory( IActivationFactory *iface )
{
    return CONTAINING_RECORD( iface, struct media_player_statics, IActivationFactory_iface );
}

static HRESULT WINAPI factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
    struct media_player_statics *impl = impl_from_IActivationFactory( iface );

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

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI factory_AddRef( IActivationFactory *iface )
{
    struct media_player_statics *impl = impl_from_IActivationFactory( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "iface %p increasing refcount to %lu.\n", iface, ref );
    return ref;
}

static ULONG WINAPI factory_Release( IActivationFactory *iface )
{
    struct media_player_statics *impl = impl_from_IActivationFactory( iface );
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

static HRESULT get_system_media_transport_controls( HWND *window, ISystemMediaTransportControls **controls )
{
    static const WCHAR *media_control_statics_name = L"Windows.Media.SystemMediaTransportControls";
    ISystemMediaTransportControlsInterop *media_control_interop_statics = NULL;
    IActivationFactory *factory = NULL;
    HSTRING str = NULL;
    HRESULT hr;
    HWND hwnd;

    FIXME( "shell integration not implemented.\n" );

    if (!(hwnd = CreateWindowExW( 0, L"static", NULL, WS_POPUP, 0, 0, 0, 0, NULL, NULL, GetModuleHandleW( NULL ), NULL ))) return HRESULT_FROM_WIN32( GetLastError() );

    if (FAILED(hr = WindowsCreateString( media_control_statics_name, wcslen( media_control_statics_name ), &str ))) goto done;
    if (SUCCEEDED(hr = RoGetActivationFactory( str, &IID_IActivationFactory, (void **)&factory )))
    {
        hr = IActivationFactory_QueryInterface( factory, &IID_ISystemMediaTransportControlsInterop, (void **)&media_control_interop_statics );
        IActivationFactory_Release( factory );
    }
    if (SUCCEEDED(hr))
    {
        hr = ISystemMediaTransportControlsInterop_GetForWindow( media_control_interop_statics, hwnd, &IID_ISystemMediaTransportControls, (void **)controls );
        ISystemMediaTransportControlsInterop_Release( media_control_interop_statics );
    }
    WindowsDeleteString( str );

done:
    if (FAILED(hr)) DestroyWindow( hwnd );
    else *window = hwnd;
    return hr;
}

static HRESULT WINAPI mp_closable_QI( void *iface, REFIID iid, void **out )
{
    struct media_player *impl = CONTAINING_RECORD( iface, struct media_player, IClosable_vtbl );
    return media_player_QueryInterface( &impl->IMediaPlayer_iface, iid, out );
}
static ULONG WINAPI mp_closable_AddRef( void *iface )
{
    struct media_player *impl = CONTAINING_RECORD( iface, struct media_player, IClosable_vtbl );
    return media_player_AddRef( &impl->IMediaPlayer_iface );
}
static ULONG WINAPI mp_closable_Release( void *iface )
{
    struct media_player *impl = CONTAINING_RECORD( iface, struct media_player, IClosable_vtbl );
    return media_player_Release( &impl->IMediaPlayer_iface );
}
static HRESULT WINAPI mp_closable_GetIids( void *iface, ULONG *c, IID **i ) { if (c) *c = 0; if (i) *i = NULL; return S_OK; }
static HRESULT WINAPI mp_closable_GetRuntimeClassName( void *iface, HSTRING *n )
{
    static const WCHAR cls[] = L"Windows.Media.Playback.MediaPlayer";
    return WindowsCreateString( cls, ARRAY_SIZE(cls) - 1, n );
}
static HRESULT WINAPI mp_closable_GetTrustLevel( void *iface, TrustLevel *l ) { if (l) *l = BaseTrust; return S_OK; }
static HRESULT WINAPI mp_closable_Close( void *iface ) { return S_OK; }

static const void * const mp_closable_vtbl[] =
{
    mp_closable_QI, mp_closable_AddRef, mp_closable_Release,
    mp_closable_GetIids, mp_closable_GetRuntimeClassName, mp_closable_GetTrustLevel,
    mp_closable_Close,
};

static HRESULT WINAPI mp_src2_QI( void *iface, REFIID iid, void **out )
{
    struct media_player *impl = CONTAINING_RECORD( iface, struct media_player, IMediaPlayerSource2_vtbl );
    return media_player_QueryInterface( &impl->IMediaPlayer_iface, iid, out );
}
static ULONG WINAPI mp_src2_AddRef( void *iface )
{
    struct media_player *impl = CONTAINING_RECORD( iface, struct media_player, IMediaPlayerSource2_vtbl );
    return media_player_AddRef( &impl->IMediaPlayer_iface );
}
static ULONG WINAPI mp_src2_Release( void *iface )
{
    struct media_player *impl = CONTAINING_RECORD( iface, struct media_player, IMediaPlayerSource2_vtbl );
    return media_player_Release( &impl->IMediaPlayer_iface );
}
static HRESULT WINAPI mp_src2_get_Source( void *iface, IUnknown **src )
{
    struct media_player *impl = CONTAINING_RECORD( iface, struct media_player, IMediaPlayerSource2_vtbl );
    if (!src) return E_POINTER;
    *src = impl->source;
    if (*src) IUnknown_AddRef( *src );
    return S_OK;
}
static HRESULT WINAPI mp_src2_put_Source( void *iface, IUnknown *src )
{
    struct media_player *impl = CONTAINING_RECORD( iface, struct media_player, IMediaPlayerSource2_vtbl );
    if (impl->source) IUnknown_Release( impl->source );
    impl->source = src;
    if (src) IUnknown_AddRef( src );
    return S_OK;
}

static const void * const mp_src2_vtbl[] =
{
    mp_src2_QI, mp_src2_AddRef, mp_src2_Release,
    mp_closable_GetIids, mp_closable_GetRuntimeClassName, mp_closable_GetTrustLevel,
    mp_src2_get_Source, mp_src2_put_Source,
};

static HRESULT WINAPI factory_ActivateInstance( IActivationFactory *iface, IInspectable **instance )
{
    struct media_player *impl;
    HRESULT hr;

    TRACE( "iface %p, instance %p.\n", iface, instance );

    *instance = NULL;

    if (!(impl = calloc( 1, sizeof( *impl ) ))) return E_OUTOFMEMORY;
    if (FAILED(hr = get_system_media_transport_controls( &impl->window, &impl->controls )))
    {
        free( impl );
        return hr;
    }

    impl->IMediaPlayer_iface.lpVtbl = &media_player_vtbl;
    impl->IMediaPlayer2_iface.lpVtbl = &media_player2_vtbl;
    impl->IClosable_vtbl = mp_closable_vtbl;
    impl->IMediaPlayerSource2_vtbl = mp_src2_vtbl;
    impl->volume = 1.0;
    impl->playback_rate = 1.0;
    impl->ref = 1;

    *instance = (IInspectable *)&impl->IMediaPlayer_iface;
    return S_OK;
}

static const struct IActivationFactoryVtbl factory_vtbl =
{
    /* IUnknown methods */
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

static struct media_player_statics media_player_statics =
{
    {&factory_vtbl},
    1,
};

static IActivationFactory *media_player_factory = &media_player_statics.IActivationFactory_iface;

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

    if (!wcscmp( buffer, RuntimeClass_Windows_Media_Playback_MediaPlayer ))
        IActivationFactory_QueryInterface( media_player_factory, &IID_IActivationFactory, (void **)factory );

    if (*factory) return S_OK;
    return E_NOINTERFACE;
}
