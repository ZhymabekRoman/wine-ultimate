/*
 * Windows.Devices.Geolocation implementation - GeoClue2 backend
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

#include "config.h"

#include <stdlib.h>
#include <string.h>

#include <dbus/dbus.h>

#include "wine/debug.h"
#include "wine/list.h"
#include "wine/unixlib.h"

#include "private.h"

WINE_DEFAULT_DEBUG_CHANNEL(geolocator);

static void get_default_fix(struct geo_fix *fix)
{
    fix->latitude = default_latitude;
    fix->longitude = default_longitude;
    fix->accuracy = default_accuracy;
    fix->altitude = default_altitude;
}

/* ---------------------------------------------------------------------------
 * GeoClue2 (org.freedesktop.GeoClue2) backend.
 *
 * The Linux geolocation daemon exposes the current position over the session
 * bus: Manager.GetClient() hands out a client object, Client.AddInterface()
 * returns an object carrying the LastKnown position as Latitude/Longitude/
 * Accuracy/Altitude properties.  Every call is answered synchronously with a
 * short timeout, and every failure path falls back to the built-in default
 * fix, so a machine without GeoClue running behaves exactly as before.
 * ------------------------------------------------------------------------- */

#define GEOCLUE_SERVICE         "org.freedesktop.GeoClue2"
#define GEOCLUE_MANAGER_PATH    "/org/freedesktop/GeoClue2/Manager"
#define GEOCLUE_MANAGER_IFACE   "org.freedesktop.GeoClue2.Manager"
#define GEOCLUE_CLIENT_IFACE    "org.freedesktop.GeoClue2.Client"
#define DBUS_PROPERTIES_IFACE   "org.freedesktop.DBus.Properties"
#define GEOCLUE_TIMEOUT_MS      1500

static DBusConnection *geoclue_connect(void)
{
    DBusError err;
    DBusConnection *conn;

    dbus_error_init(&err);
    conn = dbus_bus_get(DBUS_BUS_SESSION, &err);
    if (dbus_error_is_set(&err))
    {
        TRACE("no D-Bus session bus: %s\n", err.message);
        dbus_error_free(&err);
        return NULL;
    }
    return conn;
}

static DBusMessage *geoclue_call(DBusConnection *conn, const char *path, const char *iface,
                                 const char *member, DBusMessage *call)
{
    DBusError err;
    DBusMessage *reply;

    dbus_error_init(&err);
    reply = dbus_connection_send_with_reply_and_block(conn, call, GEOCLUE_TIMEOUT_MS, &err);
    if (dbus_error_is_set(&err))
    {
        TRACE("call %s.%s on %s failed: %s\n", iface, member, path, err.message);
        dbus_error_free(&err);
    }
    dbus_message_unref(call);
    return reply;
}

static DBusMessage *geoclue_call_noargs(DBusConnection *conn, const char *path,
                                        const char *iface, const char *member)
{
    DBusMessage *call = dbus_message_new_method_call(GEOCLUE_SERVICE, path, iface, member);
    if (!call) return NULL;
    return geoclue_call(conn, path, iface, member, call);
}

/* Reads a variant property of the given basic type.  The callee keeps the
 * returned string for object-path/string reads; the caller frees it. */
static BOOL geoclue_get_property(DBusConnection *conn, const char *path, const char *iface,
                                 const char *name, int type, void *out)
{
    DBusMessage *call, *reply;
    DBusMessageIter iter, variant;
    const char *iface_name = iface, *prop_name = name;
    BOOL ret = FALSE;

    if (!(call = dbus_message_new_method_call(GEOCLUE_SERVICE, path, DBUS_PROPERTIES_IFACE, "Get")))
        return FALSE;
    if (!dbus_message_append_args(call, DBUS_TYPE_STRING, &iface_name, DBUS_TYPE_STRING, &prop_name,
                                  DBUS_TYPE_INVALID))
    {
        dbus_message_unref(call);
        return FALSE;
    }
    if (!(reply = geoclue_call(conn, path, DBUS_PROPERTIES_IFACE, "Get", call)))
        return FALSE;

    if (dbus_message_iter_init(reply, &iter) &&
        dbus_message_iter_get_arg_type(&iter) == DBUS_TYPE_VARIANT)
    {
        dbus_message_iter_recurse(&iter, &variant);
        if (dbus_message_iter_get_arg_type(&variant) == type)
        {
            dbus_message_iter_get_basic(&variant, out);
            ret = TRUE;
        }
    }
    dbus_message_unref(reply);
    return ret;
}

/* Calls a no-argument method returning a single object path. */
static BOOL geoclue_call_object_path(DBusConnection *conn, const char *path, const char *iface,
                                     const char *member, char **out)
{
    DBusMessage *reply;
    DBusMessageIter iter;
    const char *value = NULL;
    BOOL ret = FALSE;

    if (!(reply = geoclue_call_noargs(conn, path, iface, member)))
        return FALSE;
    if (dbus_message_iter_init(reply, &iter) &&
        dbus_message_iter_get_arg_type(&iter) == DBUS_TYPE_OBJECT_PATH)
    {
        dbus_message_iter_get_basic(&iter, &value);
        if ((*out = strdup(value))) ret = TRUE;
    }
    dbus_message_unref(reply);
    return ret;
}

static BOOL geoclue_get_fix(struct geo_fix *fix)
{
    char *client = NULL, *simple = NULL;
    DBusConnection *conn;
    double value;
    BOOL ret = FALSE;

    if (!(conn = geoclue_connect())) return FALSE;

    /* Manager.GetClient() -> /org/freedesktop/GeoClue2/Client/N */
    if (!geoclue_call_object_path(conn, GEOCLUE_MANAGER_PATH, GEOCLUE_MANAGER_IFACE, "GetClient", &client))
        goto done;

    /* Client.AddInterface() -> an object with the last known position on it. */
    if (!geoclue_call_object_path(conn, client, GEOCLUE_CLIENT_IFACE, "AddInterface", &simple))
        goto done;

    if (!geoclue_get_property(conn, simple, GEOCLUE_CLIENT_IFACE, "Latitude", DBUS_TYPE_DOUBLE, &value))
        goto done;
    fix->latitude = value;
    if (!geoclue_get_property(conn, simple, GEOCLUE_CLIENT_IFACE, "Longitude", DBUS_TYPE_DOUBLE, &value))
        goto done;
    fix->longitude = value;

    if (!geoclue_get_property(conn, simple, GEOCLUE_CLIENT_IFACE, "Accuracy", DBUS_TYPE_DOUBLE, &value))
        value = default_accuracy;
    fix->accuracy = value;
    if (!geoclue_get_property(conn, simple, GEOCLUE_CLIENT_IFACE, "Altitude", DBUS_TYPE_DOUBLE, &value))
        value = default_altitude;
    fix->altitude = value;

    /* A GeoClue client that never got a fix reports 0,0; treat it as 'no data'. */
    if (!fix->latitude && !fix->longitude) goto done;

    TRACE("geoclue fix %.6f,%.6f acc %.1f alt %.1f\n", fix->latitude, fix->longitude,
          fix->accuracy, fix->altitude);
    ret = TRUE;

done:
    dbus_free(client);
    dbus_free(simple);
    return ret;
}

static NTSTATUS unix_get_fix( void *arg )
{
    struct geo_fix *fix = arg;

    get_default_fix( fix );
    geoclue_get_fix( fix );
    return STATUS_SUCCESS;
}

const unixlib_entry_t __wine_unix_call_funcs[] =
{
    unix_get_fix,
};

C_ASSERT( ARRAYSIZE(__wine_unix_call_funcs) == unix_funcs_count );
