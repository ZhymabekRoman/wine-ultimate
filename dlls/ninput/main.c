/*
 * Copyright 2018 Andrey Gusev
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

#include <stdarg.h>

#include "windef.h"
#include "winbase.h"
#include "wine/debug.h"

#include "interactioncontext.h"

WINE_DEFAULT_DEBUG_CHANNEL(ninput);

struct interaction_context
{
    BOOL filter_pointers;
    BOOL ui_feedback;
    UINT32 measurement_units;
    INTERACTION_CONFIGURATION_FLAGS config_flags[INTERACTION_ID_CROSS_SLIDE];
    INTERACTION_CONTEXT_OUTPUT_CALLBACK callback;
    void *callback_data;

    POINTER_INFO *packets;
    UINT32 packet_count;
    UINT32 packet_capacity;

    BOOL down_active;
    BOOL down_secondary;
    UINT32 down_pointer_id;
    POINTER_INPUT_TYPE down_type;
    float down_x;
    float down_y;
    DWORD last_tap_time;
    float last_tap_x;
    float last_tap_y;
    INTERACTION_STATE state;
};

static struct interaction_context *context_from_handle(HINTERACTIONCONTEXT handle)
{
    return (struct interaction_context *)handle;
}

HRESULT WINAPI BufferPointerPacketsInteractionContext(HINTERACTIONCONTEXT handle,
        UINT32 entries_count, const POINTER_INFO *pointer_info)
{
    struct interaction_context *context = context_from_handle(handle);
    UINT32 needed;

    TRACE("context %p, entries_count %u, pointer_info %p.\n", context, entries_count, pointer_info);

    if (!context)
        return E_HANDLE;
    if (!entries_count)
        return E_INVALIDARG;
    if (!pointer_info)
        return E_POINTER;

    needed = context->packet_count + entries_count;
    if (needed > context->packet_capacity)
    {
        UINT32 new_cap = max(needed, context->packet_capacity ? context->packet_capacity * 2 : 16);
        POINTER_INFO *new_buf = realloc(context->packets, new_cap * sizeof(*new_buf));
        if (!new_buf)
            return E_OUTOFMEMORY;
        context->packets = new_buf;
        context->packet_capacity = new_cap;
    }

    memcpy(&context->packets[context->packet_count], pointer_info, entries_count * sizeof(*pointer_info));
    context->packet_count += entries_count;
    return S_OK;
}

HRESULT WINAPI CreateInteractionContext(HINTERACTIONCONTEXT *handle)
{
    struct interaction_context *context;

    TRACE("handle %p.\n", handle);

    if (!handle)
        return E_POINTER;

    if (!(context = calloc(1, sizeof(*context))))
        return E_OUTOFMEMORY;

    context->filter_pointers = TRUE;
    context->ui_feedback = TRUE;

    TRACE("Created context %p.\n", context);

    *handle = (HINTERACTIONCONTEXT)context;

    return S_OK;
}

HRESULT WINAPI DestroyInteractionContext(HINTERACTIONCONTEXT handle)
{
    struct interaction_context *context = context_from_handle(handle);

    TRACE("context %p.\n", context);

    if (!context)
        return E_HANDLE;

    free(context->packets);
    free(context);
    return S_OK;
}

HRESULT WINAPI GetPropertyInteractionContext(HINTERACTIONCONTEXT handle,
        INTERACTION_CONTEXT_PROPERTY property, UINT32 *value)
{
    struct interaction_context *context = context_from_handle(handle);

    TRACE("context %p, property %#x, value %p.\n", context, property, value);

    if (!context)
        return E_HANDLE;
    if (!value)
        return E_POINTER;

    switch (property)
    {
        case INTERACTION_CONTEXT_PROPERTY_MEASUREMENT_UNITS:
            *value = context->measurement_units;
            return S_OK;

        case INTERACTION_CONTEXT_PROPERTY_INTERACTION_UI_FEEDBACK:
            *value = context->ui_feedback;
            return S_OK;

        case INTERACTION_CONTEXT_PROPERTY_FILTER_POINTERS:
            *value = context->filter_pointers;
            return S_OK;

        default:
            WARN("Invalid property %#x.\n", property);
            return E_INVALIDARG;
    }
}

HRESULT WINAPI GetStateInteractionContext(HINTERACTIONCONTEXT handle,
        const POINTER_INFO *pointer_info, INTERACTION_STATE *state)
{
    struct interaction_context *context = context_from_handle(handle);

    TRACE("context %p, pointer_info %p, state %p.\n", context, pointer_info, state);

    if (!context)
        return E_HANDLE;
    if (!state)
        return E_POINTER;

    *state = context->state;
    return S_OK;
}

HRESULT WINAPI SetPropertyInteractionContext(HINTERACTIONCONTEXT handle,
        INTERACTION_CONTEXT_PROPERTY property, UINT32 value)
{
    struct interaction_context *context = context_from_handle(handle);

    TRACE("context %p, property %#x, value %#x.\n", context, property, value);

    if (!context)
        return E_HANDLE;

    switch (property)
    {
        case INTERACTION_CONTEXT_PROPERTY_MEASUREMENT_UNITS:
            context->measurement_units = value;
            return S_OK;

        case INTERACTION_CONTEXT_PROPERTY_INTERACTION_UI_FEEDBACK:
            if (value != FALSE && value != TRUE)
                return E_INVALIDARG;
            context->ui_feedback = value;
            return S_OK;

        case INTERACTION_CONTEXT_PROPERTY_FILTER_POINTERS:
            if (value != FALSE && value != TRUE)
                return E_INVALIDARG;
            context->filter_pointers = value;
            return S_OK;

        default:
            WARN("Invalid property %#x.\n", property);
            return E_INVALIDARG;
    }
}

HRESULT WINAPI SetInteractionConfigurationInteractionContext(HINTERACTIONCONTEXT handle,
        UINT32 count, const INTERACTION_CONTEXT_CONFIGURATION *configuration)
{
    struct interaction_context *context = context_from_handle(handle);

    FIXME("context %p, count %u, configuration %p: semi-stub!.\n", context, count, configuration);

    if (!context)
        return E_HANDLE;
    if (!count)
        return E_INVALIDARG;
    if (!configuration)
        return E_POINTER;

    for (unsigned int i = 0; i < count; i++)
    {
        if (configuration[i].interactionId == INTERACTION_ID_NONE ||
                configuration[i].interactionId > INTERACTION_ID_CROSS_SLIDE)
            return E_INVALIDARG;
    }

    for (unsigned int i = 0; i < count; i++)
    {
        unsigned int index = configuration[i].interactionId - 1;
        context->config_flags[index] = configuration[i].enable;
    }

    return S_OK;
}

HRESULT WINAPI GetInteractionConfigurationInteractionContext(HINTERACTIONCONTEXT handle,
        UINT32 count, INTERACTION_CONTEXT_CONFIGURATION *configuration)
{
    struct interaction_context *context = context_from_handle(handle);

    FIXME("context %p, count %u, configuration %p: semi-stub!\n", context, count, configuration);

    if (!context)
        return E_HANDLE;
    if (!count)
        return S_OK;
    if (!configuration)
        return E_POINTER;

    for (unsigned int i = 0; i < count; i++)
    {
        if (configuration[i].interactionId == INTERACTION_ID_NONE ||
                configuration[i].interactionId > INTERACTION_ID_CROSS_SLIDE)
            return E_INVALIDARG;
    }

    for (unsigned int i = 0; i < count; i++)
    {
        unsigned int index = configuration[i].interactionId - 1;
        configuration[i].enable = context->config_flags[index];
    }

    return S_OK;
}

HRESULT WINAPI RegisterOutputCallbackInteractionContext(HINTERACTIONCONTEXT handle,
        INTERACTION_CONTEXT_OUTPUT_CALLBACK callback, void *data)
{
    struct interaction_context *context = context_from_handle(handle);

    TRACE("context %p, callback %p, data %p.\n", context, callback, data);

    if (!context)
        return E_HANDLE;

    context->callback = callback;
    context->callback_data = data;
    return S_OK;
}

HRESULT WINAPI ProcessBufferedPacketsInteractionContext(HINTERACTIONCONTEXT handle)
{
    struct interaction_context *context = context_from_handle(handle);
    POINTER_INFO *packets;
    UINT32 count, i;

    TRACE("context %p.\n", context);

    if (!context)
        return E_HANDLE;

    if (!context->packet_count)
        return S_OK;

    /* Detach buffered packets before firing callbacks to handle re-entrancy safely. */
    count = context->packet_count;
    packets = malloc(count * sizeof(*packets));
    if (!packets)
        return E_OUTOFMEMORY;
    memcpy(packets, context->packets, count * sizeof(*packets));
    context->packet_count = 0;

    for (i = 0; i < count; i++)
    {
        const POINTER_INFO *p = &packets[i];
        float x = (context->measurement_units == 1) ? (float)p->ptPixelLocation.x : (float)p->ptHimetricLocation.x;
        float y = (context->measurement_units == 1) ? (float)p->ptPixelLocation.y : (float)p->ptHimetricLocation.y;
        BOOL in_contact = (p->pointerFlags & POINTER_FLAG_INCONTACT) != 0;
        BOOL is_down = (p->pointerFlags & POINTER_FLAG_DOWN)
                || p->ButtonChangeType == POINTER_CHANGE_FIRSTBUTTON_DOWN
                || p->ButtonChangeType == POINTER_CHANGE_SECONDBUTTON_DOWN
                || (!context->down_active && in_contact);
        BOOL is_up = (p->pointerFlags & POINTER_FLAG_UP)
                || p->ButtonChangeType == POINTER_CHANGE_FIRSTBUTTON_UP
                || p->ButtonChangeType == POINTER_CHANGE_SECONDBUTTON_UP
                || (context->down_active && !in_contact);

        TRACE("pkt[%u] type=%lu id=%u flags=%#x btn=%u px=(%ld,%ld) him=(%ld,%ld) tap_cfg=%#x\n",
                i, (unsigned long)p->pointerType, p->pointerId, p->pointerFlags, p->ButtonChangeType,
                p->ptPixelLocation.x, p->ptPixelLocation.y,
                p->ptHimetricLocation.x, p->ptHimetricLocation.y,
                context->config_flags[INTERACTION_ID_TAP - 1]);

        if (is_down)
        {
            context->down_active = TRUE;
            context->down_secondary = (p->ButtonChangeType == POINTER_CHANGE_SECONDBUTTON_DOWN)
                    || ((p->pointerFlags & POINTER_FLAG_SECONDBUTTON) && !(p->pointerFlags & POINTER_FLAG_FIRSTBUTTON));
            context->down_pointer_id = p->pointerId;
            context->down_type = p->pointerType ? p->pointerType : PT_MOUSE;
            context->down_x = x;
            context->down_y = y;
            context->state = INTERACTION_STATE_IN_INTERACTION;
        }
        else if (is_up && context->down_active)
        {
            float dx = x - context->down_x;
            float dy = y - context->down_y;
            BOOL secondary = context->down_secondary
                    || (p->ButtonChangeType == POINTER_CHANGE_SECONDBUTTON_UP);
            POINTER_INPUT_TYPE input_type = p->pointerType ? p->pointerType : context->down_type;

            context->down_active = FALSE;
            context->state = INTERACTION_STATE_IDLE;

            if (context->callback && (dx * dx + dy * dy <= 1000.0f * 1000.0f))
            {
                INTERACTION_CONTEXT_OUTPUT out;
                memset(&out, 0, sizeof(out));
                out.interactionFlags = INTERACTION_FLAG_BEGIN | INTERACTION_FLAG_END;
                out.inputType = input_type;
                out.x = x;
                out.y = y;

                if (secondary && (context->config_flags[INTERACTION_ID_SECONDARY_TAP - 1]
                        & INTERACTION_CONFIGURATION_FLAG_SECONDARY_TAP))
                {
                    out.interactionId = INTERACTION_ID_SECONDARY_TAP;
                    out.arguments.tap.count = 1;
                    TRACE("firing SECONDARY_TAP at (%.1f, %.1f)\n", x, y);
                    context->callback(context->callback_data, &out);
                }
                else if (!secondary && (context->config_flags[INTERACTION_ID_TAP - 1]
                        & INTERACTION_CONFIGURATION_FLAG_TAP))
                {
                    DWORD now = p->dwTime ? p->dwTime : GetTickCount();
                    UINT32 tap_count = 1;
                    if ((context->config_flags[INTERACTION_ID_TAP - 1] & INTERACTION_CONFIGURATION_FLAG_TAP_DOUBLE)
                            && context->last_tap_time && (now - context->last_tap_time) <= 500)
                    {
                        float tdx = x - context->last_tap_x;
                        float tdy = y - context->last_tap_y;
                        if (tdx * tdx + tdy * tdy <= 1000.0f * 1000.0f)
                            tap_count = 2;
                    }
                    context->last_tap_time = (tap_count == 2) ? 0 : now;
                    context->last_tap_x = x;
                    context->last_tap_y = y;

                    out.interactionId = INTERACTION_ID_TAP;
                    out.arguments.tap.count = tap_count;
                    TRACE("firing TAP count=%u at (%.1f, %.1f)\n", tap_count, x, y);
                    context->callback(context->callback_data, &out);
                }
            }
        }
    }

    free(packets);
    return S_OK;
}

HRESULT WINAPI ProcessInertiaInteractionContext(HINTERACTIONCONTEXT context)
{
    FIXME("context %p: stub!\n", context);
    return E_NOTIMPL;
}

HRESULT WINAPI SetMouseWheelParameterInteractionContext(HINTERACTIONCONTEXT handle,
        UINT32 parameter, float value)
{
    struct interaction_context *context = context_from_handle(handle);

    FIXME("context %p, parameter %u, value %f: stub!\n", context, parameter, value);

    if (!context)
        return E_HANDLE;

    return S_OK;
}

HRESULT WINAPI ProcessPointerFramesInteractionContext(HINTERACTIONCONTEXT handle,
        UINT32 entries_count, UINT32 pointer_count, const POINTER_INFO *pointer_info)
{
    struct interaction_context *context = context_from_handle(handle);

    FIXME("context %p, entries_count %u, pointer_count %u, pointer_info %p: stub!\n",
            context, entries_count, pointer_count, pointer_info);

    if (!context)
        return E_HANDLE;
    if (!pointer_info && entries_count && pointer_count)
        return E_POINTER;

    return S_OK;
}

HRESULT WINAPI ResetInteractionContext(HINTERACTIONCONTEXT handle)
{
    struct interaction_context *context = context_from_handle(handle);

    TRACE("context %p.\n", context);

    if (!context)
        return E_HANDLE;

    context->packet_count = 0;
    context->down_active = FALSE;
    return S_OK;
}

HRESULT WINAPI StopInteractionContext(HINTERACTIONCONTEXT handle)
{
    struct interaction_context *context = context_from_handle(handle);

    TRACE("context %p.\n", context);

    if (!context)
        return E_HANDLE;

    return S_OK;
}

/* Undocumented function at ordinal 2502 */
HRESULT WINAPI NINPUT_2502(HINTERACTIONCONTEXT *context, void *p1, void *p2)
{
    FIXME("context %p p1 %p p2 %p: stub!\n", context, p1, p2);

    if (!context)
        return E_HANDLE;

    return S_OK;
}
