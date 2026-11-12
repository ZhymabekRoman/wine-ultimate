#ifndef __WINE_WINDOWS_DEVICES_GEOLOCATION_PRIVATE_H
#define __WINE_WINDOWS_DEVICES_GEOLOCATION_PRIVATE_H

#include <stdarg.h>

#define COBJMACROS
#include "windef.h"
#include "winbase.h"
#include "winstring.h"

#include "activation.h"

#define WIDL_using_Windows_Foundation
#define WIDL_using_Windows_Foundation_Collections
#include "windows.foundation.h"
#define WIDL_using_Windows_Devices_Geolocation
#include "windows.devices.geolocation.h"

/* Location shown when the system has no location service to ask: central London,
 * the coordinates every desktop geolocation backend has historically defaulted to.
 * Answering with a valid, ready position is what keeps the apps (WhatsApp Desktop,
 * Maps-style UWP shells) off their 'no location' retry loops. */
static const double default_latitude = 51.5074;
static const double default_longitude = -0.1278;
static const double default_accuracy = 15.0;
static const double default_altitude = 0.0;

struct geo_fix
{
    double latitude;
    double longitude;
    double accuracy;
    double altitude;
};

/* Implemented in unixlib.c against the GeoClue2 D-Bus service. */
enum unix_funcs
{
    unix_get_fix,
    unix_funcs_count
};

#endif
