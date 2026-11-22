/* Shared prototypes for the WinUI3 element/collection/event split.
 *
 * Copyright (C) 2026 Wine project
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

#ifndef __WINE_WINUI_PRIVATE_H
#define __WINE_WINUI_PRIVATE_H

struct xaml_element;

/* WinUI 3 UIElement / FrameworkElement event kinds (registry lives in winui_events.c) */
enum xaml_winui_ev_kind
{
    WINUI_EV_SIZE_CHANGED = 1,
    WINUI_EV_EFFECTIVE_VIEWPORT,
    WINUI_EV_KEY_UP,
    WINUI_EV_KEY_DOWN,
    WINUI_EV_GOT_FOCUS,
    WINUI_EV_LOST_FOCUS,
    WINUI_EV_POINTER_PRESSED,
    WINUI_EV_POINTER_MOVED,
    WINUI_EV_POINTER_RELEASED,
    WINUI_EV_POINTER_ENTERED,
    WINUI_EV_POINTER_EXITED,
    WINUI_EV_POINTER_CAPTURE_LOST,
    WINUI_EV_POINTER_WHEEL,
};

/* winui_events.c */
HRESULT winui_ev_add( enum xaml_winui_ev_kind kind, struct xaml_element *impl,
                      void *handler, EventRegistrationToken *token );
HRESULT WINAPI winui_ev_remove( void *iface, EventRegistrationToken token );

/* winui_collections.c */
struct xaml_collection
{
    IInspectable IInspectable_iface;
    LONG ref;
    IInspectable **items;
    unsigned int count;
    unsigned int capacity;
    void *iterable_view;  /* cached IIterable<RowDefinition> view for IV {5ddd9577} */
    void *vector_view;    /* cached IVectorView<RowDefinition> for IV3 {94ea2b94} */
    void *parent;         /* owner collection of a view shard (back-pointer) */
};

void **vtbl_dup( const void *src );
void **vtbl_dup_n( const void *src, size_t count );
HRESULT collection_create( void **instance );
struct xaml_collection *impl_from_collection( void *iface );
HRESULT WINAPI collection_Append( void *iface, void *item );

/* frame.c */
HRESULT WINAPI frame_generic_slot( void *iface, ... );
HRESULT WINAPI element_generic_slot( void *iface, ... );
extern float g_winui_actual_w;
extern float g_winui_actual_h;
HRESULT WINAPI xaml_root_GetIids( void *iface, ULONG *c, IID **i );
HRESULT WINAPI xaml_root_GetRuntimeClassName( void *iface, HSTRING *name );
HRESULT WINAPI xaml_root_GetTrustLevel( void *iface, TrustLevel *level );

/* Reference-tracker GUIDs (defined in frame.c, shared by collections) */
extern const GUID IID_IReferenceTracker;
extern const GUID IID_IReferenceTrackerTarget;
extern const GUID IID_IReferenceTrackerTarget_WinUI;

#endif /* __WINE_WINUI_PRIVATE_H */
