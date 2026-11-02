/*
 * wineuwp: in-process stand-ins for the Windows shell brokers a packaged (UWP) app talks to.
 *
 * Internal declarations shared by the module's translation units.  main.c defines the GUIDs
 * (it includes initguid.h first); the other units see them as plain externs.
 *
 * Copyright 2026 the Wine project authors
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

#ifndef __WINE_WINEUWP_PRIVATE_H
#define __WINE_WINEUWP_PRIVATE_H

#include <stdarg.h>

#define COBJMACROS
#include "windef.h"
#include "winbase.h"
#include "winerror.h"
#include "winstring.h"
#include "objbase.h"
#include "inspectable.h"

/* WindowServer's navigation client property on a CoreWindow: slot 6 get_NavigationClient,
 * slot 7 put_NavigationClient. */
extern const GUID IID_ICoreWindowNavigation;
/* CoreApplicationView::PrepareToActivateAsync QIs the navigation client for this and calls its
 * first method, PrepareToActivate(UINT64, IAsyncAction **). */
extern const GUID IID_INavigationClientPrepareToActivate;
/* The parameterised IIDs of the completed IAsyncAction the stand-in hands back. */
extern const GUID IID_IAsyncActionStandIn;
extern const GUID IID_IAsyncInfoStandIn;

/* The one view this host creates (PrivateCreateCoreWindow), in physical pixels at 100%. */
#define CORE_WINDOW_WIDTH  1280
#define CORE_WINDOW_HEIGHT 800

/* The hosted CoreWindow, once shown; the view bounds reported to a navigation client follow its
 * client area, so the window can be resized through the frame it is given. */
HWND wineuwp_hosted_core_window( void );
void wineuwp_view_size( FLOAT *width, FLOAT *height );

/* navigation.c */
HRESULT create_navigation_client( IUnknown **out );
HRESULT attach_navigation_client( IUnknown *window );

/* clientadapter.c */
HRESULT attach_window_client_adapter( IUnknown *window, HWND hwnd );

#endif /* __WINE_WINEUWP_PRIVATE_H */
