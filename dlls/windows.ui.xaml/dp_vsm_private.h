/* Internal declarations shared across uwp_resources.c and uwp_depobj.c
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

#ifndef __WINE_WINDOWS_UI_XAML_DP_VSM_PRIVATE_H
#define __WINE_WINDOWS_UI_XAML_DP_VSM_PRIVATE_H

#include "uwp_private.h"

BOOL uwp_unbox_int32( void *obj, INT32 *out_val );
BOOL uwp_unbox_bool( void *obj, boolean *out_val );
BOOL uwp_unbox_double( void *obj, double *out_val );

#endif /* __WINE_WINDOWS_UI_XAML_DP_VSM_PRIVATE_H */
