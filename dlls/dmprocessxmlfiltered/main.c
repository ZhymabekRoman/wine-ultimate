/*
 * Copyright (C) 2026 Wine Project
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#include <stdarg.h>
#include <stdio.h>
#include "windef.h"
#include "winbase.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(dmxmlfilter);

/***********************************************************************
 *           DMProcessConfigXMLFiltered (DMXMLFILTER.@)
 *
 * Filter a device configuration XML against the installed device metadata.
 * A full implementation walks <DIFunctions> XML against the device tree;
 * for now pass the configuration through unfiltered.
 */
const WCHAR *WINAPI DMProcessConfigXMLFiltered( const WCHAR *configxml, const WCHAR *rootpath )
{
    FIXME( "configxml %s rootpath %s stub, passing through\n",
           debugstr_w( configxml ), debugstr_w( rootpath ) );
    return configxml;
}
