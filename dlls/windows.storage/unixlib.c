/*
 * Windows.Storage Unix implementation: XDG Desktop Portal FileChooser over D-Bus
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

#if 0
#pragma makedep unix
#endif

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <dbus/dbus.h>

#include "ntstatus.h"
#include "windef.h"
#include "winternl.h"
#include "wine/debug.h"
#include "unixlib.h"

WINE_DEFAULT_DEBUG_CHANNEL(storage);

#define PORTAL_SERVICE   "org.freedesktop.portal.Desktop"
#define PORTAL_PATH      "/org/freedesktop/portal/desktop"
#define PORTAL_IFACE     "org.freedesktop.portal.FileChooser"
#define PORTAL_REQ_IFACE "org.freedesktop.portal.Request"
#define PORTAL_CALL_MS   5000
#define PORTAL_WAIT_SEC  300

static int portal_enabled(void)
{
    const char *env = getenv("WINE_USE_PORTAL_PICKER");
    return (env && !strcmp(env, "1")) ? 1 : 0;
}

static NTSTATUS storage_portal_is_enabled(void *args)
{
    return portal_enabled() ? STATUS_SUCCESS : STATUS_NOT_IMPLEMENTED;
}

static void append_dict_string(DBusMessageIter *dict, const char *key, const char *val)
{
    DBusMessageIter entry, var;
    dbus_message_iter_open_container(dict, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
    dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
    dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "s", &var);
    dbus_message_iter_append_basic(&var, DBUS_TYPE_STRING, &val);
    dbus_message_iter_close_container(&entry, &var);
    dbus_message_iter_close_container(dict, &entry);
}

static void append_dict_bool(DBusMessageIter *dict, const char *key, dbus_bool_t val)
{
    DBusMessageIter entry, var;
    dbus_message_iter_open_container(dict, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
    dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
    dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "b", &var);
    dbus_message_iter_append_basic(&var, DBUS_TYPE_BOOLEAN, &val);
    dbus_message_iter_close_container(&entry, &var);
    dbus_message_iter_close_container(dict, &entry);
}

static void append_dict_byte_array(DBusMessageIter *dict, const char *key, const char *path)
{
    DBusMessageIter entry, var, arr;
    int len = (int)strlen(path) + 1;
    dbus_message_iter_open_container(dict, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
    dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
    dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "ay", &var);
    dbus_message_iter_open_container(&var, DBUS_TYPE_ARRAY, "y", &arr);
    dbus_message_iter_append_fixed_array(&arr, DBUS_TYPE_BYTE, &path, len);
    dbus_message_iter_close_container(&var, &arr);
    dbus_message_iter_close_container(&entry, &var);
    dbus_message_iter_close_container(dict, &entry);
}

/* filters is packed: "Label\0*.png;*.jpg\0Other\0*.txt\0\0" */
static void append_dict_filters(DBusMessageIter *dict, const char *filters)
{
    DBusMessageIter entry, var, arr;
    const char *key = "filters";
    const char *p = filters;

    if (!filters || !*filters) return;

    dbus_message_iter_open_container(dict, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
    dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
    dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "a(sa(us))", &var);
    dbus_message_iter_open_container(&var, DBUS_TYPE_ARRAY, "(sa(us))", &arr);

    while (*p)
    {
        const char *label = p;
        const char *exts = label + strlen(label) + 1;
        DBusMessageIter filter_struct, pat_arr;
        char *exts_copy, *tok, *saveptr = NULL;

        if (!*exts) break;

        dbus_message_iter_open_container(&arr, DBUS_TYPE_STRUCT, NULL, &filter_struct);
        dbus_message_iter_append_basic(&filter_struct, DBUS_TYPE_STRING, &label);
        dbus_message_iter_open_container(&filter_struct, DBUS_TYPE_ARRAY, "(us)", &pat_arr);

        exts_copy = strdup(exts);
        if (exts_copy)
        {
            for (tok = strtok_r(exts_copy, ";", &saveptr); tok; tok = strtok_r(NULL, ";", &saveptr))
            {
                DBusMessageIter pat_struct;
                dbus_uint32_t kind = 0; /* 0 = glob pattern */
                if (!*tok) continue;
                dbus_message_iter_open_container(&pat_arr, DBUS_TYPE_STRUCT, NULL, &pat_struct);
                dbus_message_iter_append_basic(&pat_struct, DBUS_TYPE_UINT32, &kind);
                dbus_message_iter_append_basic(&pat_struct, DBUS_TYPE_STRING, &tok);
                dbus_message_iter_close_container(&pat_arr, &pat_struct);
            }
            free(exts_copy);
        }

        dbus_message_iter_close_container(&filter_struct, &pat_arr);
        dbus_message_iter_close_container(&arr, &filter_struct);

        p = exts + strlen(exts) + 1;
    }

    dbus_message_iter_close_container(&var, &arr);
    dbus_message_iter_close_container(&entry, &var);
    dbus_message_iter_close_container(dict, &entry);
}

static int hex_val(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int decode_file_uri(const char *uri, char *dst, size_t dst_cap)
{
    const char *src;
    size_t pos = 0;

    if (!strncmp(uri, "file://", 7)) src = uri + 7;
    else if (uri[0] == '/') src = uri;
    else return -1;

    /* Skip optional localhost authority */
    if (!strncmp(src, "localhost/", 10)) src += 9;

    while (*src && pos + 1 < dst_cap)
    {
        if (src[0] == '%' && hex_val(src[1]) >= 0 && hex_val(src[2]) >= 0)
        {
            dst[pos++] = (char)((hex_val(src[1]) << 4) | hex_val(src[2]));
            src += 3;
        }
        else
        {
            dst[pos++] = *src++;
        }
    }
    if (*src || pos == 0) return -1;
    dst[pos] = '\0';
    return (int)pos;
}

static NTSTATUS parse_response_uris(DBusMessage *sig, struct portal_pick_params *params)
{
    DBusMessageIter iter, dict;
    dbus_uint32_t response = 1;
    unsigned int out_pos = 0;

    if (!dbus_message_iter_init(sig, &iter)) return STATUS_UNSUCCESSFUL;
    if (dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_UINT32) return STATUS_UNSUCCESSFUL;
    dbus_message_iter_get_basic(&iter, &response);
    if (response != 0) return STATUS_CANCELLED;

    if (!dbus_message_iter_next(&iter)) return STATUS_CANCELLED;
    if (dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_ARRAY) return STATUS_UNSUCCESSFUL;

    dbus_message_iter_recurse(&iter, &dict);
    while (dbus_message_iter_get_arg_type(&dict) == DBUS_TYPE_DICT_ENTRY)
    {
        DBusMessageIter entry, var, uris;
        const char *key = NULL;

        dbus_message_iter_recurse(&dict, &entry);
        if (dbus_message_iter_get_arg_type(&entry) == DBUS_TYPE_STRING)
        {
            dbus_message_iter_get_basic(&entry, &key);
            if (key && !strcmp(key, "uris") && dbus_message_iter_next(&entry) &&
                dbus_message_iter_get_arg_type(&entry) == DBUS_TYPE_VARIANT)
            {
                dbus_message_iter_recurse(&entry, &var);
                if (dbus_message_iter_get_arg_type(&var) == DBUS_TYPE_ARRAY)
                {
                    dbus_message_iter_recurse(&var, &uris);
                    while (dbus_message_iter_get_arg_type(&uris) == DBUS_TYPE_STRING)
                    {
                        const char *uri = NULL;
                        dbus_message_iter_get_basic(&uris, &uri);
                        if (uri && out_pos + 2 < params->out_size)
                        {
                            int n = decode_file_uri(uri, params->out_paths + out_pos,
                                                    params->out_size - out_pos - 1);
                            if (n > 0)
                            {
                                out_pos += (unsigned int)n + 1;
                                params->out_count++;
                            }
                        }
                        dbus_message_iter_next(&uris);
                    }
                }
            }
        }
        dbus_message_iter_next(&dict);
    }

    if (params->out_size > out_pos) params->out_paths[out_pos] = '\0';
    return params->out_count ? STATUS_SUCCESS : STATUS_CANCELLED;
}

static NTSTATUS storage_portal_pick(void *args)
{
    static unsigned int token_seq = 0;
    struct portal_pick_params *params = args;
    DBusError err;
    DBusConnection *conn;
    DBusMessage *msg = NULL, *reply = NULL;
    DBusMessageIter iter, dict;
    const char *method;
    const char *parent_window = "";
    const char *title;
    const char *req_path = NULL;
    char token[64];
    char match_rule[256];
    NTSTATUS status = STATUS_UNSUCCESSFUL;
    struct timespec start, now;

    if (!portal_enabled()) return STATUS_NOT_IMPLEMENTED;
    if (!params || !params->out_paths || params->out_size < 2) return STATUS_INVALID_PARAMETER;

    params->out_count = 0;
    params->out_paths[0] = '\0';
    params->out_paths[1] = '\0';

    dbus_error_init(&err);
    conn = dbus_bus_get(DBUS_BUS_SESSION, &err);
    if (!conn || dbus_error_is_set(&err))
    {
        WARN("D-Bus session connection failed: %s\n", dbus_error_is_set(&err) ? err.message : "null");
        dbus_error_free(&err);
        return STATUS_UNSUCCESSFUL;
    }

    snprintf(token, sizeof(token), "wine_picker_%u_%u", (unsigned int)getpid(),
             __atomic_add_fetch(&token_seq, 1, __ATOMIC_RELAXED));
    snprintf(match_rule, sizeof(match_rule),
             "type='signal',interface='" PORTAL_REQ_IFACE "',member='Response'");
    dbus_bus_add_match(conn, match_rule, &err);
    if (dbus_error_is_set(&err))
    {
        dbus_error_free(&err);
        return STATUS_UNSUCCESSFUL;
    }
    dbus_connection_flush(conn);

    method = (params->mode == PORTAL_PICKER_SAVE) ? "SaveFile" : "OpenFile";
    title = (params->title && *params->title) ? params->title
            : (params->mode == PORTAL_PICKER_SAVE ? "Save File"
               : (params->mode == PORTAL_PICKER_FOLDER ? "Select Folder" : "Open File"));

    msg = dbus_message_new_method_call(PORTAL_SERVICE, PORTAL_PATH, PORTAL_IFACE, method);
    if (!msg) goto cleanup;

    dbus_message_iter_init_append(msg, &iter);
    dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &parent_window);
    dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &title);
    dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY, "{sv}", &dict);

    append_dict_string(&dict, "handle_token", token);
    if (params->accept_label && *params->accept_label)
        append_dict_string(&dict, "accept_label", params->accept_label);
    if (params->mode == PORTAL_PICKER_OPEN_MULTIPLE)
        append_dict_bool(&dict, "multiple", TRUE);
    if (params->mode == PORTAL_PICKER_FOLDER)
        append_dict_bool(&dict, "directory", TRUE);
    if (params->mode == PORTAL_PICKER_SAVE && params->current_name && *params->current_name)
        append_dict_string(&dict, "current_name", params->current_name);
    if (params->current_folder && *params->current_folder)
        append_dict_byte_array(&dict, "current_folder", params->current_folder);
    if (params->filters && *params->filters)
        append_dict_filters(&dict, params->filters);

    dbus_message_iter_close_container(&iter, &dict);

    reply = dbus_connection_send_with_reply_and_block(conn, msg, PORTAL_CALL_MS, &err);
    dbus_message_unref(msg);
    msg = NULL;

    if (!reply || dbus_error_is_set(&err))
    {
        WARN("Portal %s failed: %s\n", method, dbus_error_is_set(&err) ? err.message : "no reply");
        dbus_error_free(&err);
        goto cleanup;
    }

    if (!dbus_message_get_args(reply, &err, DBUS_TYPE_OBJECT_PATH, &req_path, DBUS_TYPE_INVALID) || !req_path)
    {
        dbus_error_free(&err);
        goto cleanup;
    }

    clock_gettime(CLOCK_MONOTONIC, &start);
    for (;;)
    {
        DBusMessage *incoming;

        dbus_connection_read_write(conn, 100);
        while ((incoming = dbus_connection_pop_message(conn)) != NULL)
        {
            if (dbus_message_is_signal(incoming, PORTAL_REQ_IFACE, "Response"))
            {
                const char *sig_path = dbus_message_get_path(incoming);
                if (sig_path && !strcmp(sig_path, req_path))
                {
                    status = parse_response_uris(incoming, params);
                    dbus_message_unref(incoming);
                    goto cleanup;
                }
            }
            dbus_message_unref(incoming);
        }

        clock_gettime(CLOCK_MONOTONIC, &now);
        if (now.tv_sec - start.tv_sec >= PORTAL_WAIT_SEC)
        {
            status = STATUS_CANCELLED;
            goto cleanup;
        }
    }

cleanup:
    if (reply) dbus_message_unref(reply);
    dbus_bus_remove_match(conn, match_rule, NULL);
    return status;
}

const unixlib_entry_t __wine_unix_call_funcs[] =
{
    storage_portal_is_enabled,
    storage_portal_pick,
};

C_ASSERT(ARRAYSIZE(__wine_unix_call_funcs) == unix_funcs_count);

#ifdef _WIN64
const unixlib_entry_t __wine_unix_call_wow64_funcs[] =
{
    storage_portal_is_enabled,
    storage_portal_pick,
};
#endif
