/* WinRT Windows.UI.Xaml shared helpers for controls_nav / controls_menu_dialog
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

#ifndef CONTROLS_NAV_PRIVATE_H
#define CONTROLS_NAV_PRIVATE_H

#include "uwp_private.h"

struct uwp_sel_changed_args
{
    IInspectable IInspectable_iface;
    LONG ref;
    IInspectable *removed_items;
    IInspectable *added_items;
};

struct uwp_sel_changed_args *uwp_sel_args_create( IInspectable *old_item, IInspectable *new_item );
ULONG WINAPI uwp_sel_args_Release( void *iface );
HRESULT WINAPI uwp_flyout_add_Closed( void *iface, void *handler, EventRegistrationToken *tok );

#endif /* CONTROLS_NAV_PRIVATE_H */
