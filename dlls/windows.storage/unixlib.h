/*
 * Windows.Storage Unix library interface (XDG Desktop Portal FileChooser)
 *
 * Copyright 2026 Wine Project
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

#ifndef __WINE_WINDOWS_STORAGE_UNIXLIB_H
#define __WINE_WINDOWS_STORAGE_UNIXLIB_H

#include "wine/unixlib.h"

enum portal_picker_mode
{
    PORTAL_PICKER_OPEN_SINGLE = 0,
    PORTAL_PICKER_OPEN_MULTIPLE = 1,
    PORTAL_PICKER_SAVE = 2,
    PORTAL_PICKER_FOLDER = 3,
};

struct portal_pick_params
{
    int mode;
    const char *title;
    const char *accept_label;
    const char *current_name;
    const char *current_folder;
    const char *filters; /* packed: "Label\0*.ext1;*.ext2\0...\0\0" */
    char *out_paths;     /* packed UTF-8 Unix paths, NUL-separated, double-NUL terminated */
    unsigned int out_size;
    unsigned int out_count;
};

enum storage_unix_funcs
{
    unix_portal_is_enabled,
    unix_portal_pick,
    unix_funcs_count
};

#endif /* __WINE_WINDOWS_STORAGE_UNIXLIB_H */
