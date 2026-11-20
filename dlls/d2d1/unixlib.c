/*
 * d2d1 unix implementation: SVG rasterization via librsvg-2.so.2 + libcairo.so.2
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#if 0
#pragma makedep unix
#endif

#include "config.h"

#include <dlfcn.h>
#include <pthread.h>
#include <stdint.h>
#include <string.h>

#include "ntstatus.h"
#define WIN32_NO_STATUS
#include "windef.h"
#include "winbase.h"
#include "wine/debug.h"
#include "unixlib.h"

WINE_DEFAULT_DEBUG_CHANNEL(d2d);

/* Minimal ABI-exact declarations for librsvg-2.so.2, libcairo.so.2, libgobject-2.0.so.0 */
typedef int gboolean;
typedef struct _GError GError;
typedef struct _RsvgHandle RsvgHandle;
typedef struct _cairo cairo_t;
typedef struct _cairo_surface cairo_surface_t;

typedef struct _RsvgRectangle
{
    double x;
    double y;
    double width;
    double height;
} RsvgRectangle;

#define CAIRO_FORMAT_ARGB32    0
#define CAIRO_ANTIALIAS_BEST   6

static pthread_once_t svg_libs_once = PTHREAD_ONCE_INIT;
static int svg_libs_ok;

static RsvgHandle *(*p_rsvg_handle_new_from_data)(const uint8_t *data, size_t data_len, GError **error);
static gboolean (*p_rsvg_handle_render_document)(RsvgHandle *handle, cairo_t *cr,
        const RsvgRectangle *viewport, GError **error);
static void (*p_rsvg_handle_get_intrinsic_dimensions)(RsvgHandle *handle,
        gboolean *out_has_width, void *out_width,
        gboolean *out_has_height, void *out_height,
        gboolean *out_has_viewbox, RsvgRectangle *out_viewbox);
static gboolean (*p_rsvg_handle_get_intrinsic_size_in_pixels)(RsvgHandle *handle,
        double *out_width, double *out_height);
static void (*p_g_object_unref)(void *object);
static void (*p_g_clear_error)(GError **err);

static cairo_surface_t *(*p_cairo_image_surface_create_for_data)(unsigned char *data, int format,
        int width, int height, int stride);
static cairo_t *(*p_cairo_create)(cairo_surface_t *target);
static void (*p_cairo_destroy)(cairo_t *cr);
static void (*p_cairo_surface_destroy)(cairo_surface_t *surface);
static void (*p_cairo_surface_flush)(cairo_surface_t *surface);
static void (*p_cairo_set_antialias)(cairo_t *cr, int antialias);

static void svg_libs_init(void)
{
    void *rsvg_mod, *cairo_mod, *gobj_mod, *glib_mod;

    rsvg_mod = dlopen("librsvg-2.so.2", RTLD_LAZY);
    cairo_mod = dlopen("libcairo.so.2", RTLD_LAZY);
    if (!rsvg_mod || !cairo_mod)
    {
        ERR("Failed to load librsvg-2.so.2 (%p) or libcairo.so.2 (%p).\n", rsvg_mod, cairo_mod);
        return;
    }

    p_rsvg_handle_new_from_data = dlsym(rsvg_mod, "rsvg_handle_new_from_data");
    p_rsvg_handle_render_document = dlsym(rsvg_mod, "rsvg_handle_render_document");
    p_rsvg_handle_get_intrinsic_dimensions = dlsym(rsvg_mod, "rsvg_handle_get_intrinsic_dimensions");
    p_rsvg_handle_get_intrinsic_size_in_pixels = dlsym(rsvg_mod, "rsvg_handle_get_intrinsic_size_in_pixels");

    p_g_object_unref = dlsym(rsvg_mod, "g_object_unref");
    if (!p_g_object_unref && (gobj_mod = dlopen("libgobject-2.0.so.0", RTLD_LAZY)))
        p_g_object_unref = dlsym(gobj_mod, "g_object_unref");

    p_g_clear_error = dlsym(rsvg_mod, "g_clear_error");
    if (!p_g_clear_error && (glib_mod = dlopen("libglib-2.0.so.0", RTLD_LAZY)))
        p_g_clear_error = dlsym(glib_mod, "g_clear_error");

    p_cairo_image_surface_create_for_data = dlsym(cairo_mod, "cairo_image_surface_create_for_data");
    p_cairo_create = dlsym(cairo_mod, "cairo_create");
    p_cairo_destroy = dlsym(cairo_mod, "cairo_destroy");
    p_cairo_surface_destroy = dlsym(cairo_mod, "cairo_surface_destroy");
    p_cairo_surface_flush = dlsym(cairo_mod, "cairo_surface_flush");
    p_cairo_set_antialias = dlsym(cairo_mod, "cairo_set_antialias");

    if (!p_rsvg_handle_new_from_data || !p_rsvg_handle_render_document || !p_g_object_unref
            || !p_cairo_image_surface_create_for_data || !p_cairo_create || !p_cairo_destroy
            || !p_cairo_surface_destroy || !p_cairo_surface_flush)
    {
        ERR("Missing required librsvg/libcairo symbols.\n");
        return;
    }

    svg_libs_ok = 1;
}

static NTSTATUS svg_get_dimensions(void *args)
{
    struct d2d1_svg_dimensions_params *p = args;
    RsvgHandle *handle;
    GError *err = NULL;

    pthread_once(&svg_libs_once, svg_libs_init);
    if (!svg_libs_ok || !p->xml || !p->xml_len)
        return STATUS_UNSUCCESSFUL;

    handle = p_rsvg_handle_new_from_data(p->xml, p->xml_len, &err);
    if (err && p_g_clear_error) p_g_clear_error(&err);
    if (!handle)
        return STATUS_INVALID_PARAMETER;

    p->has_vb = FALSE;
    p->vb_x = p->vb_y = p->vb_w = p->vb_h = 0.0f;

    if (p_rsvg_handle_get_intrinsic_dimensions)
    {
        gboolean has_vb = 0;
        RsvgRectangle vb = {0};
        p_rsvg_handle_get_intrinsic_dimensions(handle, NULL, NULL, NULL, NULL, &has_vb, &vb);
        if (has_vb && vb.width > 0.0 && vb.height > 0.0)
        {
            p->has_vb = TRUE;
            p->vb_x = (float)vb.x;
            p->vb_y = (float)vb.y;
            p->vb_w = (float)vb.width;
            p->vb_h = (float)vb.height;
        }
    }

    if (!p->has_vb && p_rsvg_handle_get_intrinsic_size_in_pixels)
    {
        double w = 0.0, h = 0.0;
        if (p_rsvg_handle_get_intrinsic_size_in_pixels(handle, &w, &h) && w > 0.0 && h > 0.0)
        {
            p->has_vb = TRUE;
            p->vb_w = (float)w;
            p->vb_h = (float)h;
        }
    }

    p_g_object_unref(handle);
    return STATUS_SUCCESS;
}

static NTSTATUS svg_rasterize(void *args)
{
    struct d2d1_svg_rasterize_params *p = args;
    RsvgHandle *handle;
    cairo_surface_t *surface;
    cairo_t *cr;
    RsvgRectangle viewport;
    GError *err = NULL;
    gboolean ok;

    pthread_once(&svg_libs_once, svg_libs_init);
    if (!svg_libs_ok || !p->xml || !p->xml_len || !p->out_bgra || !p->width || !p->height
            || p->out_stride < p->width * 4)
        return STATUS_INVALID_PARAMETER;

    handle = p_rsvg_handle_new_from_data(p->xml, p->xml_len, &err);
    if (err && p_g_clear_error) p_g_clear_error(&err);
    if (!handle)
        return STATUS_UNSUCCESSFUL;

    memset(p->out_bgra, 0, (size_t)p->out_stride * p->height);

    surface = p_cairo_image_surface_create_for_data(p->out_bgra, CAIRO_FORMAT_ARGB32,
            (int)p->width, (int)p->height, (int)p->out_stride);
    if (!surface)
    {
        p_g_object_unref(handle);
        return STATUS_NO_MEMORY;
    }

    cr = p_cairo_create(surface);
    if (!cr)
    {
        p_cairo_surface_destroy(surface);
        p_g_object_unref(handle);
        return STATUS_NO_MEMORY;
    }

    if (p_cairo_set_antialias)
        p_cairo_set_antialias(cr, CAIRO_ANTIALIAS_BEST);

    viewport.x = 0.0;
    viewport.y = 0.0;
    viewport.width = (double)p->width;
    viewport.height = (double)p->height;

    ok = p_rsvg_handle_render_document(handle, cr, &viewport, &err);
    if (err && p_g_clear_error) p_g_clear_error(&err);

    p_cairo_surface_flush(surface);
    p_cairo_destroy(cr);
    p_cairo_surface_destroy(surface);
    p_g_object_unref(handle);

    return ok ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
}

const unixlib_entry_t __wine_unix_call_funcs[] =
{
    svg_get_dimensions,
    svg_rasterize,
};
