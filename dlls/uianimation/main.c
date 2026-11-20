/*
 * Uianimation main file.
 *
 * Copyright (C) 2018 Louis Lenders
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
#define COBJMACROS

#include <stdarg.h>

#include "windef.h"
#include "winbase.h"
#include "objbase.h"
#include "rpcproxy.h"
#include "oaidl.h"
#include "ocidl.h"

#include "initguid.h"
#include "uianimation.h"

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(uianimation);

struct class_factory
{
    IClassFactory IClassFactory_iface;
    HRESULT (*create_instance)(IUnknown *, REFIID, void **);
};

static inline struct class_factory *impl_from_IClassFactory( IClassFactory *iface )
{
    return CONTAINING_RECORD( iface, struct class_factory, IClassFactory_iface );
}

static HRESULT WINAPI class_factory_QueryInterface( IClassFactory *iface, REFIID iid, void **obj )
{
    if (IsEqualIID( iid, &IID_IUnknown ) ||
        IsEqualIID( iid, &IID_IClassFactory ))
    {
        IClassFactory_AddRef( iface );
        *obj = iface;
        return S_OK;
    }

    FIXME( "interface %s not implemented\n", debugstr_guid( iid ) );
    *obj = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI class_factory_AddRef( IClassFactory *iface )
{
    return 2;
}

static ULONG WINAPI class_factory_Release( IClassFactory *iface )
{
    return 1;
}

static HRESULT WINAPI class_factory_CreateInstance( IClassFactory *iface,
                                                    IUnknown *outer, REFIID iid,
                                                    void **obj )
{
    struct class_factory *This = impl_from_IClassFactory( iface );

    TRACE( "%p %s %p\n", outer, debugstr_guid( iid ), obj );

    *obj = NULL;
    return This->create_instance( outer, iid, obj );
}

static HRESULT WINAPI class_factory_LockServer( IClassFactory *iface,
                                                BOOL lock )
{
    FIXME( "%d: stub!\n", lock );
    return S_OK;
}

static const struct IClassFactoryVtbl class_factory_vtbl =
{
    class_factory_QueryInterface,
    class_factory_AddRef,
    class_factory_Release,
    class_factory_CreateInstance,
    class_factory_LockServer
};

/***********************************************************************
 *          IUIAnimationStoryboard
 */
struct animation_storyboard
{
    IUIAnimationStoryboard IUIAnimationStoryboard_iface;
    LONG ref;
};

struct animation_storyboard *impl_from_IUIAnimationStoryboard( IUIAnimationStoryboard *iface )
{
    return CONTAINING_RECORD( iface, struct animation_storyboard, IUIAnimationStoryboard_iface );
}

static HRESULT WINAPI animation_storyboard_QueryInterface( IUIAnimationStoryboard *iface,
                                                 REFIID iid, void **obj )
{
    struct animation_storyboard *This = impl_from_IUIAnimationStoryboard( iface );

    TRACE( "(%p)->(%s %p)\n", This, debugstr_guid( iid ), obj );

    if (IsEqualIID( iid, &IID_IUnknown ) ||
        IsEqualIID( iid, &IID_IUIAnimationStoryboard ))
    {
        IUIAnimationStoryboard_AddRef( iface );
        *obj = iface;
        return S_OK;
    }

    FIXME( "interface %s not implemented\n", debugstr_guid( iid ) );
    *obj = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI animation_storyboard_AddRef( IUIAnimationStoryboard *iface )
{
    struct animation_storyboard *This = impl_from_IUIAnimationStoryboard( iface );
    ULONG ref = InterlockedIncrement( &This->ref );

    TRACE( "(%p) ref = %lu\n", This, ref );
    return ref;
}

static ULONG WINAPI animation_storyboard_Release( IUIAnimationStoryboard *iface )
{
    struct animation_storyboard *This = impl_from_IUIAnimationStoryboard( iface );
    ULONG ref = InterlockedDecrement(&This->ref);

    TRACE( "(%p) ref = %lu\n", This, ref );

    if (!ref)
        free( This );

    return ref;
}

static HRESULT WINAPI animation_storyboard_AddTransition (IUIAnimationStoryboard *iface, IUIAnimationVariable *variable,
        IUIAnimationTransition *transition)
{
    struct animation_storyboard *This = impl_from_IUIAnimationStoryboard( iface );
    FIXME( "stub (%p)->(  )\n", This );
    return S_OK;
}

static HRESULT WINAPI animation_storyboard_AddKeyframeAtOffset (IUIAnimationStoryboard *iface, UI_ANIMATION_KEYFRAME existingframe,
        UI_ANIMATION_SECONDS offset, UI_ANIMATION_KEYFRAME *keyframe)
{
    struct animation_storyboard *This = impl_from_IUIAnimationStoryboard( iface );
    FIXME( "stub (%p)->(  )\n", This );
    return E_NOTIMPL;
}

static HRESULT WINAPI animation_storyboard_AddKeyframeAfterTransition (IUIAnimationStoryboard *iface,IUIAnimationTransition *transition,
        UI_ANIMATION_KEYFRAME *keyframe)
{
    struct animation_storyboard *This = impl_from_IUIAnimationStoryboard( iface );
    FIXME( "stub (%p)->(  )\n", This );
    return S_OK;
}

static HRESULT WINAPI animation_storyboard_AddTransitionAtKeyframe (IUIAnimationStoryboard *iface, IUIAnimationVariable *variable,
        IUIAnimationTransition *transition, UI_ANIMATION_KEYFRAME start_key)
{
    struct animation_storyboard *This = impl_from_IUIAnimationStoryboard( iface );
    FIXME( "stub (%p)->(  )\n", This );
    return E_NOTIMPL;
}

static HRESULT WINAPI animation_storyboard_AddTransitionBetweenKeyframes (IUIAnimationStoryboard *iface, IUIAnimationVariable *variable,
        IUIAnimationTransition *transition, UI_ANIMATION_KEYFRAME start_key, UI_ANIMATION_KEYFRAME end_key)
{
    struct animation_storyboard *This = impl_from_IUIAnimationStoryboard( iface );
    FIXME( "stub (%p)->(  )\n", This );
    return E_NOTIMPL;
}

static HRESULT WINAPI animation_storyboard_RepeatBetweenKeyframes (IUIAnimationStoryboard *iface, UI_ANIMATION_KEYFRAME start_key,
        UI_ANIMATION_KEYFRAME end_key, INT32 count)
{
    struct animation_storyboard *This = impl_from_IUIAnimationStoryboard( iface );
    FIXME( "stub (%p)->(  )\n", This );
    return S_OK;
}

static HRESULT WINAPI animation_storyboard_HoldVariable (IUIAnimationStoryboard *iface,  IUIAnimationVariable *variable)
{
    struct animation_storyboard *This = impl_from_IUIAnimationStoryboard( iface );
    FIXME( "stub (%p)->(  )\n", This );
    return E_NOTIMPL;
}

static HRESULT WINAPI animation_storyboard_SetLongestAcceptableDelay (IUIAnimationStoryboard *iface,  UI_ANIMATION_SECONDS delay)
{
    struct animation_storyboard *This = impl_from_IUIAnimationStoryboard( iface );
    FIXME( "stub (%p)->(  )\n", This );
    return E_NOTIMPL;
}

static HRESULT WINAPI animation_storyboard_Schedule (IUIAnimationStoryboard *iface, UI_ANIMATION_SECONDS now,
        UI_ANIMATION_SCHEDULING_RESULT *result)
{
    struct animation_storyboard *This = impl_from_IUIAnimationStoryboard( iface );
    FIXME( "stub (%p)->(  )\n", This );
    return 0;
}

static HRESULT WINAPI animation_storyboard_Conclude (IUIAnimationStoryboard *iface)
{
    struct animation_storyboard *This = impl_from_IUIAnimationStoryboard( iface );
    FIXME( "stub (%p)->(  )\n", This );
    return E_NOTIMPL;
}

static HRESULT WINAPI animation_storyboard_Finish (IUIAnimationStoryboard *iface,  UI_ANIMATION_SECONDS deadline)
{
    struct animation_storyboard *This = impl_from_IUIAnimationStoryboard( iface );
    FIXME( "stub (%p)->(  )\n", This );
    return E_NOTIMPL;
}

static HRESULT WINAPI animation_storyboard_Abandon (IUIAnimationStoryboard *iface)
{
    struct animation_storyboard *This = impl_from_IUIAnimationStoryboard( iface );
    FIXME( "stub (%p)->(  )\n", This );
    return E_NOTIMPL;
}

static HRESULT WINAPI animation_storyboard_SetTag(IUIAnimationStoryboard *iface, IUnknown *object, UINT32 id)
{
    struct animation_storyboard *This = impl_from_IUIAnimationStoryboard( iface );
    FIXME( "stub (%p)->(  )\n", This );
    return E_NOTIMPL;
}

static HRESULT WINAPI animation_storyboard_GetTag (IUIAnimationStoryboard *iface, IUnknown **object, UINT32 *id)
{
    struct animation_storyboard *This = impl_from_IUIAnimationStoryboard( iface );
    FIXME( "stub (%p)->(  )\n", This );
    return E_NOTIMPL;
}

static HRESULT WINAPI animation_storyboard_GetStatus (IUIAnimationStoryboard *iface, UI_ANIMATION_STORYBOARD_STATUS *status)
{
    struct animation_storyboard *This = impl_from_IUIAnimationStoryboard( iface );
    FIXME( "stub (%p)->(  )\n", This );
    return E_NOTIMPL;
}

static HRESULT WINAPI animation_storyboard_GetElapsedTime (IUIAnimationStoryboard *iface,  UI_ANIMATION_SECONDS *elapsed)
{
    struct animation_storyboard *This = impl_from_IUIAnimationStoryboard( iface );
    FIXME( "stub (%p)->(  )\n", This );
    return E_NOTIMPL;
}

static HRESULT WINAPI animation_storyboard_SetStoryboardEventHandler (IUIAnimationStoryboard *iface, IUIAnimationStoryboardEventHandler *handler)
{
    struct animation_storyboard *This = impl_from_IUIAnimationStoryboard( iface );
    FIXME( "stub (%p)->(  )\n", This );
    return S_OK;
}

const struct IUIAnimationStoryboardVtbl animation_storyboard_vtbl =
{
    animation_storyboard_QueryInterface,
    animation_storyboard_AddRef,
    animation_storyboard_Release,
    animation_storyboard_AddTransition,
    animation_storyboard_AddKeyframeAtOffset,
    animation_storyboard_AddKeyframeAfterTransition,
    animation_storyboard_AddTransitionAtKeyframe,
    animation_storyboard_AddTransitionBetweenKeyframes,
    animation_storyboard_RepeatBetweenKeyframes,
    animation_storyboard_HoldVariable,
    animation_storyboard_SetLongestAcceptableDelay,
    animation_storyboard_Schedule ,
    animation_storyboard_Conclude ,
    animation_storyboard_Finish ,
    animation_storyboard_Abandon,
    animation_storyboard_SetTag,
    animation_storyboard_GetTag ,
    animation_storyboard_GetStatus ,
    animation_storyboard_GetElapsedTime,
    animation_storyboard_SetStoryboardEventHandler
};

static HRESULT animation_storyboard_create( IUIAnimationStoryboard **obj )
{
    struct animation_storyboard *This = malloc( sizeof(*This) );

    if (!This) return E_OUTOFMEMORY;
    This->IUIAnimationStoryboard_iface.lpVtbl = &animation_storyboard_vtbl;
    This->ref = 1;

    *obj = &This->IUIAnimationStoryboard_iface;

    return S_OK;
}

/***********************************************************************
 *          IUIAnimationVariable
 */
struct animation_var
{
    IUIAnimationVariable IUIAnimationVariable_iface;
    LONG ref;
    DOUBLE initial;
};

struct animation_var *impl_from_IUIAnimationVariable( IUIAnimationVariable *iface )
{
    return CONTAINING_RECORD( iface, struct animation_var, IUIAnimationVariable_iface );
}

static HRESULT WINAPI animation_var_QueryInterface( IUIAnimationVariable *iface,
                                                 REFIID iid, void **obj )
{
    struct animation_var *This = impl_from_IUIAnimationVariable( iface );

    TRACE( "(%p)->(%s %p)\n", This, debugstr_guid( iid ), obj );

    if (IsEqualIID( iid, &IID_IUnknown ) ||
        IsEqualIID( iid, &IID_IUIAnimationVariable ))
    {
        IUIAnimationVariable_AddRef( iface );
        *obj = iface;
        return S_OK;
    }

    FIXME( "interface %s not implemented\n", debugstr_guid( iid ) );
    *obj = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI animation_var_AddRef( IUIAnimationVariable *iface )
{
    struct animation_var *This = impl_from_IUIAnimationVariable( iface );
    ULONG ref = InterlockedIncrement( &This->ref );

    TRACE( "(%p) ref = %lu\n", This, ref );
    return ref;
}

static ULONG WINAPI animation_var_Release( IUIAnimationVariable *iface )
{
    struct animation_var *This = impl_from_IUIAnimationVariable( iface );
    ULONG ref = InterlockedDecrement(&This->ref);

    TRACE( "(%p) ref = %lu\n", This, ref );

    if (!ref)
        free( This );

    return ref;
}

static HRESULT WINAPI animation_var_GetValue ( IUIAnimationVariable *iface, DOUBLE *value)
{
    struct animation_var *This = impl_from_IUIAnimationVariable( iface );
    FIXME( "stub (%p)->(  )\n", This);
    return E_NOTIMPL;
}

static HRESULT WINAPI animation_var_GetFinalValue ( IUIAnimationVariable *iface, DOUBLE *value)
{
    struct animation_var *This = impl_from_IUIAnimationVariable( iface );
    FIXME( "stub (%p)->(  )\n", This );
    return E_NOTIMPL;
}

static HRESULT WINAPI animation_var_GetPreviousValue ( IUIAnimationVariable *iface, DOUBLE *value)
{
    struct animation_var *This = impl_from_IUIAnimationVariable( iface );
    FIXME( "stub (%p)->(  )\n", This );
    return E_NOTIMPL;
}

static HRESULT WINAPI animation_var_GetIntegerValue ( IUIAnimationVariable *iface, INT32 *value)
{
    struct animation_var *This = impl_from_IUIAnimationVariable( iface );
    FIXME( "stub (%p)->(  )\n", This );
    return E_NOTIMPL;
}

static HRESULT WINAPI animation_var_GetFinalIntegerValue ( IUIAnimationVariable *iface, INT32 *value)
{
    struct animation_var *This = impl_from_IUIAnimationVariable( iface );
    FIXME( "stub (%p)->(  )\n", This );
    return E_NOTIMPL;
}

static HRESULT WINAPI animation_var_GetPreviousIntegerValue ( IUIAnimationVariable *iface, INT32 *value)
{
    struct animation_var *This = impl_from_IUIAnimationVariable( iface );
    FIXME( "stub (%p)->(  )\n", This );
    return E_NOTIMPL;
}

static HRESULT WINAPI animation_var_GetCurrentStoryboard ( IUIAnimationVariable *iface, IUIAnimationStoryboard **storyboard)
{
    struct animation_var *This = impl_from_IUIAnimationVariable( iface );
    FIXME( "stub (%p)->(  )\n", This );
    return E_NOTIMPL;
}

static HRESULT WINAPI animation_var_SetLowerBound ( IUIAnimationVariable *iface, DOUBLE bound)
{
    struct animation_var *This = impl_from_IUIAnimationVariable( iface );
    FIXME( "stub (%p)->(  )\n", This );
    return E_NOTIMPL;
}

static HRESULT WINAPI animation_var_SetUpperBound ( IUIAnimationVariable *iface, DOUBLE bound)
{
    struct animation_var *This = impl_from_IUIAnimationVariable( iface );
    FIXME( "stub (%p)->(  )\n", This );
    return E_NOTIMPL;
}

static HRESULT WINAPI animation_var_SetRoundingMode ( IUIAnimationVariable *iface,UI_ANIMATION_ROUNDING_MODE mode)
{
    struct animation_var *This = impl_from_IUIAnimationVariable( iface );
    FIXME( "stub (%p)->(  )\n", This );
    return S_OK;
}

static HRESULT WINAPI animation_var_SetTag ( IUIAnimationVariable *iface, IUnknown *object, UINT32 id)
{
    struct animation_var *This = impl_from_IUIAnimationVariable( iface );
    FIXME( "stub (%p)->(  )\n", This );
    return E_NOTIMPL;
}

static HRESULT WINAPI animation_var_GetTag ( IUIAnimationVariable *iface, IUnknown **object, UINT32 *id)
{
    struct animation_var *This = impl_from_IUIAnimationVariable( iface );
    FIXME( "stub (%p)->(  )\n", This );
    return E_NOTIMPL;
}

static HRESULT WINAPI animation_var_SetVariableChangeHandler ( IUIAnimationVariable *iface, IUIAnimationVariableChangeHandler *handler)
{
    struct animation_var *This = impl_from_IUIAnimationVariable( iface );
    FIXME( "stub (%p)->(  )\n", This );
    return S_OK;
}

static HRESULT WINAPI animation_var_SetVariableIntegerChangeHandler ( IUIAnimationVariable *iface,
        IUIAnimationVariableIntegerChangeHandler *handler)
{
    struct animation_var *This = impl_from_IUIAnimationVariable( iface );
    FIXME( "stub (%p)->(  )\n", This );
    return S_OK;
}

const struct IUIAnimationVariableVtbl animation_var_vtbl =
{
    animation_var_QueryInterface,
    animation_var_AddRef,
    animation_var_Release,
    animation_var_GetValue,
    animation_var_GetFinalValue,
    animation_var_GetPreviousValue,
    animation_var_GetIntegerValue,
    animation_var_GetFinalIntegerValue,
    animation_var_GetPreviousIntegerValue,
    animation_var_GetCurrentStoryboard,
    animation_var_SetLowerBound,
    animation_var_SetUpperBound,
    animation_var_SetRoundingMode,
    animation_var_SetTag,
    animation_var_GetTag,
    animation_var_SetVariableChangeHandler,
    animation_var_SetVariableIntegerChangeHandler,
};

static HRESULT animation_var_create(DOUBLE initial, IUIAnimationVariable **obj )
{
    struct animation_var *This = malloc( sizeof(*This) );

    if (!This) return E_OUTOFMEMORY;
    This->IUIAnimationVariable_iface.lpVtbl = &animation_var_vtbl;
    This->ref = 1;
    This->initial = initial;

    *obj = &This->IUIAnimationVariable_iface;

    return S_OK;
}

/***********************************************************************
 *          IUIAnimationManager
 */
struct manager
{
    IUIAnimationManager IUIAnimationManager_iface;
    LONG ref;
};

struct manager *impl_from_IUIAnimationManager( IUIAnimationManager *iface )
{
    return CONTAINING_RECORD( iface, struct manager, IUIAnimationManager_iface );
}

static HRESULT WINAPI manager_QueryInterface( IUIAnimationManager *iface, REFIID iid, void **obj )
{
    struct manager *This = impl_from_IUIAnimationManager( iface );

    TRACE( "(%p)->(%s %p)\n", This, debugstr_guid( iid ), obj );

    if (IsEqualIID( iid, &IID_IUnknown ) ||
        IsEqualIID( iid, &IID_IUIAnimationManager ))
    {
        IUIAnimationManager_AddRef( iface );
        *obj = iface;
        return S_OK;
    }

    FIXME( "interface %s not implemented\n", debugstr_guid( iid ) );
    *obj = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI manager_AddRef( IUIAnimationManager *iface )
{
    struct manager *This = impl_from_IUIAnimationManager( iface );
    ULONG ref = InterlockedIncrement( &This->ref );

    TRACE( "(%p) ref = %lu\n", This, ref );
    return ref;
}

static ULONG WINAPI manager_Release( IUIAnimationManager *iface )
{
    struct manager *This = impl_from_IUIAnimationManager( iface );
    ULONG ref = InterlockedDecrement(&This->ref);

    TRACE( "(%p) ref = %lu\n", This, ref );

    if (!ref)
    {
        free( This );
    }

    return ref;
}

static HRESULT WINAPI manager_CreateAnimationVariable( IUIAnimationManager *iface, DOUBLE initial_value, IUIAnimationVariable **variable )
{
    struct manager *This = impl_from_IUIAnimationManager( iface );
    TRACE( "(%p)->(%f, %p)\n", This, initial_value, variable );
    return animation_var_create(initial_value, variable);
}

static HRESULT WINAPI manager_ScheduleTransition( IUIAnimationManager *iface, IUIAnimationVariable *variable, IUIAnimationTransition *transition, UI_ANIMATION_SECONDS current_time )
{
    struct manager *This = impl_from_IUIAnimationManager( iface );
    FIXME( "stub (%p)->(%p, %p)\n", This, variable, transition );
    return E_NOTIMPL;
}

static HRESULT WINAPI manager_CreateStoryboard( IUIAnimationManager *iface, IUIAnimationStoryboard **storyboard )
{
    struct manager *This = impl_from_IUIAnimationManager( iface );
    TRACE( "(%p)->(%p)\n", This, storyboard );
    return animation_storyboard_create(storyboard);
}

static HRESULT WINAPI manager_FinishAllStoryboards( IUIAnimationManager *iface, UI_ANIMATION_SECONDS max_time )
{
    struct manager *This = impl_from_IUIAnimationManager( iface );
    FIXME( "stub (%p)->(%f)\n", This, max_time );
    return E_NOTIMPL;
}

static HRESULT WINAPI manager_AbandonAllStoryboards( IUIAnimationManager *iface )
{
    struct manager *This = impl_from_IUIAnimationManager( iface );
    FIXME( "stub (%p)->()\n", This );
    return E_NOTIMPL;
}

static HRESULT WINAPI manager_Update( IUIAnimationManager *iface, UI_ANIMATION_SECONDS cur_time, UI_ANIMATION_UPDATE_RESULT *update_result )
{
    struct manager *This = impl_from_IUIAnimationManager( iface );
    FIXME( "stub (%p)->(%f, %p)\n", This, cur_time, update_result );
    if (update_result)
        *update_result = UI_ANIMATION_UPDATE_VARIABLES_CHANGED;
    return S_OK;
}

static HRESULT WINAPI manager_GetVariableFromTag( IUIAnimationManager *iface, IUnknown *object, UINT32 id, IUIAnimationVariable **variable )
{
    struct manager *This = impl_from_IUIAnimationManager( iface );
    FIXME( "stub (%p)->(%p, %p)\n", This, object, variable );
    return E_NOTIMPL;
}

static HRESULT WINAPI manager_GetStoryboardFromTag( IUIAnimationManager *iface, IUnknown *object, UINT32 id, IUIAnimationStoryboard **storyboard )
{
    struct manager *This = impl_from_IUIAnimationManager( iface );
    FIXME( "stub (%p)->(%p, %d, %p)\n", This, object, id, storyboard );
    return E_NOTIMPL;
}

static HRESULT WINAPI manager_GetStatus( IUIAnimationManager *iface, UI_ANIMATION_MANAGER_STATUS *status )
{
    struct manager *This = impl_from_IUIAnimationManager( iface );
    FIXME( "stub (%p)->(%p)\n", This, status );
    return E_NOTIMPL;
}

static HRESULT WINAPI manager_SetAnimationMode( IUIAnimationManager *iface, UI_ANIMATION_MODE mode )
{
    struct manager *This = impl_from_IUIAnimationManager( iface );
    FIXME( "stub (%p)->(%d)\n", This, mode );
    return S_OK;
}

static HRESULT WINAPI manager_Pause( IUIAnimationManager *iface )
{
    struct manager *This = impl_from_IUIAnimationManager( iface );
    FIXME( "stub (%p)->()\n", This );
    return E_NOTIMPL;
}

static HRESULT WINAPI manager_Resume( IUIAnimationManager *iface )
{
    struct manager *This = impl_from_IUIAnimationManager( iface );
    FIXME( "stub (%p)->()\n", This );
    return E_NOTIMPL;
}

static HRESULT WINAPI manager_SetManagerEventHandler( IUIAnimationManager *iface, IUIAnimationManagerEventHandler *handler )
{
    struct manager *This = impl_from_IUIAnimationManager( iface );
    FIXME( "stub (%p)->(%p)\n", This, handler );
    return S_OK;
}

static HRESULT WINAPI manager_SetCancelPriorityComparison( IUIAnimationManager *iface, IUIAnimationPriorityComparison *comparison )
{
    struct manager *This = impl_from_IUIAnimationManager( iface );
    FIXME( "stub (%p)->(%p)\n", This, comparison );
    return E_NOTIMPL;
}

static HRESULT WINAPI manager_SetTrimPriorityComparison( IUIAnimationManager *iface, IUIAnimationPriorityComparison *comparison )
{
    struct manager *This = impl_from_IUIAnimationManager( iface );
    FIXME( "stub (%p)->(%p)\n", This, comparison );
    return E_NOTIMPL;
}

static HRESULT WINAPI manager_SetCompressPriorityComparison( IUIAnimationManager *iface, IUIAnimationPriorityComparison *comparison)
{
    struct manager *This = impl_from_IUIAnimationManager( iface );
    FIXME( "stub (%p)->(%p)\n", This, comparison );
    return E_NOTIMPL;
}

static HRESULT WINAPI manager_SetConcludePriorityComparison( IUIAnimationManager *iface, IUIAnimationPriorityComparison *comparison )
{
    struct manager *This = impl_from_IUIAnimationManager( iface );
    FIXME( "stub (%p)->(%p)\n", This, comparison );
    return E_NOTIMPL;
}

static HRESULT WINAPI manager_SetDefaultLongestAcceptableDelay( IUIAnimationManager *iface, UI_ANIMATION_SECONDS delay )
{
    struct manager *This = impl_from_IUIAnimationManager( iface );
    FIXME( "stub (%p)->(%f)\n", This, delay );
    return E_NOTIMPL;
}

static HRESULT WINAPI manager_Shutdown( IUIAnimationManager *iface )
{
    struct manager *This = impl_from_IUIAnimationManager( iface );
    FIXME( "stub (%p)->()\n", This );
    return E_NOTIMPL;
}

const struct IUIAnimationManagerVtbl manager_vtbl =
{
    manager_QueryInterface,
    manager_AddRef,
    manager_Release,
    manager_CreateAnimationVariable,
    manager_ScheduleTransition,
    manager_CreateStoryboard,
    manager_FinishAllStoryboards,
    manager_AbandonAllStoryboards,
    manager_Update,
    manager_GetVariableFromTag,
    manager_GetStoryboardFromTag,
    manager_GetStatus,
    manager_SetAnimationMode,
    manager_Pause,
    manager_Resume,
    manager_SetManagerEventHandler,
    manager_SetCancelPriorityComparison,
    manager_SetTrimPriorityComparison,
    manager_SetCompressPriorityComparison,
    manager_SetConcludePriorityComparison,
    manager_SetDefaultLongestAcceptableDelay,
    manager_Shutdown
};

static HRESULT manager_create( IUnknown *outer, REFIID iid, void **obj )
{
    struct manager *This = malloc( sizeof(*This) );
    HRESULT hr;

    if (!This) return E_OUTOFMEMORY;
    This->IUIAnimationManager_iface.lpVtbl = &manager_vtbl;
    This->ref = 1;

    hr = IUIAnimationManager_QueryInterface( &This->IUIAnimationManager_iface, iid, obj );

    IUIAnimationManager_Release( &This->IUIAnimationManager_iface );
    return hr;
}

/***********************************************************************
 *          IUIAnimationTimer
 */
struct timer
{
    IUIAnimationTimer IUIAnimationTimer_iface;
    LONG ref;
};

struct timer *impl_from_IUIAnimationTimer( IUIAnimationTimer *iface )
{
    return CONTAINING_RECORD( iface, struct timer, IUIAnimationTimer_iface );
}

static HRESULT WINAPI timer_QueryInterface( IUIAnimationTimer *iface, REFIID iid, void **obj )
{
    struct timer *This = impl_from_IUIAnimationTimer( iface );

    TRACE( "(%p)->(%s %p)\n", This, debugstr_guid( iid ), obj );

    if (IsEqualIID( iid, &IID_IUnknown ) ||
        IsEqualIID( iid, &IID_IUIAnimationTimer ))
    {
        IUIAnimationTimer_AddRef( iface );
        *obj = iface;
        return S_OK;
    }

    FIXME( "interface %s not implemented\n", debugstr_guid( iid ) );
    *obj = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI timer_AddRef( IUIAnimationTimer *iface )
{
    struct timer *This = impl_from_IUIAnimationTimer( iface );
    ULONG ref = InterlockedIncrement( &This->ref );

    TRACE( "(%p) ref = %lu\n", This, ref );
    return ref;
}

static ULONG WINAPI timer_Release( IUIAnimationTimer *iface )
{
    struct timer *This = impl_from_IUIAnimationTimer( iface );
    ULONG ref = InterlockedDecrement(&This->ref);

    TRACE( "(%p) ref = %lu\n", This, ref );

    if (!ref)
        free( This );

    return ref;
}

static HRESULT WINAPI timer_SetTimerUpdateHandler (IUIAnimationTimer *iface,
        IUIAnimationTimerUpdateHandler *update_handler,
        UI_ANIMATION_IDLE_BEHAVIOR idle_behaviour)
{
    struct timer *This = impl_from_IUIAnimationTimer( iface );
    FIXME( "stub (%p)->(%p, %d)\n", This, update_handler, idle_behaviour );
    return E_NOTIMPL;
}

 static HRESULT WINAPI timer_SetTimerEventHandler (IUIAnimationTimer *iface,
        IUIAnimationTimerEventHandler *handler)
{
    struct timer *This = impl_from_IUIAnimationTimer( iface );
    FIXME( "stub (%p)->()\n", This );
    return S_OK;
}

static HRESULT WINAPI timer_Enable (IUIAnimationTimer *iface)
{
    struct timer *This = impl_from_IUIAnimationTimer( iface );
    FIXME( "stub (%p)->()\n", This );
    return S_OK;
}

static HRESULT WINAPI timer_Disable (IUIAnimationTimer *iface)
{
    struct timer *This = impl_from_IUIAnimationTimer( iface );
    FIXME( "stub (%p)->()\n", This );
    return E_NOTIMPL;
}

static HRESULT WINAPI timer_IsEnabled (IUIAnimationTimer *iface)
{
    struct timer *This = impl_from_IUIAnimationTimer( iface );
    FIXME( "stub (%p)->()\n", This );
    return E_NOTIMPL;
}

static HRESULT WINAPI timer_GetTime (IUIAnimationTimer *iface, UI_ANIMATION_SECONDS *seconds)
{
    struct timer *This = impl_from_IUIAnimationTimer( iface );
    FIXME( "stub (%p)->(%p)\n", This, seconds );
    return S_OK;
}

static HRESULT WINAPI timer_SetFrameRateThreshold (IUIAnimationTimer *iface, UINT32 frames_per_sec)
{
    struct timer *This = impl_from_IUIAnimationTimer( iface );
    FIXME( "stub (%p)->(%d)\n", This, frames_per_sec );
    return E_NOTIMPL;
}

const struct IUIAnimationTimerVtbl timer_vtbl =
{
    timer_QueryInterface,
    timer_AddRef,
    timer_Release,
    timer_SetTimerUpdateHandler,
    timer_SetTimerEventHandler,
    timer_Enable,
    timer_Disable,
    timer_IsEnabled,
    timer_GetTime,
    timer_SetFrameRateThreshold,
};

static HRESULT timer_create( IUnknown *outer, REFIID iid, void **obj )
{
    struct timer *This = malloc( sizeof(*This) );
    HRESULT hr;

    if (!This)
        return E_OUTOFMEMORY;
    This->IUIAnimationTimer_iface.lpVtbl = &timer_vtbl;
    This->ref = 1;

    hr = IUIAnimationTimer_QueryInterface( &This->IUIAnimationTimer_iface, iid, obj );

    IUIAnimationTimer_Release( &This->IUIAnimationTimer_iface );
    return hr;
}

/***********************************************************************
 *          IUIAnimationTransitionFactory
 */
struct tr_factory
{
    IUIAnimationTransitionFactory IUIAnimationTransitionFactory_iface;
    LONG ref;
};

struct tr_factory *impl_from_IUIAnimationTransitionFactory( IUIAnimationTransitionFactory *iface )
{
    return CONTAINING_RECORD( iface, struct tr_factory, IUIAnimationTransitionFactory_iface );
}

static HRESULT WINAPI tr_factory_QueryInterface( IUIAnimationTransitionFactory *iface, REFIID iid, void **obj )
{
    struct tr_factory *This = impl_from_IUIAnimationTransitionFactory( iface );

    TRACE( "(%p)->(%s %p)\n", This, debugstr_guid( iid ), obj );

    if (IsEqualIID( iid, &IID_IUnknown ) ||
        IsEqualIID( iid, &IID_IUIAnimationTransitionFactory ))
    {
        IUIAnimationTransitionFactory_AddRef( iface );
        *obj = iface;
        return S_OK;
    }

    FIXME( "interface %s not implemented\n", debugstr_guid( iid ) );
    *obj = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI tr_factory_AddRef( IUIAnimationTransitionFactory *iface )
{
    struct tr_factory *This = impl_from_IUIAnimationTransitionFactory( iface );
    ULONG ref = InterlockedIncrement( &This->ref );

    TRACE( "(%p) ref = %lu\n", This, ref );
    return ref;
}

static ULONG WINAPI tr_factory_Release( IUIAnimationTransitionFactory *iface )
{
    struct tr_factory *This = impl_from_IUIAnimationTransitionFactory( iface );
    ULONG ref = InterlockedDecrement(&This->ref);

    TRACE( "(%p) ref = %lu\n", This, ref );

    if (!ref)
        free( This );

    return ref;
}

static HRESULT WINAPI tr_factory_CreateTransition(IUIAnimationTransitionFactory *iface,
          IUIAnimationInterpolator *interpolator, IUIAnimationTransition **transition)
{
    struct tr_factory *This = impl_from_IUIAnimationTransitionFactory( iface );
    FIXME( "stub (%p)->(%p, %p)\n", This, interpolator, transition );
    return E_NOTIMPL;
}

const struct IUIAnimationTransitionFactoryVtbl tr_factory_vtbl =
{
    tr_factory_QueryInterface,
    tr_factory_AddRef,
    tr_factory_Release,
    tr_factory_CreateTransition
};

static HRESULT transition_create( IUnknown *outer, REFIID iid, void **obj )
{
    struct tr_factory *This = malloc( sizeof(*This) );
    HRESULT hr;

    if (!This) return E_OUTOFMEMORY;
    This->IUIAnimationTransitionFactory_iface.lpVtbl = &tr_factory_vtbl;
    This->ref = 1;

    hr = IUIAnimationTransitionFactory_QueryInterface( &This->IUIAnimationTransitionFactory_iface, iid, obj );

    IUIAnimationTransitionFactory_Release( &This->IUIAnimationTransitionFactory_iface );
    return hr;
}

/***********************************************************************
 *          IUITransitionLibrary
 */
struct tr_library
{
    IUIAnimationTransitionLibrary IUIAnimationTransitionLibrary_iface;
    LONG ref;
};

struct tr_library *impl_from_IUIAnimationTransitionLibrary( IUIAnimationTransitionLibrary *iface )
{
    return CONTAINING_RECORD( iface, struct tr_library, IUIAnimationTransitionLibrary_iface );
}

static HRESULT WINAPI tr_library_QueryInterface( IUIAnimationTransitionLibrary *iface,
                                                 REFIID iid, void **obj )
{
    struct tr_library *This = impl_from_IUIAnimationTransitionLibrary( iface );

    TRACE( "(%p)->(%s %p)\n", This, debugstr_guid( iid ), obj );

    if (IsEqualIID( iid, &IID_IUnknown ) ||
        IsEqualIID( iid, &IID_IUIAnimationTransitionLibrary ))
    {
        IUIAnimationTransitionLibrary_AddRef( iface );
        *obj = iface;
        return S_OK;
    }

    FIXME( "interface %s not implemented\n", debugstr_guid( iid ) );
    *obj = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI tr_library_AddRef( IUIAnimationTransitionLibrary *iface )
{
    struct tr_library *This = impl_from_IUIAnimationTransitionLibrary( iface );
    ULONG ref = InterlockedIncrement( &This->ref );

    TRACE( "(%p) ref = %lu\n", This, ref );
    return ref;
}

static ULONG WINAPI tr_library_Release( IUIAnimationTransitionLibrary *iface )
{
    struct tr_library *This = impl_from_IUIAnimationTransitionLibrary( iface );
    ULONG ref = InterlockedDecrement(&This->ref);

    TRACE( "(%p) ref = %lu\n", This, ref );

    if (!ref)
        free( This );

    return ref;
}

static HRESULT WINAPI tr_library_CreateInstantaneousTransition(IUIAnimationTransitionLibrary *iface,
        double finalValue, IUIAnimationTransition **transition)
{
    struct tr_library *This = impl_from_IUIAnimationTransitionLibrary( iface );
    FIXME( "stub (%p)->(%f, %p)\n", This, finalValue, transition );
    return E_NOTIMPL;
}

static HRESULT WINAPI tr_library_CreateConstantTransition(IUIAnimationTransitionLibrary *iface,
        double duration, IUIAnimationTransition **transition)
{
    struct tr_library *This = impl_from_IUIAnimationTransitionLibrary( iface );
    FIXME( "stub (%p)->(%f, %p)\n", This, duration, transition );
    return E_NOTIMPL;
}

static HRESULT WINAPI tr_library_CreateDiscreteTransition(IUIAnimationTransitionLibrary *iface,
        double delay, double finalValue, double hold, IUIAnimationTransition **transition)
{
    struct tr_library *This = impl_from_IUIAnimationTransitionLibrary( iface );
    FIXME( "stub (%p)->(  )\n", This );
    return E_NOTIMPL;
}

static HRESULT WINAPI tr_library_CreateLinearTransition(IUIAnimationTransitionLibrary *iface,
        double duration, double finalValue, IUIAnimationTransition **transition)
{
    struct tr_library *This = impl_from_IUIAnimationTransitionLibrary( iface );
    FIXME( "stub (%p)->(%f, %f, %p)\n", This, duration, finalValue, transition );
    return E_NOTIMPL;
}

static HRESULT WINAPI tr_library_CreateLinearTransitionFromSpeed(IUIAnimationTransitionLibrary *iface,
        double speed, double finalValue, IUIAnimationTransition **transition)
{
    struct tr_library *This = impl_from_IUIAnimationTransitionLibrary( iface );
    FIXME( "stub (%p)->(%f, %f, %p)\n", This, speed, finalValue, transition );
    return E_NOTIMPL;
}

static HRESULT WINAPI tr_library_CreateSinusoidalTransitionFromVelocity(IUIAnimationTransitionLibrary *iface,
        double duration, double period, IUIAnimationTransition **transition)
{
    struct tr_library *This = impl_from_IUIAnimationTransitionLibrary( iface );
    FIXME( "stub (%p)->(%f, %f, %p)\n", This, duration, period, transition );
    return E_NOTIMPL;
}

static HRESULT WINAPI tr_library_CreateSinusoidalTransitionFromRange(IUIAnimationTransitionLibrary *iface,
        double duration, double minimumValue, double maximumValue, double period,
        UI_ANIMATION_SLOPE slope, IUIAnimationTransition **transition)
{
    struct tr_library *This = impl_from_IUIAnimationTransitionLibrary( iface );
    FIXME( "stub (%p)->(%f, %f, %f, %f, %d, %p)\n", This, duration, minimumValue, maximumValue, period, slope, transition );
    return E_NOTIMPL;
}

static HRESULT WINAPI tr_library_CreateAccelerateDecelerateTransition(IUIAnimationTransitionLibrary *iface,
        double duration, double finalValue, double accelerationRatio, double decelerationRatio,
        IUIAnimationTransition **transition)
{
    struct tr_library *This = impl_from_IUIAnimationTransitionLibrary( iface );
    FIXME( "stub (%p)->(%f, %f, %f, %f, %p)\n", This, duration, finalValue, accelerationRatio, decelerationRatio, transition );
    return E_NOTIMPL;
}

static HRESULT WINAPI tr_library_CreateReversalTransition(IUIAnimationTransitionLibrary *iface, double duration,
        IUIAnimationTransition **transition)
{
    struct tr_library *This = impl_from_IUIAnimationTransitionLibrary( iface );
    FIXME( "stub (%p)->(%f, %p)\n", This, duration, transition );
    return E_NOTIMPL;
}

static HRESULT WINAPI tr_library_CreateCubicTransition(IUIAnimationTransitionLibrary *iface, double duration,
        double finalValue, double finalVelocity, IUIAnimationTransition **transition)
{
    struct tr_library *This = impl_from_IUIAnimationTransitionLibrary( iface );
    FIXME( "stub (%p)->(%f, %f, %f, %p)\n", This, duration, finalValue, finalVelocity, transition );
    return E_NOTIMPL;
}

static HRESULT WINAPI tr_library_CreateSmoothStopTransition(IUIAnimationTransitionLibrary *iface,
        double maximumDuration, double finalValue, IUIAnimationTransition **transition)
{
    struct tr_library *This = impl_from_IUIAnimationTransitionLibrary( iface );
    FIXME( "stub (%p)->(%f, %f, %p)\n", This, maximumDuration, finalValue, transition );
    return E_NOTIMPL;
}

static HRESULT WINAPI tr_library_CreateParabolicTransitionFromAcceleration(IUIAnimationTransitionLibrary *iface,
        double finalValue, double finalVelocity, double acceleration, IUIAnimationTransition **transition)
{
    struct tr_library *This = impl_from_IUIAnimationTransitionLibrary( iface );
    FIXME( "stub (%p)->(  )\n", This );
    return E_NOTIMPL;
}

const struct IUIAnimationTransitionLibraryVtbl tr_library_vtbl =
{
    tr_library_QueryInterface,
    tr_library_AddRef,
    tr_library_Release,
    tr_library_CreateInstantaneousTransition,
    tr_library_CreateConstantTransition,
    tr_library_CreateDiscreteTransition,
    tr_library_CreateLinearTransition,
    tr_library_CreateLinearTransitionFromSpeed,
    tr_library_CreateSinusoidalTransitionFromVelocity,
    tr_library_CreateSinusoidalTransitionFromRange,
    tr_library_CreateAccelerateDecelerateTransition,
    tr_library_CreateReversalTransition,
    tr_library_CreateCubicTransition,
    tr_library_CreateSmoothStopTransition,
    tr_library_CreateParabolicTransitionFromAcceleration,
};

static HRESULT library_create( IUnknown *outer, REFIID iid, void **obj )
{
    struct tr_library *This = malloc( sizeof(*This) );
    HRESULT hr;

    if (!This) return E_OUTOFMEMORY;
    This->IUIAnimationTransitionLibrary_iface.lpVtbl = &tr_library_vtbl;
    This->ref = 1;

    hr = IUIAnimationTransitionLibrary_QueryInterface( &This->IUIAnimationTransitionLibrary_iface, iid, obj );

    IUIAnimationTransitionLibrary_Release( &This->IUIAnimationTransitionLibrary_iface );
    return hr;
}

static struct class_factory manager_cf = { { &class_factory_vtbl }, manager_create };
static struct class_factory timer_cf   = { { &class_factory_vtbl }, timer_create };
static struct class_factory transition_cf = { { &class_factory_vtbl }, transition_create };
static struct class_factory library_cf = { { &class_factory_vtbl }, library_create };

/*
 * UIAnimation2 interfaces (Windows 8+ / DirectManipulation)
 */
DEFINE_GUID(CLSID_UIAnimationManager2, 0xd25d8842, 0x8884, 0x4a4a, 0xb3, 0x21, 0x09, 0x13, 0x14, 0x37, 0x9b, 0xdd);
DEFINE_GUID(IID_IUIAnimationManager2, 0xd8b6f7d4, 0x4109, 0x4d3f, 0xac, 0xee, 0x87, 0x99, 0x26, 0x96, 0x8c, 0xb1);
DEFINE_GUID(CLSID_UIAnimationTransitionLibrary2, 0x812f944a, 0xc5c8, 0x4cd9, 0xb0, 0xa6, 0xb3, 0xda, 0x80, 0x2f, 0x22, 0x8d);
DEFINE_GUID(IID_IUIAnimationTransitionLibrary2, 0x03cfae53, 0x9580, 0x4ee3, 0xb3, 0x63, 0x2e, 0xce, 0x51, 0xb4, 0xaf, 0x6a);
DEFINE_GUID(IID_IUIAnimationVariable2, 0x4914b304, 0x96ab, 0x44d9, 0x9e, 0x77, 0xd5, 0x10, 0x9b, 0x7e, 0x74, 0x66);
DEFINE_GUID(IID_IUIAnimationStoryboard2, 0xae289cd2, 0x12d4, 0x4945, 0x94, 0x19, 0x9e, 0x41, 0xbe, 0x03, 0x4d, 0xf2);
DEFINE_GUID(IID_IUIAnimationTransition2, 0x62ff9123, 0xa85a, 0x4e9b, 0xa2, 0x18, 0x43, 0x5a, 0x93, 0xe2, 0x68, 0xfd);

typedef struct IUIAnimationManager2 IUIAnimationManager2;
typedef struct IUIAnimationTransitionLibrary2 IUIAnimationTransitionLibrary2;
typedef struct IUIAnimationVariable2 IUIAnimationVariable2;
typedef struct IUIAnimationStoryboard2 IUIAnimationStoryboard2;
typedef struct IUIAnimationTransition2 IUIAnimationTransition2;

typedef struct IUIAnimationStoryboardEventHandler2Vtbl
{
    HRESULT (WINAPI *QueryInterface)(IUnknown *iface, REFIID iid, void **obj);
    ULONG (WINAPI *AddRef)(IUnknown *iface);
    ULONG (WINAPI *Release)(IUnknown *iface);
    HRESULT (WINAPI *OnStoryboardStatusChanged)(IUnknown *iface, IUIAnimationStoryboard2 *storyboard,
            UI_ANIMATION_STORYBOARD_STATUS newStatus, UI_ANIMATION_STORYBOARD_STATUS previousStatus);
    HRESULT (WINAPI *OnStoryboardUpdated)(IUnknown *iface, IUIAnimationStoryboard2 *storyboard);
} IUIAnimationStoryboardEventHandler2Vtbl;

typedef struct IUIAnimationVariableChangeHandler2Vtbl
{
    HRESULT (WINAPI *QueryInterface)(IUnknown *iface, REFIID iid, void **obj);
    ULONG (WINAPI *AddRef)(IUnknown *iface);
    ULONG (WINAPI *Release)(IUnknown *iface);
    HRESULT (WINAPI *OnValueChanged)(IUnknown *iface, IUIAnimationStoryboard2 *storyboard,
            IUIAnimationVariable2 *variable, DOUBLE *newValue, DOUBLE *previousValue, UINT cDimension);
} IUIAnimationVariableChangeHandler2Vtbl;

typedef struct IUIAnimationTransition2Vtbl
{
    HRESULT (WINAPI *QueryInterface)(IUIAnimationTransition2 *iface, REFIID iid, void **obj);
    ULONG (WINAPI *AddRef)(IUIAnimationTransition2 *iface);
    ULONG (WINAPI *Release)(IUIAnimationTransition2 *iface);
    HRESULT (WINAPI *GetDimension)(IUIAnimationTransition2 *iface, UINT *dimension);
    HRESULT (WINAPI *SetInitialValue)(IUIAnimationTransition2 *iface, DOUBLE value);
    HRESULT (WINAPI *SetInitialVectorValue)(IUIAnimationTransition2 *iface, const DOUBLE *value, UINT cDimension);
    HRESULT (WINAPI *SetInitialVelocity)(IUIAnimationTransition2 *iface, DOUBLE velocity);
    HRESULT (WINAPI *SetInitialVectorVelocity)(IUIAnimationTransition2 *iface, const DOUBLE *velocity, UINT cDimension);
    HRESULT (WINAPI *IsDurationKnown)(IUIAnimationTransition2 *iface);
    HRESULT (WINAPI *GetDuration)(IUIAnimationTransition2 *iface, UI_ANIMATION_SECONDS *duration);
} IUIAnimationTransition2Vtbl;

struct IUIAnimationTransition2 { const IUIAnimationTransition2Vtbl *lpVtbl; };

typedef struct IUIAnimationVariable2Vtbl
{
    HRESULT (WINAPI *QueryInterface)(IUIAnimationVariable2 *iface, REFIID iid, void **obj);
    ULONG (WINAPI *AddRef)(IUIAnimationVariable2 *iface);
    ULONG (WINAPI *Release)(IUIAnimationVariable2 *iface);
    HRESULT (WINAPI *GetDimension)(IUIAnimationVariable2 *iface, UINT *dimension);
    HRESULT (WINAPI *GetValue)(IUIAnimationVariable2 *iface, DOUBLE *value);
    HRESULT (WINAPI *GetFinalValue)(IUIAnimationVariable2 *iface, DOUBLE *finalValue);
    HRESULT (WINAPI *GetPreviousValue)(IUIAnimationVariable2 *iface, DOUBLE *previousValue);
    HRESULT (WINAPI *GetIntegerValue)(IUIAnimationVariable2 *iface, INT32 *value);
    HRESULT (WINAPI *GetFinalIntegerValue)(IUIAnimationVariable2 *iface, INT32 *finalValue);
    HRESULT (WINAPI *GetPreviousIntegerValue)(IUIAnimationVariable2 *iface, INT32 *previousValue);
    HRESULT (WINAPI *GetVectorValue)(IUIAnimationVariable2 *iface, DOUBLE *value, UINT cDimension);
    HRESULT (WINAPI *GetFinalVectorValue)(IUIAnimationVariable2 *iface, DOUBLE *finalValue, UINT cDimension);
    HRESULT (WINAPI *GetPreviousVectorValue)(IUIAnimationVariable2 *iface, DOUBLE *previousValue, UINT cDimension);
    HRESULT (WINAPI *GetIntegerVectorValue)(IUIAnimationVariable2 *iface, INT32 *value, UINT cDimension);
    HRESULT (WINAPI *GetFinalIntegerVectorValue)(IUIAnimationVariable2 *iface, INT32 *finalValue, UINT cDimension);
    HRESULT (WINAPI *GetPreviousIntegerVectorValue)(IUIAnimationVariable2 *iface, INT32 *previousValue, UINT cDimension);
    HRESULT (WINAPI *GetCurve)(IUIAnimationVariable2 *iface, IUnknown *animation);
    HRESULT (WINAPI *GetVectorCurve)(IUIAnimationVariable2 *iface, IUnknown **animation, UINT cDimension);
    HRESULT (WINAPI *GetCurrentStoryboard)(IUIAnimationVariable2 *iface, IUIAnimationStoryboard2 **storyboard);
    HRESULT (WINAPI *SetLowerBound)(IUIAnimationVariable2 *iface, DOUBLE bound);
    HRESULT (WINAPI *SetUpperBound)(IUIAnimationVariable2 *iface, DOUBLE bound);
    HRESULT (WINAPI *SetLowerBoundVector)(IUIAnimationVariable2 *iface, const DOUBLE *bound, UINT cDimension);
    HRESULT (WINAPI *SetUpperBoundVector)(IUIAnimationVariable2 *iface, const DOUBLE *bound, UINT cDimension);
    HRESULT (WINAPI *SetRoundingMode)(IUIAnimationVariable2 *iface, UI_ANIMATION_ROUNDING_MODE mode);
    HRESULT (WINAPI *SetTag)(IUIAnimationVariable2 *iface, IUnknown *object, UINT32 id);
    HRESULT (WINAPI *GetTag)(IUIAnimationVariable2 *iface, IUnknown **object, UINT32 *id);
    HRESULT (WINAPI *SetVariableChangeHandler)(IUIAnimationVariable2 *iface, IUnknown *handler, BOOL reg);
    HRESULT (WINAPI *SetVariableIntegerChangeHandler)(IUIAnimationVariable2 *iface, IUnknown *handler, BOOL reg);
    HRESULT (WINAPI *SetVariableCurveChangeHandler)(IUIAnimationVariable2 *iface, IUnknown *handler);
} IUIAnimationVariable2Vtbl;

struct IUIAnimationVariable2 { const IUIAnimationVariable2Vtbl *lpVtbl; };

typedef struct IUIAnimationStoryboard2Vtbl
{
    HRESULT (WINAPI *QueryInterface)(IUIAnimationStoryboard2 *iface, REFIID iid, void **obj);
    ULONG (WINAPI *AddRef)(IUIAnimationStoryboard2 *iface);
    ULONG (WINAPI *Release)(IUIAnimationStoryboard2 *iface);
    HRESULT (WINAPI *AddTransition)(IUIAnimationStoryboard2 *iface, IUIAnimationVariable2 *variable, IUIAnimationTransition2 *transition);
    HRESULT (WINAPI *AddKeyframeAtOffset)(IUIAnimationStoryboard2 *iface, UI_ANIMATION_KEYFRAME existingKeyframe, UI_ANIMATION_SECONDS offset, UI_ANIMATION_KEYFRAME *keyframe);
    HRESULT (WINAPI *AddKeyframeAfterTransition)(IUIAnimationStoryboard2 *iface, IUIAnimationTransition2 *transition, UI_ANIMATION_KEYFRAME *keyframe);
    HRESULT (WINAPI *AddTransitionAtKeyframe)(IUIAnimationStoryboard2 *iface, IUIAnimationVariable2 *variable, IUIAnimationTransition2 *transition, UI_ANIMATION_KEYFRAME startKeyframe);
    HRESULT (WINAPI *AddTransitionBetweenKeyframes)(IUIAnimationStoryboard2 *iface, IUIAnimationVariable2 *variable, IUIAnimationTransition2 *transition, UI_ANIMATION_KEYFRAME startKeyframe, UI_ANIMATION_KEYFRAME endKeyframe);
    HRESULT (WINAPI *RepeatBetweenKeyframes)(IUIAnimationStoryboard2 *iface, UI_ANIMATION_KEYFRAME startKeyframe, UI_ANIMATION_KEYFRAME endKeyframe, DOUBLE cRepetition, INT repeatMode, IUnknown *handler, UINT_PTR id, BOOL reg);
    HRESULT (WINAPI *HoldVariable)(IUIAnimationStoryboard2 *iface, IUIAnimationVariable2 *variable);
    HRESULT (WINAPI *SetLongestAcceptableDelay)(IUIAnimationStoryboard2 *iface, UI_ANIMATION_SECONDS delay);
    HRESULT (WINAPI *SetSkipDuration)(IUIAnimationStoryboard2 *iface, UI_ANIMATION_SECONDS secondsDuration);
    HRESULT (WINAPI *Schedule)(IUIAnimationStoryboard2 *iface, UI_ANIMATION_SECONDS timeNow, UI_ANIMATION_SCHEDULING_RESULT *schedulingResult);
    HRESULT (WINAPI *Conclude)(IUIAnimationStoryboard2 *iface);
    HRESULT (WINAPI *Finish)(IUIAnimationStoryboard2 *iface, UI_ANIMATION_SECONDS completionDeadline);
    HRESULT (WINAPI *Abandon)(IUIAnimationStoryboard2 *iface);
    HRESULT (WINAPI *SetTag)(IUIAnimationStoryboard2 *iface, IUnknown *object, UINT32 id);
    HRESULT (WINAPI *GetTag)(IUIAnimationStoryboard2 *iface, IUnknown **object, UINT32 *id);
    HRESULT (WINAPI *GetStatus)(IUIAnimationStoryboard2 *iface, UI_ANIMATION_STORYBOARD_STATUS *status);
    HRESULT (WINAPI *GetElapsedTime)(IUIAnimationStoryboard2 *iface, UI_ANIMATION_SECONDS *elapsedTime);
    HRESULT (WINAPI *SetStoryboardEventHandler)(IUIAnimationStoryboard2 *iface, IUnknown *handler, BOOL regNext, BOOL regPrio);
} IUIAnimationStoryboard2Vtbl;

struct IUIAnimationStoryboard2 { const IUIAnimationStoryboard2Vtbl *lpVtbl; };

typedef struct IUIAnimationManager2Vtbl
{
    HRESULT (WINAPI *QueryInterface)(IUIAnimationManager2 *iface, REFIID iid, void **obj);
    ULONG (WINAPI *AddRef)(IUIAnimationManager2 *iface);
    ULONG (WINAPI *Release)(IUIAnimationManager2 *iface);
    HRESULT (WINAPI *CreateAnimationVectorVariable)(IUIAnimationManager2 *iface, const DOUBLE *initialValue, UINT cDimension, IUIAnimationVariable2 **variable);
    HRESULT (WINAPI *CreateAnimationVariable)(IUIAnimationManager2 *iface, DOUBLE initialValue, IUIAnimationVariable2 **variable);
    HRESULT (WINAPI *ScheduleTransition)(IUIAnimationManager2 *iface, IUIAnimationVariable2 *variable, IUIAnimationTransition2 *transition, UI_ANIMATION_SECONDS timeNow);
    HRESULT (WINAPI *CreateStoryboard)(IUIAnimationManager2 *iface, IUIAnimationStoryboard2 **storyboard);
    HRESULT (WINAPI *FinishAllStoryboards)(IUIAnimationManager2 *iface, UI_ANIMATION_SECONDS completionDeadline);
    HRESULT (WINAPI *AbandonAllStoryboards)(IUIAnimationManager2 *iface);
    HRESULT (WINAPI *Update)(IUIAnimationManager2 *iface, UI_ANIMATION_SECONDS timeNow, UI_ANIMATION_UPDATE_RESULT *updateResult);
    HRESULT (WINAPI *GetVariableFromTag)(IUIAnimationManager2 *iface, IUnknown *object, UINT32 id, IUIAnimationVariable2 **variable);
    HRESULT (WINAPI *GetStoryboardFromTag)(IUIAnimationManager2 *iface, IUnknown *object, UINT32 id, IUIAnimationStoryboard2 **storyboard);
    HRESULT (WINAPI *EstimateNextEventTime)(IUIAnimationManager2 *iface, UI_ANIMATION_SECONDS *seconds);
    HRESULT (WINAPI *GetStatus)(IUIAnimationManager2 *iface, UI_ANIMATION_MANAGER_STATUS *status);
    HRESULT (WINAPI *SetAnimationMode)(IUIAnimationManager2 *iface, UI_ANIMATION_MODE mode);
    HRESULT (WINAPI *Pause)(IUIAnimationManager2 *iface);
    HRESULT (WINAPI *Resume)(IUIAnimationManager2 *iface);
    HRESULT (WINAPI *SetManagerEventHandler)(IUIAnimationManager2 *iface, IUnknown *handler, BOOL reg);
    HRESULT (WINAPI *SetCancelPriorityComparison)(IUIAnimationManager2 *iface, IUnknown *comparison);
    HRESULT (WINAPI *SetTrimPriorityComparison)(IUIAnimationManager2 *iface, IUnknown *comparison);
    HRESULT (WINAPI *SetCompressPriorityComparison)(IUIAnimationManager2 *iface, IUnknown *comparison);
    HRESULT (WINAPI *SetConcludePriorityComparison)(IUIAnimationManager2 *iface, IUnknown *comparison);
    HRESULT (WINAPI *SetDefaultLongestAcceptableDelay)(IUIAnimationManager2 *iface, UI_ANIMATION_SECONDS delay);
    HRESULT (WINAPI *Shutdown)(IUIAnimationManager2 *iface);
} IUIAnimationManager2Vtbl;

struct IUIAnimationManager2 { const IUIAnimationManager2Vtbl *lpVtbl; };

typedef struct IUIAnimationTransitionLibrary2Vtbl
{
    HRESULT (WINAPI *QueryInterface)(IUIAnimationTransitionLibrary2 *iface, REFIID iid, void **obj);
    ULONG (WINAPI *AddRef)(IUIAnimationTransitionLibrary2 *iface);
    ULONG (WINAPI *Release)(IUIAnimationTransitionLibrary2 *iface);
    HRESULT (WINAPI *CreateInstantaneousTransition)(IUIAnimationTransitionLibrary2 *iface, DOUBLE finalValue, IUIAnimationTransition2 **transition);
    HRESULT (WINAPI *CreateInstantaneousVectorTransition)(IUIAnimationTransitionLibrary2 *iface, const DOUBLE *finalValue, UINT cDimension, IUIAnimationTransition2 **transition);
    HRESULT (WINAPI *CreateConstantTransition)(IUIAnimationTransitionLibrary2 *iface, UI_ANIMATION_SECONDS duration, IUIAnimationTransition2 **transition);
    HRESULT (WINAPI *CreateDiscreteTransition)(IUIAnimationTransitionLibrary2 *iface, UI_ANIMATION_SECONDS delay, DOUBLE finalValue, UI_ANIMATION_SECONDS hold, IUIAnimationTransition2 **transition);
    HRESULT (WINAPI *CreateDiscreteVectorTransition)(IUIAnimationTransitionLibrary2 *iface, UI_ANIMATION_SECONDS delay, const DOUBLE *finalValue, UINT cDimension, UI_ANIMATION_SECONDS hold, IUIAnimationTransition2 **transition);
    HRESULT (WINAPI *CreateLinearTransition)(IUIAnimationTransitionLibrary2 *iface, UI_ANIMATION_SECONDS duration, DOUBLE finalValue, IUIAnimationTransition2 **transition);
    HRESULT (WINAPI *CreateLinearVectorTransition)(IUIAnimationTransitionLibrary2 *iface, UI_ANIMATION_SECONDS duration, const DOUBLE *finalValue, UINT cDimension, IUIAnimationTransition2 **transition);
    HRESULT (WINAPI *CreateLinearTransitionFromSpeed)(IUIAnimationTransitionLibrary2 *iface, DOUBLE speed, DOUBLE finalValue, IUIAnimationTransition2 **transition);
    HRESULT (WINAPI *CreateLinearVectorTransitionFromSpeed)(IUIAnimationTransitionLibrary2 *iface, DOUBLE speed, const DOUBLE *finalValue, UINT cDimension, IUIAnimationTransition2 **transition);
    HRESULT (WINAPI *CreateSinusoidalTransitionFromVelocity)(IUIAnimationTransitionLibrary2 *iface, UI_ANIMATION_SECONDS duration, UI_ANIMATION_SECONDS period, IUIAnimationTransition2 **transition);
    HRESULT (WINAPI *CreateSinusoidalTransitionFromRange)(IUIAnimationTransitionLibrary2 *iface, UI_ANIMATION_SECONDS duration, DOUBLE minimumValue, DOUBLE maximumValue, UI_ANIMATION_SECONDS period, UI_ANIMATION_SLOPE slope, IUIAnimationTransition2 **transition);
    HRESULT (WINAPI *CreateAccelerateDecelerateTransition)(IUIAnimationTransitionLibrary2 *iface, UI_ANIMATION_SECONDS duration, DOUBLE finalValue, DOUBLE accelerationRatio, DOUBLE decelerationRatio, IUIAnimationTransition2 **transition);
    HRESULT (WINAPI *CreateReversalTransition)(IUIAnimationTransitionLibrary2 *iface, UI_ANIMATION_SECONDS duration, IUIAnimationTransition2 **transition);
    HRESULT (WINAPI *CreateCubicTransition)(IUIAnimationTransitionLibrary2 *iface, UI_ANIMATION_SECONDS duration, DOUBLE finalValue, DOUBLE finalVelocity, IUIAnimationTransition2 **transition);
    HRESULT (WINAPI *CreateCubicVectorTransition)(IUIAnimationTransitionLibrary2 *iface, UI_ANIMATION_SECONDS duration, const DOUBLE *finalValue, const DOUBLE *finalVelocity, UINT cDimension, IUIAnimationTransition2 **transition);
    HRESULT (WINAPI *CreateSmoothStopTransition)(IUIAnimationTransitionLibrary2 *iface, UI_ANIMATION_SECONDS maximumDuration, DOUBLE finalValue, IUIAnimationTransition2 **transition);
    HRESULT (WINAPI *CreateParabolicTransitionFromAcceleration)(IUIAnimationTransitionLibrary2 *iface, DOUBLE finalValue, DOUBLE finalVelocity, DOUBLE acceleration, IUIAnimationTransition2 **transition);
    HRESULT (WINAPI *CreateCubicBezierLinearTransition)(IUIAnimationTransitionLibrary2 *iface, UI_ANIMATION_SECONDS duration, DOUBLE finalValue, DOUBLE x1, DOUBLE y1, DOUBLE x2, DOUBLE y2, IUIAnimationTransition2 **transition);
    HRESULT (WINAPI *CreateCubicBezierLinearVectorTransition)(IUIAnimationTransitionLibrary2 *iface, UI_ANIMATION_SECONDS duration, const DOUBLE *finalValue, UINT cDimension, DOUBLE x1, DOUBLE y1, DOUBLE x2, DOUBLE y2, IUIAnimationTransition2 **transition);
} IUIAnimationTransitionLibrary2Vtbl;

struct IUIAnimationTransitionLibrary2 { const IUIAnimationTransitionLibrary2Vtbl *lpVtbl; };

struct anim2_transition
{
    IUIAnimationTransition2 iface;
    LONG ref;
    DOUBLE duration;
    DOUBLE final_value;
    DOUBLE initial_value;
    DOUBLE initial_velocity;
    BOOL has_final;
};

struct anim2_variable
{
    IUIAnimationVariable2 iface;
    LONG ref;
    DOUBLE value;
    DOUBLE prev_value;
    DOUBLE final_value;
    IUnknown *tag_obj;
    UINT32 tag_id;
    IUnknown *change_handler;
    struct anim2_storyboard *current_sb;
};

struct anim2_sb_entry
{
    struct anim2_variable *var;
    struct anim2_transition *tr;
    DOUBLE start_value;
};

struct anim2_storyboard
{
    IUIAnimationStoryboard2 iface;
    LONG ref;
    struct anim2_manager *mgr;
    UI_ANIMATION_STORYBOARD_STATUS status;
    DOUBLE start_time;
    DOUBLE last_time;
    IUnknown *tag_obj;
    UINT32 tag_id;
    IUnknown *event_handler;
    struct anim2_sb_entry *entries;
    UINT entry_count;
    UINT entry_cap;
};

struct anim2_manager
{
    IUIAnimationManager2 iface;
    LONG ref;
    struct anim2_storyboard **storyboards;
    UINT sb_count;
    UINT sb_cap;
};

static inline struct anim2_transition *impl_from_transition2(IUIAnimationTransition2 *iface)
{
    return CONTAINING_RECORD(iface, struct anim2_transition, iface);
}

static HRESULT WINAPI tr2_QueryInterface(IUIAnimationTransition2 *iface, REFIID iid, void **obj)
{
    if (IsEqualIID(iid, &IID_IUnknown) || IsEqualIID(iid, &IID_IUIAnimationTransition2))
    {
        iface->lpVtbl->AddRef(iface);
        *obj = iface;
        return S_OK;
    }
    *obj = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI tr2_AddRef(IUIAnimationTransition2 *iface)
{
    return InterlockedIncrement(&impl_from_transition2(iface)->ref);
}

static ULONG WINAPI tr2_Release(IUIAnimationTransition2 *iface)
{
    struct anim2_transition *This = impl_from_transition2(iface);
    ULONG ref = InterlockedDecrement(&This->ref);
    if (!ref) free(This);
    return ref;
}

static HRESULT WINAPI tr2_GetDimension(IUIAnimationTransition2 *iface, UINT *dim)
{
    if (!dim) return E_POINTER;
    *dim = 1;
    return S_OK;
}

static HRESULT WINAPI tr2_SetInitialValue(IUIAnimationTransition2 *iface, DOUBLE val)
{
    impl_from_transition2(iface)->initial_value = val;
    return S_OK;
}

static HRESULT WINAPI tr2_SetInitialVectorValue(IUIAnimationTransition2 *iface, const DOUBLE *val, UINT dim)
{
    if (val && dim) impl_from_transition2(iface)->initial_value = val[0];
    return S_OK;
}

static HRESULT WINAPI tr2_SetInitialVelocity(IUIAnimationTransition2 *iface, DOUBLE vel)
{
    impl_from_transition2(iface)->initial_velocity = vel;
    return S_OK;
}

static HRESULT WINAPI tr2_SetInitialVectorVelocity(IUIAnimationTransition2 *iface, const DOUBLE *vel, UINT dim)
{
    if (vel && dim) impl_from_transition2(iface)->initial_velocity = vel[0];
    return S_OK;
}

static HRESULT WINAPI tr2_IsDurationKnown(IUIAnimationTransition2 *iface)
{
    return S_OK;
}

static HRESULT WINAPI tr2_GetDuration(IUIAnimationTransition2 *iface, UI_ANIMATION_SECONDS *dur)
{
    if (!dur) return E_POINTER;
    *dur = impl_from_transition2(iface)->duration;
    return S_OK;
}

static const IUIAnimationTransition2Vtbl tr2_vtbl =
{
    tr2_QueryInterface,
    tr2_AddRef,
    tr2_Release,
    tr2_GetDimension,
    tr2_SetInitialValue,
    tr2_SetInitialVectorValue,
    tr2_SetInitialVelocity,
    tr2_SetInitialVectorVelocity,
    tr2_IsDurationKnown,
    tr2_GetDuration,
};

static HRESULT create_tr2(DOUBLE duration, DOUBLE final_value, BOOL has_final, IUIAnimationTransition2 **out)
{
    struct anim2_transition *t;
    TRACE("(dur=%f, final=%f, has_final=%d, out=%p)\n", duration, final_value, has_final, out);
    if (!out) return E_POINTER;
    t = calloc(1, sizeof(*t));
    if (!t) return E_OUTOFMEMORY;
    t->iface.lpVtbl = &tr2_vtbl;
    t->ref = 1;
    t->duration = duration >= 0.0 ? duration : 0.0;
    t->final_value = final_value;
    t->has_final = has_final;
    *out = &t->iface;
    return S_OK;
}

static inline struct anim2_variable *impl_from_variable2(IUIAnimationVariable2 *iface)
{
    return CONTAINING_RECORD(iface, struct anim2_variable, iface);
}

static HRESULT WINAPI var2_QueryInterface(IUIAnimationVariable2 *iface, REFIID iid, void **obj)
{
    if (IsEqualIID(iid, &IID_IUnknown) || IsEqualIID(iid, &IID_IUIAnimationVariable2))
    {
        iface->lpVtbl->AddRef(iface);
        *obj = iface;
        return S_OK;
    }
    *obj = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI var2_AddRef(IUIAnimationVariable2 *iface)
{
    return InterlockedIncrement(&impl_from_variable2(iface)->ref);
}

static ULONG WINAPI var2_Release(IUIAnimationVariable2 *iface)
{
    struct anim2_variable *This = impl_from_variable2(iface);
    ULONG ref = InterlockedDecrement(&This->ref);
    if (!ref)
    {
        if (This->tag_obj) IUnknown_Release(This->tag_obj);
        if (This->change_handler) IUnknown_Release(This->change_handler);
        free(This);
    }
    return ref;
}

static HRESULT WINAPI var2_GetDimension(IUIAnimationVariable2 *iface, UINT *dim) { if (dim) *dim = 1; return S_OK; }
static HRESULT WINAPI var2_GetValue(IUIAnimationVariable2 *iface, DOUBLE *v) { if (v) *v = impl_from_variable2(iface)->value; return S_OK; }
static HRESULT WINAPI var2_GetFinalValue(IUIAnimationVariable2 *iface, DOUBLE *v) { if (v) *v = impl_from_variable2(iface)->final_value; return S_OK; }
static HRESULT WINAPI var2_GetPreviousValue(IUIAnimationVariable2 *iface, DOUBLE *v) { if (v) *v = impl_from_variable2(iface)->prev_value; return S_OK; }
static HRESULT WINAPI var2_GetIntegerValue(IUIAnimationVariable2 *iface, INT32 *v) { if (v) *v = (INT32)impl_from_variable2(iface)->value; return S_OK; }
static HRESULT WINAPI var2_GetFinalIntegerValue(IUIAnimationVariable2 *iface, INT32 *v) { if (v) *v = (INT32)impl_from_variable2(iface)->final_value; return S_OK; }
static HRESULT WINAPI var2_GetPreviousIntegerValue(IUIAnimationVariable2 *iface, INT32 *v) { if (v) *v = (INT32)impl_from_variable2(iface)->prev_value; return S_OK; }
static HRESULT WINAPI var2_GetVectorValue(IUIAnimationVariable2 *iface, DOUBLE *v, UINT d) { if (v && d) v[0] = impl_from_variable2(iface)->value; return S_OK; }
static HRESULT WINAPI var2_GetFinalVectorValue(IUIAnimationVariable2 *iface, DOUBLE *v, UINT d) { if (v && d) v[0] = impl_from_variable2(iface)->final_value; return S_OK; }
static HRESULT WINAPI var2_GetPreviousVectorValue(IUIAnimationVariable2 *iface, DOUBLE *v, UINT d) { if (v && d) v[0] = impl_from_variable2(iface)->prev_value; return S_OK; }
static HRESULT WINAPI var2_GetIntegerVectorValue(IUIAnimationVariable2 *iface, INT32 *v, UINT d) { if (v && d) v[0] = (INT32)impl_from_variable2(iface)->value; return S_OK; }
static HRESULT WINAPI var2_GetFinalIntegerVectorValue(IUIAnimationVariable2 *iface, INT32 *v, UINT d) { if (v && d) v[0] = (INT32)impl_from_variable2(iface)->final_value; return S_OK; }
static HRESULT WINAPI var2_GetPreviousIntegerVectorValue(IUIAnimationVariable2 *iface, INT32 *v, UINT d) { if (v && d) v[0] = (INT32)impl_from_variable2(iface)->prev_value; return S_OK; }
static HRESULT WINAPI var2_GetCurve(IUIAnimationVariable2 *iface, IUnknown *a) { return E_NOTIMPL; }
static HRESULT WINAPI var2_GetVectorCurve(IUIAnimationVariable2 *iface, IUnknown **a, UINT d) { return E_NOTIMPL; }
static HRESULT WINAPI var2_GetCurrentStoryboard(IUIAnimationVariable2 *iface, IUIAnimationStoryboard2 **sb)
{
    struct anim2_variable *This = impl_from_variable2(iface);
    if (!sb) return E_POINTER;
    if (This->current_sb)
    {
        This->current_sb->iface.lpVtbl->AddRef(&This->current_sb->iface);
        *sb = &This->current_sb->iface;
    }
    else *sb = NULL;
    return S_OK;
}
static HRESULT WINAPI var2_SetLowerBound(IUIAnimationVariable2 *iface, DOUBLE b) { return S_OK; }
static HRESULT WINAPI var2_SetUpperBound(IUIAnimationVariable2 *iface, DOUBLE b) { return S_OK; }
static HRESULT WINAPI var2_SetLowerBoundVector(IUIAnimationVariable2 *iface, const DOUBLE *b, UINT d) { return S_OK; }
static HRESULT WINAPI var2_SetUpperBoundVector(IUIAnimationVariable2 *iface, const DOUBLE *b, UINT d) { return S_OK; }
static HRESULT WINAPI var2_SetRoundingMode(IUIAnimationVariable2 *iface, UI_ANIMATION_ROUNDING_MODE m) { return S_OK; }
static HRESULT WINAPI var2_SetTag(IUIAnimationVariable2 *iface, IUnknown *obj, UINT32 id)
{
    struct anim2_variable *This = impl_from_variable2(iface);
    if (This->tag_obj) IUnknown_Release(This->tag_obj);
    This->tag_obj = obj;
    if (obj) IUnknown_AddRef(obj);
    This->tag_id = id;
    return S_OK;
}
static HRESULT WINAPI var2_GetTag(IUIAnimationVariable2 *iface, IUnknown **obj, UINT32 *id)
{
    struct anim2_variable *This = impl_from_variable2(iface);
    if (obj)
    {
        *obj = This->tag_obj;
        if (This->tag_obj) IUnknown_AddRef(This->tag_obj);
    }
    if (id) *id = This->tag_id;
    return S_OK;
}
static HRESULT WINAPI var2_SetVariableChangeHandler(IUIAnimationVariable2 *iface, IUnknown *handler, BOOL reg)
{
    struct anim2_variable *This = impl_from_variable2(iface);
    if (This->change_handler) IUnknown_Release(This->change_handler);
    This->change_handler = handler;
    if (handler) IUnknown_AddRef(handler);
    return S_OK;
}
static HRESULT WINAPI var2_SetVariableIntegerChangeHandler(IUIAnimationVariable2 *iface, IUnknown *handler, BOOL reg) { return S_OK; }
static HRESULT WINAPI var2_SetVariableCurveChangeHandler(IUIAnimationVariable2 *iface, IUnknown *handler) { return S_OK; }

static const IUIAnimationVariable2Vtbl var2_vtbl =
{
    var2_QueryInterface,
    var2_AddRef,
    var2_Release,
    var2_GetDimension,
    var2_GetValue,
    var2_GetFinalValue,
    var2_GetPreviousValue,
    var2_GetIntegerValue,
    var2_GetFinalIntegerValue,
    var2_GetPreviousIntegerValue,
    var2_GetVectorValue,
    var2_GetFinalVectorValue,
    var2_GetPreviousVectorValue,
    var2_GetIntegerVectorValue,
    var2_GetFinalIntegerVectorValue,
    var2_GetPreviousIntegerVectorValue,
    var2_GetCurve,
    var2_GetVectorCurve,
    var2_GetCurrentStoryboard,
    var2_SetLowerBound,
    var2_SetUpperBound,
    var2_SetLowerBoundVector,
    var2_SetUpperBoundVector,
    var2_SetRoundingMode,
    var2_SetTag,
    var2_GetTag,
    var2_SetVariableChangeHandler,
    var2_SetVariableIntegerChangeHandler,
    var2_SetVariableCurveChangeHandler,
};

static inline struct anim2_storyboard *impl_from_storyboard2(IUIAnimationStoryboard2 *iface)
{
    return CONTAINING_RECORD(iface, struct anim2_storyboard, iface);
}

static HRESULT WINAPI sb2_QueryInterface(IUIAnimationStoryboard2 *iface, REFIID iid, void **obj)
{
    if (IsEqualIID(iid, &IID_IUnknown) || IsEqualIID(iid, &IID_IUIAnimationStoryboard2))
    {
        iface->lpVtbl->AddRef(iface);
        *obj = iface;
        return S_OK;
    }
    *obj = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI sb2_AddRef(IUIAnimationStoryboard2 *iface)
{
    return InterlockedIncrement(&impl_from_storyboard2(iface)->ref);
}

static ULONG WINAPI sb2_Release(IUIAnimationStoryboard2 *iface)
{
    struct anim2_storyboard *This = impl_from_storyboard2(iface);
    ULONG ref = InterlockedDecrement(&This->ref);
    if (!ref)
    {
        UINT i;
        for (i = 0; i < This->entry_count; i++)
        {
            if (This->entries[i].var->current_sb == This)
                This->entries[i].var->current_sb = NULL;
            This->entries[i].var->iface.lpVtbl->Release(&This->entries[i].var->iface);
            This->entries[i].tr->iface.lpVtbl->Release(&This->entries[i].tr->iface);
        }
        free(This->entries);
        if (This->tag_obj) IUnknown_Release(This->tag_obj);
        if (This->event_handler) IUnknown_Release(This->event_handler);
        free(This);
    }
    return ref;
}

static HRESULT WINAPI sb2_AddTransition(IUIAnimationStoryboard2 *iface,
        IUIAnimationVariable2 *variable, IUIAnimationTransition2 *transition)
{
    struct anim2_storyboard *This = impl_from_storyboard2(iface);
    struct anim2_variable *var;
    struct anim2_transition *tr;
    TRACE("(%p)->(%p, %p)\n", iface, variable, transition);
    if (!variable || !transition) return E_POINTER;
    var = impl_from_variable2(variable);
    tr = impl_from_transition2(transition);
    if (This->entry_count == This->entry_cap)
    {
        UINT new_cap = This->entry_cap ? This->entry_cap * 2 : 8;
        struct anim2_sb_entry *nb = realloc(This->entries, new_cap * sizeof(*nb));
        if (!nb) return E_OUTOFMEMORY;
        This->entries = nb;
        This->entry_cap = new_cap;
    }
    variable->lpVtbl->AddRef(variable);
    transition->lpVtbl->AddRef(transition);
    This->entries[This->entry_count].var = var;
    This->entries[This->entry_count].tr = tr;
    This->entries[This->entry_count].start_value = var->value;
    if (tr->has_final)
        var->final_value = tr->final_value;
    This->entry_count++;
    return S_OK;
}

static HRESULT WINAPI sb2_AddKeyframeAtOffset(IUIAnimationStoryboard2 *iface, UI_ANIMATION_KEYFRAME ex, UI_ANIMATION_SECONDS off, UI_ANIMATION_KEYFRAME *kf)
{
    if (kf) *kf = (UI_ANIMATION_KEYFRAME)1;
    return S_OK;
}
static HRESULT WINAPI sb2_AddKeyframeAfterTransition(IUIAnimationStoryboard2 *iface, IUIAnimationTransition2 *tr, UI_ANIMATION_KEYFRAME *kf)
{
    if (kf) *kf = (UI_ANIMATION_KEYFRAME)1;
    return S_OK;
}
static HRESULT WINAPI sb2_AddTransitionAtKeyframe(IUIAnimationStoryboard2 *iface, IUIAnimationVariable2 *v, IUIAnimationTransition2 *t, UI_ANIMATION_KEYFRAME kf)
{
    return sb2_AddTransition(iface, v, t);
}
static HRESULT WINAPI sb2_AddTransitionBetweenKeyframes(IUIAnimationStoryboard2 *iface, IUIAnimationVariable2 *v, IUIAnimationTransition2 *t, UI_ANIMATION_KEYFRAME k1, UI_ANIMATION_KEYFRAME k2)
{
    return sb2_AddTransition(iface, v, t);
}
static HRESULT WINAPI sb2_RepeatBetweenKeyframes(IUIAnimationStoryboard2 *iface, UI_ANIMATION_KEYFRAME k1, UI_ANIMATION_KEYFRAME k2, DOUBLE c, INT m, IUnknown *h, UINT_PTR id, BOOL r) { return S_OK; }
static HRESULT WINAPI sb2_HoldVariable(IUIAnimationStoryboard2 *iface, IUIAnimationVariable2 *v) { return S_OK; }
static HRESULT WINAPI sb2_SetLongestAcceptableDelay(IUIAnimationStoryboard2 *iface, UI_ANIMATION_SECONDS d) { return S_OK; }
static HRESULT WINAPI sb2_SetSkipDuration(IUIAnimationStoryboard2 *iface, UI_ANIMATION_SECONDS d) { return S_OK; }

static HRESULT WINAPI sb2_Schedule(IUIAnimationStoryboard2 *iface, UI_ANIMATION_SECONDS timeNow, UI_ANIMATION_SCHEDULING_RESULT *res)
{
    struct anim2_storyboard *This = impl_from_storyboard2(iface);
    UI_ANIMATION_STORYBOARD_STATUS prev = This->status;
    UINT i;
    TRACE("(%p)->(%f, %p) entries=%u\n", iface, timeNow, res, This->entry_count);
    This->start_time = timeNow;
    This->last_time = timeNow;
    This->status = UI_ANIMATION_STORYBOARD_PLAYING;
    for (i = 0; i < This->entry_count; i++)
    {
        This->entries[i].start_value = This->entries[i].var->value;
        This->entries[i].var->current_sb = This;
    }
    if (res) *res = UI_ANIMATION_SCHEDULING_SUCCEEDED;
    if (This->event_handler)
    {
        const IUIAnimationStoryboardEventHandler2Vtbl *vtbl = (const IUIAnimationStoryboardEventHandler2Vtbl *)This->event_handler->lpVtbl;
        vtbl->OnStoryboardStatusChanged(This->event_handler, iface, This->status, prev);
    }
    return S_OK;
}

static HRESULT WINAPI sb2_Conclude(IUIAnimationStoryboard2 *iface)
{
    impl_from_storyboard2(iface)->status = UI_ANIMATION_STORYBOARD_FINISHED;
    return S_OK;
}
static HRESULT WINAPI sb2_Finish(IUIAnimationStoryboard2 *iface, UI_ANIMATION_SECONDS d)
{
    impl_from_storyboard2(iface)->status = UI_ANIMATION_STORYBOARD_FINISHED;
    return S_OK;
}
static HRESULT WINAPI sb2_Abandon(IUIAnimationStoryboard2 *iface)
{
    struct anim2_storyboard *This = impl_from_storyboard2(iface);
    UI_ANIMATION_STORYBOARD_STATUS prev = This->status;
    This->status = UI_ANIMATION_STORYBOARD_CANCELLED;
    if (This->event_handler)
    {
        const IUIAnimationStoryboardEventHandler2Vtbl *vtbl = (const IUIAnimationStoryboardEventHandler2Vtbl *)This->event_handler->lpVtbl;
        vtbl->OnStoryboardStatusChanged(This->event_handler, iface, This->status, prev);
    }
    return S_OK;
}
static HRESULT WINAPI sb2_SetTag(IUIAnimationStoryboard2 *iface, IUnknown *obj, UINT32 id)
{
    struct anim2_storyboard *This = impl_from_storyboard2(iface);
    if (This->tag_obj) IUnknown_Release(This->tag_obj);
    This->tag_obj = obj;
    if (obj) IUnknown_AddRef(obj);
    This->tag_id = id;
    return S_OK;
}
static HRESULT WINAPI sb2_GetTag(IUIAnimationStoryboard2 *iface, IUnknown **obj, UINT32 *id)
{
    struct anim2_storyboard *This = impl_from_storyboard2(iface);
    if (obj)
    {
        *obj = This->tag_obj;
        if (This->tag_obj) IUnknown_AddRef(This->tag_obj);
    }
    if (id) *id = This->tag_id;
    return S_OK;
}
static HRESULT WINAPI sb2_GetStatus(IUIAnimationStoryboard2 *iface, UI_ANIMATION_STORYBOARD_STATUS *st)
{
    if (st) *st = impl_from_storyboard2(iface)->status;
    return S_OK;
}
static HRESULT WINAPI sb2_GetElapsedTime(IUIAnimationStoryboard2 *iface, UI_ANIMATION_SECONDS *t)
{
    struct anim2_storyboard *This = impl_from_storyboard2(iface);
    if (t) *t = This->last_time - This->start_time;
    return S_OK;
}
static HRESULT WINAPI sb2_SetStoryboardEventHandler(IUIAnimationStoryboard2 *iface, IUnknown *handler, BOOL r1, BOOL r2)
{
    struct anim2_storyboard *This = impl_from_storyboard2(iface);
    if (This->event_handler) IUnknown_Release(This->event_handler);
    This->event_handler = handler;
    if (handler) IUnknown_AddRef(handler);
    return S_OK;
}

static const IUIAnimationStoryboard2Vtbl sb2_vtbl =
{
    sb2_QueryInterface,
    sb2_AddRef,
    sb2_Release,
    sb2_AddTransition,
    sb2_AddKeyframeAtOffset,
    sb2_AddKeyframeAfterTransition,
    sb2_AddTransitionAtKeyframe,
    sb2_AddTransitionBetweenKeyframes,
    sb2_RepeatBetweenKeyframes,
    sb2_HoldVariable,
    sb2_SetLongestAcceptableDelay,
    sb2_SetSkipDuration,
    sb2_Schedule,
    sb2_Conclude,
    sb2_Finish,
    sb2_Abandon,
    sb2_SetTag,
    sb2_GetTag,
    sb2_GetStatus,
    sb2_GetElapsedTime,
    sb2_SetStoryboardEventHandler,
};

static inline struct anim2_manager *impl_from_manager2(IUIAnimationManager2 *iface)
{
    return CONTAINING_RECORD(iface, struct anim2_manager, iface);
}

static HRESULT WINAPI mgr2_QueryInterface(IUIAnimationManager2 *iface, REFIID iid, void **obj)
{
    if (IsEqualIID(iid, &IID_IUnknown) || IsEqualIID(iid, &IID_IUIAnimationManager2))
    {
        iface->lpVtbl->AddRef(iface);
        *obj = iface;
        return S_OK;
    }
    *obj = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI mgr2_AddRef(IUIAnimationManager2 *iface)
{
    return InterlockedIncrement(&impl_from_manager2(iface)->ref);
}

static ULONG WINAPI mgr2_Release(IUIAnimationManager2 *iface)
{
    struct anim2_manager *This = impl_from_manager2(iface);
    ULONG ref = InterlockedDecrement(&This->ref);
    if (!ref)
    {
        UINT i;
        for (i = 0; i < This->sb_count; i++)
            This->storyboards[i]->iface.lpVtbl->Release(&This->storyboards[i]->iface);
        free(This->storyboards);
        free(This);
    }
    return ref;
}

static HRESULT WINAPI mgr2_CreateAnimationVariable(IUIAnimationManager2 *iface, DOUBLE initialValue, IUIAnimationVariable2 **variable)
{
    struct anim2_variable *v;
    if (!variable) return E_POINTER;
    v = calloc(1, sizeof(*v));
    if (!v) return E_OUTOFMEMORY;
    v->iface.lpVtbl = &var2_vtbl;
    v->ref = 1;
    v->value = initialValue;
    v->prev_value = initialValue;
    v->final_value = initialValue;
    *variable = &v->iface;
    return S_OK;
}

static HRESULT WINAPI mgr2_CreateAnimationVectorVariable(IUIAnimationManager2 *iface, const DOUBLE *initialValue, UINT cDimension, IUIAnimationVariable2 **variable)
{
    return mgr2_CreateAnimationVariable(iface, (initialValue && cDimension) ? initialValue[0] : 0.0, variable);
}

static HRESULT WINAPI mgr2_CreateStoryboard(IUIAnimationManager2 *iface, IUIAnimationStoryboard2 **storyboard)
{
    struct anim2_manager *This = impl_from_manager2(iface);
    struct anim2_storyboard *sb;
    if (!storyboard) return E_POINTER;
    sb = calloc(1, sizeof(*sb));
    if (!sb) return E_OUTOFMEMORY;
    sb->iface.lpVtbl = &sb2_vtbl;
    sb->ref = 1;
    sb->mgr = This;
    sb->status = UI_ANIMATION_STORYBOARD_BUILDING;
    if (This->sb_count == This->sb_cap)
    {
        UINT new_cap = This->sb_cap ? This->sb_cap * 2 : 8;
        struct anim2_storyboard **nb = realloc(This->storyboards, new_cap * sizeof(*nb));
        if (!nb) { free(sb); return E_OUTOFMEMORY; }
        This->storyboards = nb;
        This->sb_cap = new_cap;
    }
    sb->iface.lpVtbl->AddRef(&sb->iface);
    This->storyboards[This->sb_count++] = sb;
    *storyboard = &sb->iface;
    return S_OK;
}

static HRESULT WINAPI mgr2_ScheduleTransition(IUIAnimationManager2 *iface, IUIAnimationVariable2 *variable, IUIAnimationTransition2 *transition, UI_ANIMATION_SECONDS timeNow)
{
    IUIAnimationStoryboard2 *sb;
    HRESULT hr = mgr2_CreateStoryboard(iface, &sb);
    if (FAILED(hr)) return hr;
    hr = sb->lpVtbl->AddTransition(sb, variable, transition);
    if (SUCCEEDED(hr)) hr = sb->lpVtbl->Schedule(sb, timeNow, NULL);
    sb->lpVtbl->Release(sb);
    return hr;
}

static HRESULT WINAPI mgr2_FinishAllStoryboards(IUIAnimationManager2 *iface, UI_ANIMATION_SECONDS d)
{
    struct anim2_manager *This = impl_from_manager2(iface);
    UINT i;
    for (i = 0; i < This->sb_count; i++)
        This->storyboards[i]->status = UI_ANIMATION_STORYBOARD_FINISHED;
    return S_OK;
}

static HRESULT WINAPI mgr2_AbandonAllStoryboards(IUIAnimationManager2 *iface)
{
    struct anim2_manager *This = impl_from_manager2(iface);
    UINT i;
    for (i = 0; i < This->sb_count; i++)
        This->storyboards[i]->iface.lpVtbl->Abandon(&This->storyboards[i]->iface);
    return S_OK;
}

static HRESULT WINAPI mgr2_Update(IUIAnimationManager2 *iface, UI_ANIMATION_SECONDS timeNow, UI_ANIMATION_UPDATE_RESULT *updateResult)
{
    struct anim2_manager *This = impl_from_manager2(iface);
    BOOL any_changed = FALSE;
    UINT i, j;

    TRACE("(%p)->(%f, %p) sb_count=%u\n", iface, timeNow, updateResult, This->sb_count);

    for (i = 0; i < This->sb_count; i++)
    {
        struct anim2_storyboard *sb = This->storyboards[i];
        BOOL all_done = TRUE;
        BOOL sb_changed = FALSE;
        DOUBLE elapsed;

        if (sb->status != UI_ANIMATION_STORYBOARD_PLAYING)
            continue;

        sb->last_time = timeNow;
        elapsed = timeNow - sb->start_time;
        if (elapsed < 0.0) elapsed = 0.0;

        for (j = 0; j < sb->entry_count; j++)
        {
            struct anim2_variable *var = sb->entries[j].var;
            struct anim2_transition *tr = sb->entries[j].tr;
            DOUBLE prev = var->value;
            DOUBLE next = prev;

            if (tr->has_final)
            {
                if (tr->duration <= 0.0 || elapsed >= tr->duration)
                {
                    next = tr->final_value;
                }
                else
                {
                    DOUBLE t = elapsed / tr->duration;
                    next = sb->entries[j].start_value + (tr->final_value - sb->entries[j].start_value) * t;
                    all_done = FALSE;
                }
            }
            else if (elapsed < tr->duration)
            {
                all_done = FALSE;
            }

            if (next != prev)
            {
                var->prev_value = prev;
                var->value = next;
                sb_changed = TRUE;
                any_changed = TRUE;
                if (var->change_handler)
                {
                    const IUIAnimationVariableChangeHandler2Vtbl *vtbl =
                            (const IUIAnimationVariableChangeHandler2Vtbl *)var->change_handler->lpVtbl;
                    vtbl->OnValueChanged(var->change_handler, &sb->iface, &var->iface, &var->value, &var->prev_value, 1);
                }
            }
        }

        if (sb_changed && sb->event_handler)
        {
            const IUIAnimationStoryboardEventHandler2Vtbl *vtbl =
                    (const IUIAnimationStoryboardEventHandler2Vtbl *)sb->event_handler->lpVtbl;
            vtbl->OnStoryboardUpdated(sb->event_handler, &sb->iface);
        }

        if (all_done)
        {
            UI_ANIMATION_STORYBOARD_STATUS prev_st = sb->status;
            sb->status = UI_ANIMATION_STORYBOARD_FINISHED;
            if (sb->event_handler)
            {
                const IUIAnimationStoryboardEventHandler2Vtbl *vtbl =
                        (const IUIAnimationStoryboardEventHandler2Vtbl *)sb->event_handler->lpVtbl;
                vtbl->OnStoryboardStatusChanged(sb->event_handler, &sb->iface, sb->status, prev_st);
            }
        }
    }

    if (updateResult)
        *updateResult = any_changed ? UI_ANIMATION_UPDATE_VARIABLES_CHANGED : UI_ANIMATION_UPDATE_NO_CHANGE;
    return S_OK;
}

static HRESULT WINAPI mgr2_GetVariableFromTag(IUIAnimationManager2 *iface, IUnknown *o, UINT32 id, IUIAnimationVariable2 **v) { if (v) *v = NULL; return E_NOTIMPL; }
static HRESULT WINAPI mgr2_GetStoryboardFromTag(IUIAnimationManager2 *iface, IUnknown *o, UINT32 id, IUIAnimationStoryboard2 **s) { if (s) *s = NULL; return E_NOTIMPL; }
static HRESULT WINAPI mgr2_EstimateNextEventTime(IUIAnimationManager2 *iface, UI_ANIMATION_SECONDS *s) { if (s) *s = 0.0; return S_OK; }
static HRESULT WINAPI mgr2_GetStatus(IUIAnimationManager2 *iface, UI_ANIMATION_MANAGER_STATUS *st)
{
    struct anim2_manager *This = impl_from_manager2(iface);
    UINT i;
    if (!st) return E_POINTER;
    *st = UI_ANIMATION_MANAGER_IDLE;
    for (i = 0; i < This->sb_count; i++)
        if (This->storyboards[i]->status == UI_ANIMATION_STORYBOARD_PLAYING)
            *st = UI_ANIMATION_MANAGER_BUSY;
    return S_OK;
}
static HRESULT WINAPI mgr2_SetAnimationMode(IUIAnimationManager2 *iface, UI_ANIMATION_MODE m) { return S_OK; }
static HRESULT WINAPI mgr2_Pause(IUIAnimationManager2 *iface) { return S_OK; }
static HRESULT WINAPI mgr2_Resume(IUIAnimationManager2 *iface) { return S_OK; }
static HRESULT WINAPI mgr2_SetManagerEventHandler(IUIAnimationManager2 *iface, IUnknown *h, BOOL r) { return S_OK; }
static HRESULT WINAPI mgr2_SetCancelPriorityComparison(IUIAnimationManager2 *iface, IUnknown *c) { return S_OK; }
static HRESULT WINAPI mgr2_SetTrimPriorityComparison(IUIAnimationManager2 *iface, IUnknown *c) { return S_OK; }
static HRESULT WINAPI mgr2_SetCompressPriorityComparison(IUIAnimationManager2 *iface, IUnknown *c) { return S_OK; }
static HRESULT WINAPI mgr2_SetConcludePriorityComparison(IUIAnimationManager2 *iface, IUnknown *c) { return S_OK; }
static HRESULT WINAPI mgr2_SetDefaultLongestAcceptableDelay(IUIAnimationManager2 *iface, UI_ANIMATION_SECONDS d) { return S_OK; }
static HRESULT WINAPI mgr2_Shutdown(IUIAnimationManager2 *iface) { return mgr2_AbandonAllStoryboards(iface); }

static const IUIAnimationManager2Vtbl mgr2_vtbl =
{
    mgr2_QueryInterface,
    mgr2_AddRef,
    mgr2_Release,
    mgr2_CreateAnimationVectorVariable,
    mgr2_CreateAnimationVariable,
    mgr2_ScheduleTransition,
    mgr2_CreateStoryboard,
    mgr2_FinishAllStoryboards,
    mgr2_AbandonAllStoryboards,
    mgr2_Update,
    mgr2_GetVariableFromTag,
    mgr2_GetStoryboardFromTag,
    mgr2_EstimateNextEventTime,
    mgr2_GetStatus,
    mgr2_SetAnimationMode,
    mgr2_Pause,
    mgr2_Resume,
    mgr2_SetManagerEventHandler,
    mgr2_SetCancelPriorityComparison,
    mgr2_SetTrimPriorityComparison,
    mgr2_SetCompressPriorityComparison,
    mgr2_SetConcludePriorityComparison,
    mgr2_SetDefaultLongestAcceptableDelay,
    mgr2_Shutdown,
};

static HRESULT manager2_create(IUnknown *outer, REFIID iid, void **obj)
{
    struct anim2_manager *This = calloc(1, sizeof(*This));
    HRESULT hr;
    if (!This) return E_OUTOFMEMORY;
    This->iface.lpVtbl = &mgr2_vtbl;
    This->ref = 1;
    hr = This->iface.lpVtbl->QueryInterface(&This->iface, iid, obj);
    This->iface.lpVtbl->Release(&This->iface);
    return hr;
}

struct tr_library2
{
    IUIAnimationTransitionLibrary2 iface;
    LONG ref;
};

static inline struct tr_library2 *impl_from_library2(IUIAnimationTransitionLibrary2 *iface)
{
    return CONTAINING_RECORD(iface, struct tr_library2, iface);
}

static HRESULT WINAPI lib2_QueryInterface(IUIAnimationTransitionLibrary2 *iface, REFIID iid, void **obj)
{
    if (IsEqualIID(iid, &IID_IUnknown) || IsEqualIID(iid, &IID_IUIAnimationTransitionLibrary2))
    {
        iface->lpVtbl->AddRef(iface);
        *obj = iface;
        return S_OK;
    }
    *obj = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI lib2_AddRef(IUIAnimationTransitionLibrary2 *iface)
{
    return InterlockedIncrement(&impl_from_library2(iface)->ref);
}

static ULONG WINAPI lib2_Release(IUIAnimationTransitionLibrary2 *iface)
{
    struct tr_library2 *This = impl_from_library2(iface);
    ULONG ref = InterlockedDecrement(&This->ref);
    if (!ref) free(This);
    return ref;
}

static HRESULT WINAPI lib2_CreateInstantaneousTransition(IUIAnimationTransitionLibrary2 *iface, DOUBLE fv, IUIAnimationTransition2 **tr) { return create_tr2(0.0, fv, TRUE, tr); }
static HRESULT WINAPI lib2_CreateInstantaneousVectorTransition(IUIAnimationTransitionLibrary2 *iface, const DOUBLE *fv, UINT d, IUIAnimationTransition2 **tr) { return create_tr2(0.0, (fv && d) ? fv[0] : 0.0, TRUE, tr); }
static HRESULT WINAPI lib2_CreateConstantTransition(IUIAnimationTransitionLibrary2 *iface, UI_ANIMATION_SECONDS dur, IUIAnimationTransition2 **tr) { return create_tr2(dur, 0.0, FALSE, tr); }
static HRESULT WINAPI lib2_CreateDiscreteTransition(IUIAnimationTransitionLibrary2 *iface, UI_ANIMATION_SECONDS del, DOUBLE fv, UI_ANIMATION_SECONDS hold, IUIAnimationTransition2 **tr) { return create_tr2(del + hold, fv, TRUE, tr); }
static HRESULT WINAPI lib2_CreateDiscreteVectorTransition(IUIAnimationTransitionLibrary2 *iface, UI_ANIMATION_SECONDS del, const DOUBLE *fv, UINT d, UI_ANIMATION_SECONDS hold, IUIAnimationTransition2 **tr) { return create_tr2(del + hold, (fv && d) ? fv[0] : 0.0, TRUE, tr); }
static HRESULT WINAPI lib2_CreateLinearTransition(IUIAnimationTransitionLibrary2 *iface, UI_ANIMATION_SECONDS dur, DOUBLE fv, IUIAnimationTransition2 **tr) { return create_tr2(dur, fv, TRUE, tr); }
static HRESULT WINAPI lib2_CreateLinearVectorTransition(IUIAnimationTransitionLibrary2 *iface, UI_ANIMATION_SECONDS dur, const DOUBLE *fv, UINT d, IUIAnimationTransition2 **tr) { return create_tr2(dur, (fv && d) ? fv[0] : 0.0, TRUE, tr); }
static HRESULT WINAPI lib2_CreateLinearTransitionFromSpeed(IUIAnimationTransitionLibrary2 *iface, DOUBLE spd, DOUBLE fv, IUIAnimationTransition2 **tr) { return create_tr2(0.15, fv, TRUE, tr); }
static HRESULT WINAPI lib2_CreateLinearVectorTransitionFromSpeed(IUIAnimationTransitionLibrary2 *iface, DOUBLE spd, const DOUBLE *fv, UINT d, IUIAnimationTransition2 **tr) { return create_tr2(0.15, (fv && d) ? fv[0] : 0.0, TRUE, tr); }
static HRESULT WINAPI lib2_CreateSinusoidalTransitionFromVelocity(IUIAnimationTransitionLibrary2 *iface, UI_ANIMATION_SECONDS dur, UI_ANIMATION_SECONDS p, IUIAnimationTransition2 **tr) { return create_tr2(dur, 0.0, FALSE, tr); }
static HRESULT WINAPI lib2_CreateSinusoidalTransitionFromRange(IUIAnimationTransitionLibrary2 *iface, UI_ANIMATION_SECONDS dur, DOUBLE mn, DOUBLE mx, UI_ANIMATION_SECONDS p, UI_ANIMATION_SLOPE s, IUIAnimationTransition2 **tr) { return create_tr2(dur, mx, TRUE, tr); }
static HRESULT WINAPI lib2_CreateAccelerateDecelerateTransition(IUIAnimationTransitionLibrary2 *iface, UI_ANIMATION_SECONDS dur, DOUBLE fv, DOUBLE a, DOUBLE d, IUIAnimationTransition2 **tr) { return create_tr2(dur, fv, TRUE, tr); }
static HRESULT WINAPI lib2_CreateReversalTransition(IUIAnimationTransitionLibrary2 *iface, UI_ANIMATION_SECONDS dur, IUIAnimationTransition2 **tr) { return create_tr2(dur, 0.0, FALSE, tr); }
static HRESULT WINAPI lib2_CreateCubicTransition(IUIAnimationTransitionLibrary2 *iface, UI_ANIMATION_SECONDS dur, DOUBLE fv, DOUBLE fvel, IUIAnimationTransition2 **tr) { return create_tr2(dur, fv, TRUE, tr); }
static HRESULT WINAPI lib2_CreateCubicVectorTransition(IUIAnimationTransitionLibrary2 *iface, UI_ANIMATION_SECONDS dur, const DOUBLE *fv, const DOUBLE *fvel, UINT d, IUIAnimationTransition2 **tr) { return create_tr2(dur, (fv && d) ? fv[0] : 0.0, TRUE, tr); }
static HRESULT WINAPI lib2_CreateSmoothStopTransition(IUIAnimationTransitionLibrary2 *iface, UI_ANIMATION_SECONDS md, DOUBLE fv, IUIAnimationTransition2 **tr) { return create_tr2(md, fv, TRUE, tr); }
static HRESULT WINAPI lib2_CreateParabolicTransitionFromAcceleration(IUIAnimationTransitionLibrary2 *iface, DOUBLE fv, DOUBLE fvel, DOUBLE acc, IUIAnimationTransition2 **tr) { return create_tr2(0.15, fv, TRUE, tr); }
static HRESULT WINAPI lib2_CreateCubicBezierLinearTransition(IUIAnimationTransitionLibrary2 *iface, UI_ANIMATION_SECONDS dur, DOUBLE fv, DOUBLE x1, DOUBLE y1, DOUBLE x2, DOUBLE y2, IUIAnimationTransition2 **tr) { return create_tr2(dur, fv, TRUE, tr); }
static HRESULT WINAPI lib2_CreateCubicBezierLinearVectorTransition(IUIAnimationTransitionLibrary2 *iface, UI_ANIMATION_SECONDS dur, const DOUBLE *fv, UINT d, DOUBLE x1, DOUBLE y1, DOUBLE x2, DOUBLE y2, IUIAnimationTransition2 **tr) { return create_tr2(dur, (fv && d) ? fv[0] : 0.0, TRUE, tr); }

static const IUIAnimationTransitionLibrary2Vtbl lib2_vtbl =
{
    lib2_QueryInterface,
    lib2_AddRef,
    lib2_Release,
    lib2_CreateInstantaneousTransition,
    lib2_CreateInstantaneousVectorTransition,
    lib2_CreateConstantTransition,
    lib2_CreateDiscreteTransition,
    lib2_CreateDiscreteVectorTransition,
    lib2_CreateLinearTransition,
    lib2_CreateLinearVectorTransition,
    lib2_CreateLinearTransitionFromSpeed,
    lib2_CreateLinearVectorTransitionFromSpeed,
    lib2_CreateSinusoidalTransitionFromVelocity,
    lib2_CreateSinusoidalTransitionFromRange,
    lib2_CreateAccelerateDecelerateTransition,
    lib2_CreateReversalTransition,
    lib2_CreateCubicTransition,
    lib2_CreateCubicVectorTransition,
    lib2_CreateSmoothStopTransition,
    lib2_CreateParabolicTransitionFromAcceleration,
    lib2_CreateCubicBezierLinearTransition,
    lib2_CreateCubicBezierLinearVectorTransition,
};

static HRESULT library2_create(IUnknown *outer, REFIID iid, void **obj)
{
    struct tr_library2 *This = calloc(1, sizeof(*This));
    HRESULT hr;
    if (!This) return E_OUTOFMEMORY;
    This->iface.lpVtbl = &lib2_vtbl;
    This->ref = 1;
    hr = This->iface.lpVtbl->QueryInterface(&This->iface, iid, obj);
    This->iface.lpVtbl->Release(&This->iface);
    return hr;
}

static struct class_factory manager2_cf = { { &class_factory_vtbl }, manager2_create };
static struct class_factory library2_cf = { { &class_factory_vtbl }, library2_create };

/******************************************************************
 *             DllGetClassObject
 */
HRESULT WINAPI DllGetClassObject( REFCLSID clsid, REFIID iid, void **obj )
{
    IClassFactory *cf = NULL;

    TRACE( "(%s %s %p)\n", debugstr_guid( clsid ), debugstr_guid( iid ), obj );

    if (IsEqualCLSID( clsid, &CLSID_UIAnimationManager ))
        cf = &manager_cf.IClassFactory_iface;
    else if (IsEqualCLSID( clsid, &CLSID_UIAnimationTimer ))
        cf = &timer_cf.IClassFactory_iface;
    else if (IsEqualCLSID( clsid, &CLSID_UIAnimationTransitionFactory ))
        cf = &transition_cf.IClassFactory_iface;
    else if (IsEqualCLSID( clsid, &CLSID_UIAnimationTransitionLibrary ))
        cf = &library_cf.IClassFactory_iface;
    else if (IsEqualCLSID( clsid, &CLSID_UIAnimationManager2 ))
        cf = &manager2_cf.IClassFactory_iface;
    else if (IsEqualCLSID( clsid, &CLSID_UIAnimationTransitionLibrary2 ))
        cf = &library2_cf.IClassFactory_iface;

    if (!cf)
        return CLASS_E_CLASSNOTAVAILABLE;

    return IClassFactory_QueryInterface( cf, iid, obj );
}
