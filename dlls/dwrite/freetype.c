/*
 *    FreeType integration
 *
 * Copyright 2014-2017 Nikolay Sivov for CodeWeavers
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

#include <sys/types.h>
#include <dlfcn.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#ifdef HAVE_FT2BUILD_H
#include <ft2build.h>
#include FT_GLYPH_H
#include FT_FREETYPE_H
#include FT_OUTLINE_H
#include FT_TRUETYPE_TABLES_H
#include FT_SIZES_H
#ifdef FT_LCD_FILTER_H
#include FT_LCD_FILTER_H
#endif
#endif /* HAVE_FT2BUILD_H */

#include "ntstatus.h"
#include "windef.h"
#include "wine/debug.h"
#include "unixlib.h"

#include "dwrite_private.h"

#ifdef SONAME_LIBFREETYPE

WINE_DEFAULT_DEBUG_CHANNEL(dwrite);

static void *ft_handle = NULL;
static FT_Library library = 0;
typedef struct
{
    FT_Int major;
    FT_Int minor;
    FT_Int patch;
} FT_Version_t;

#define MAKE_FUNCPTR(f) static typeof(f) * p##f = NULL
MAKE_FUNCPTR(FT_Activate_Size);
MAKE_FUNCPTR(FT_Done_Face);
MAKE_FUNCPTR(FT_Done_FreeType);
MAKE_FUNCPTR(FT_Done_Glyph);
MAKE_FUNCPTR(FT_Done_Size);
MAKE_FUNCPTR(FT_Get_First_Char);
MAKE_FUNCPTR(FT_Get_Glyph);
MAKE_FUNCPTR(FT_Get_Kerning);
MAKE_FUNCPTR(FT_Get_Sfnt_Table);
MAKE_FUNCPTR(FT_Glyph_Copy);
MAKE_FUNCPTR(FT_Glyph_Get_CBox);
MAKE_FUNCPTR(FT_Glyph_To_Bitmap);
MAKE_FUNCPTR(FT_Glyph_Transform);
MAKE_FUNCPTR(FT_Init_FreeType);
MAKE_FUNCPTR(FT_Library_Version);
MAKE_FUNCPTR(FT_Load_Glyph);
MAKE_FUNCPTR(FT_Matrix_Multiply);
MAKE_FUNCPTR(FT_MulDiv);
MAKE_FUNCPTR(FT_New_Memory_Face);
MAKE_FUNCPTR(FT_New_Size);
MAKE_FUNCPTR(FT_Outline_Copy);
MAKE_FUNCPTR(FT_Outline_Decompose);
MAKE_FUNCPTR(FT_Outline_Done);
MAKE_FUNCPTR(FT_Outline_EmboldenXY);
MAKE_FUNCPTR(FT_Outline_Get_Bitmap);
MAKE_FUNCPTR(FT_Outline_New);
MAKE_FUNCPTR(FT_Outline_Transform);
MAKE_FUNCPTR(FT_Outline_Translate);
MAKE_FUNCPTR(FT_Set_Pixel_Sizes);
#undef MAKE_FUNCPTR

static FT_Error (*pFT_Library_SetLcdFilter)(FT_Library, int) = NULL;

#define FaceFromObject(o) ((FT_Face)(ULONG_PTR)(o))

static FT_Size freetype_set_face_size(FT_Face face, FT_UInt emsize)
{
    FT_Size size;

    if (pFT_New_Size(face, &size)) return NULL;

    pFT_Activate_Size(size);

    if (pFT_Set_Pixel_Sizes(face, emsize, emsize))
    {
        pFT_Done_Size(size);
        return NULL;
    }

    return size;
}

static BOOL freetype_glyph_has_contours(FT_Face face)
{
    return face->glyph->format == FT_GLYPH_FORMAT_OUTLINE && face->glyph->outline.n_contours;
}

static NTSTATUS process_attach(void *args)
{
    FT_Version_t FT_Version;

    ft_handle = dlopen(SONAME_LIBFREETYPE, RTLD_NOW);
    if (!ft_handle)
    {
        WINE_MESSAGE("Wine cannot find the FreeType font library.\n");
        return STATUS_DLL_NOT_FOUND;
    }

#define LOAD_FUNCPTR(f) if((p##f = dlsym(ft_handle, #f)) == NULL){WARN("Can't find symbol %s\n", #f); goto sym_not_found;}
    LOAD_FUNCPTR(FT_Activate_Size)
    LOAD_FUNCPTR(FT_Done_Face)
    LOAD_FUNCPTR(FT_Done_FreeType)
    LOAD_FUNCPTR(FT_Done_Glyph)
    LOAD_FUNCPTR(FT_Done_Size)
    LOAD_FUNCPTR(FT_Get_First_Char)
    LOAD_FUNCPTR(FT_Get_Glyph)
    LOAD_FUNCPTR(FT_Get_Kerning)
    LOAD_FUNCPTR(FT_Get_Sfnt_Table)
    LOAD_FUNCPTR(FT_Glyph_Copy)
    LOAD_FUNCPTR(FT_Glyph_Get_CBox)
    LOAD_FUNCPTR(FT_Glyph_To_Bitmap)
    LOAD_FUNCPTR(FT_Glyph_Transform)
    LOAD_FUNCPTR(FT_Init_FreeType)
    LOAD_FUNCPTR(FT_Library_Version)
    LOAD_FUNCPTR(FT_Load_Glyph)
    LOAD_FUNCPTR(FT_Matrix_Multiply)
    LOAD_FUNCPTR(FT_MulDiv)
    LOAD_FUNCPTR(FT_New_Memory_Face)
    LOAD_FUNCPTR(FT_New_Size)
    LOAD_FUNCPTR(FT_Outline_Copy)
    LOAD_FUNCPTR(FT_Outline_Decompose)
    LOAD_FUNCPTR(FT_Outline_Done)
    LOAD_FUNCPTR(FT_Outline_EmboldenXY)
    LOAD_FUNCPTR(FT_Outline_Get_Bitmap)
    LOAD_FUNCPTR(FT_Outline_New)
    LOAD_FUNCPTR(FT_Outline_Transform)
    LOAD_FUNCPTR(FT_Outline_Translate)
    LOAD_FUNCPTR(FT_Set_Pixel_Sizes)
#undef LOAD_FUNCPTR

    pFT_Library_SetLcdFilter = dlsym(ft_handle, "FT_Library_SetLcdFilter");

    if (pFT_Init_FreeType(&library) != 0)
    {
        ERR("Can't init FreeType library\n");
        dlclose(ft_handle);
        ft_handle = NULL;
        return STATUS_UNSUCCESSFUL;
    }
    if (pFT_Library_SetLcdFilter)
        pFT_Library_SetLcdFilter(library, 1 /* FT_LCD_FILTER_DEFAULT */);

    pFT_Library_Version(library, &FT_Version.major, &FT_Version.minor, &FT_Version.patch);

    TRACE("FreeType version is %d.%d.%d\n", FT_Version.major, FT_Version.minor, FT_Version.patch);
    return STATUS_SUCCESS;

sym_not_found:
    WINE_MESSAGE("Wine cannot find certain functions that it needs from FreeType library.\n");
    dlclose(ft_handle);
    ft_handle = NULL;
    return STATUS_UNSUCCESSFUL;
}

static void fontconfig_cleanup(void);

static NTSTATUS process_detach(void *args)
{
    fontconfig_cleanup();
    pFT_Done_FreeType(library);
    return STATUS_SUCCESS;
}

static NTSTATUS create_font_object(void *args)
{
    struct create_font_object_params *params = args;
    FT_Face face = NULL;
    FT_Error fterror;

    fterror = pFT_New_Memory_Face(library, params->data, params->size, params->index, &face);
    if (fterror != FT_Err_Ok)
    {
        WARN("Failed to create a face object, error %d.\n", fterror);
        return STATUS_UNSUCCESSFUL;
    }

    *params->object = (ULONG_PTR)face;

    return STATUS_SUCCESS;
}

static NTSTATUS release_font_object(void *args)
{
    struct release_font_object_params *params = args;
    pFT_Done_Face(FaceFromObject(params->object));
    return STATUS_SUCCESS;
}

static NTSTATUS get_design_glyph_metrics(void *args)
{
    struct get_design_glyph_metrics_params *params = args;
    FT_Face face = FaceFromObject(params->object);
    FT_Size size;

    if (!(size = freetype_set_face_size(face, params->upem)))
        return STATUS_UNSUCCESSFUL;

    if (!pFT_Load_Glyph(face, params->glyph, FT_LOAD_NO_SCALE))
    {
        FT_Glyph_Metrics *metrics = &face->glyph->metrics;

        params->metrics->leftSideBearing = metrics->horiBearingX;
        params->metrics->advanceWidth = metrics->horiAdvance;
        params->metrics->rightSideBearing = metrics->horiAdvance - metrics->horiBearingX - metrics->width;

        params->metrics->advanceHeight = metrics->vertAdvance;
        params->metrics->verticalOriginY = params->ascent;
        params->metrics->topSideBearing = params->ascent - metrics->horiBearingY;
        params->metrics->bottomSideBearing = metrics->vertAdvance - metrics->height - params->metrics->topSideBearing;

        /* Adjust in case of bold simulation, glyphs without contours are ignored. */
        if (params->simulations & DWRITE_FONT_SIMULATIONS_BOLD && freetype_glyph_has_contours(face))
        {
            if (params->metrics->advanceWidth)
                params->metrics->advanceWidth += (params->upem + 49) / 50;
        }
    }

    pFT_Done_Size(size);

    return STATUS_SUCCESS;
}

struct decompose_context
{
    struct dwrite_outline *outline;
    BOOL figure_started;
    BOOL move_to;     /* last call was 'move_to' */
    FT_Vector origin; /* 'pen' position from last call */
};

static inline void ft_vector_to_d2d_point(const FT_Vector *v, D2D1_POINT_2F *p)
{
    p->x = v->x / 64.0f;
    p->y = v->y / 64.0f;
}

static int dwrite_outline_push_tag(struct dwrite_outline *outline, unsigned char tag)
{
    if (outline->tags.size < outline->tags.count + 1)
        return 1;

    outline->tags.values[outline->tags.count++] = tag;

    return 0;
}

static int dwrite_outline_push_points(struct dwrite_outline *outline, const D2D1_POINT_2F *points, unsigned int count)
{
    if (outline->points.size < outline->points.count + count)
        return 1;

    memcpy(&outline->points.values[outline->points.count], points, sizeof(*points) * count);
    outline->points.count += count;

    return 0;
}

static int decompose_beginfigure(struct decompose_context *ctxt)
{
    D2D1_POINT_2F point;
    int ret;

    if (!ctxt->move_to)
        return 0;

    ft_vector_to_d2d_point(&ctxt->origin, &point);
    if ((ret = dwrite_outline_push_tag(ctxt->outline, OUTLINE_BEGIN_FIGURE))) return ret;
    if ((ret = dwrite_outline_push_points(ctxt->outline, &point, 1))) return ret;

    ctxt->figure_started = TRUE;
    ctxt->move_to = FALSE;

    return 0;
}

static int decompose_move_to(const FT_Vector *to, void *user)
{
    struct decompose_context *ctxt = (struct decompose_context *)user;
    int ret;

    if (ctxt->figure_started)
    {
        if ((ret = dwrite_outline_push_tag(ctxt->outline, OUTLINE_END_FIGURE))) return ret;
        ctxt->figure_started = FALSE;
    }

    ctxt->move_to = TRUE;
    ctxt->origin = *to;
    return 0;
}

static int decompose_line_to(const FT_Vector *to, void *user)
{
    struct decompose_context *ctxt = (struct decompose_context *)user;
    D2D1_POINT_2F point;
    int ret;

    /* Special case for empty contours, in a way freetype returns them. */
    if (ctxt->move_to && !memcmp(to, &ctxt->origin, sizeof(*to)))
        return 0;

    ft_vector_to_d2d_point(to, &point);

    if ((ret = decompose_beginfigure(ctxt))) return ret;
    if ((ret = dwrite_outline_push_points(ctxt->outline, &point, 1))) return ret;
    if ((ret = dwrite_outline_push_tag(ctxt->outline, OUTLINE_LINE))) return ret;

    ctxt->origin = *to;
    return 0;
}

static int decompose_conic_to(const FT_Vector *control, const FT_Vector *to, void *user)
{
    struct decompose_context *ctxt = (struct decompose_context *)user;
    D2D1_POINT_2F points[3];
    FT_Vector cubic[3];
    int ret;

    if ((ret = decompose_beginfigure(ctxt)))
        return ret;

    /* convert from quadratic to cubic */

    /*
       The parametric eqn for a cubic Bezier is, from PLRM:
       r(t) = at^3 + bt^2 + ct + r0
       with the control points:
       r1 = r0 + c/3
       r2 = r1 + (c + b)/3
       r3 = r0 + c + b + a

       A quadratic Bezier has the form:
       p(t) = (1-t)^2 p0 + 2(1-t)t p1 + t^2 p2

       So equating powers of t leads to:
       r1 = 2/3 p1 + 1/3 p0
       r2 = 2/3 p1 + 1/3 p2
       and of course r0 = p0, r3 = p2
    */

    /* r1 = 1/3 p0 + 2/3 p1
       r2 = 1/3 p2 + 2/3 p1 */
    cubic[0].x = (2 * control->x + 1) / 3;
    cubic[0].y = (2 * control->y + 1) / 3;
    cubic[1] = cubic[0];
    cubic[0].x += (ctxt->origin.x + 1) / 3;
    cubic[0].y += (ctxt->origin.y + 1) / 3;
    cubic[1].x += (to->x + 1) / 3;
    cubic[1].y += (to->y + 1) / 3;
    cubic[2] = *to;

    ft_vector_to_d2d_point(cubic, points);
    ft_vector_to_d2d_point(cubic + 1, points + 1);
    ft_vector_to_d2d_point(cubic + 2, points + 2);
    if ((ret = dwrite_outline_push_points(ctxt->outline, points, 3))) return ret;
    if ((ret = dwrite_outline_push_tag(ctxt->outline, OUTLINE_BEZIER))) return ret;
    ctxt->origin = *to;
    return 0;
}

static int decompose_cubic_to(const FT_Vector *control1, const FT_Vector *control2,
    const FT_Vector *to, void *user)
{
    struct decompose_context *ctxt = (struct decompose_context *)user;
    D2D1_POINT_2F points[3];
    int ret;

    if ((ret = decompose_beginfigure(ctxt)))
        return ret;

    ft_vector_to_d2d_point(control1, points);
    ft_vector_to_d2d_point(control2, points + 1);
    ft_vector_to_d2d_point(to, points + 2);
    ctxt->origin = *to;

    if ((ret = dwrite_outline_push_points(ctxt->outline, points, 3))) return ret;
    if ((ret = dwrite_outline_push_tag(ctxt->outline, OUTLINE_BEZIER))) return ret;

    return 0;
}

static int decompose_outline(FT_Outline *ft_outline, struct dwrite_outline *outline)
{
    static const FT_Outline_Funcs decompose_funcs =
    {
        decompose_move_to,
        decompose_line_to,
        decompose_conic_to,
        decompose_cubic_to,
        0,
        0
    };
    struct decompose_context context = { 0 };
    int ret;

    context.outline = outline;

    ret = pFT_Outline_Decompose(ft_outline, &decompose_funcs, &context);

    if (!ret && context.figure_started)
        ret = dwrite_outline_push_tag(outline, OUTLINE_END_FIGURE);

    return ret;
}

static void embolden_glyph_outline(FT_Outline *outline, FLOAT emsize)
{
    FT_Pos strength;

    strength = pFT_MulDiv(emsize, 1 << 6, 24);
    pFT_Outline_EmboldenXY(outline, strength, 0);
}

static void embolden_glyph(FT_Glyph glyph, FLOAT emsize)
{
    FT_OutlineGlyph outline_glyph = (FT_OutlineGlyph)glyph;

    if (glyph->format != FT_GLYPH_FORMAT_OUTLINE)
        return;

    embolden_glyph_outline(&outline_glyph->outline, emsize);
}

static NTSTATUS get_glyph_outline(void *args)
{
    struct get_glyph_outline_params *params = args;
    FT_Face face = FaceFromObject(params->object);
    FT_Size size;

    if (!(size = freetype_set_face_size(face, params->emsize)))
        return STATUS_UNSUCCESSFUL;

    if (!pFT_Load_Glyph(face, params->glyph, FT_LOAD_NO_BITMAP))
    {
        FT_Outline *ft_outline = &face->glyph->outline;
        FT_Matrix m;

        if (params->outline->points.values)
        {
            if (params->simulations & DWRITE_FONT_SIMULATIONS_BOLD)
                embolden_glyph_outline(ft_outline, params->emsize);

            m.xx = 1 << 16;
            m.xy = params->simulations & DWRITE_FONT_SIMULATIONS_OBLIQUE ? (1 << 16) / 3 : 0;
            m.yx = 0;
            m.yy = -(1 << 16); /* flip Y axis */

            pFT_Outline_Transform(ft_outline, &m);

            decompose_outline(ft_outline, params->outline);
        }
        else
        {
            /* Intentionally overestimate numbers to keep it simple. */
            params->outline->points.count = ft_outline->n_points * 3;
            params->outline->tags.count = ft_outline->n_points + ft_outline->n_contours * 2;
        }
    }

    pFT_Done_Size(size);

    return STATUS_SUCCESS;
}

static NTSTATUS get_glyph_count(void *args)
{
    struct get_glyph_count_params *params = args;
    FT_Face face = FaceFromObject(params->object);

    *params->count = face ? face->num_glyphs : 0;

    return STATUS_SUCCESS;
}

static inline void ft_matrix_from_matrix_2x2(const MATRIX_2X2 *m, FT_Matrix *ft_matrix)
{
    ft_matrix->xx =  m->m11 * 0x10000;
    ft_matrix->xy = -m->m21 * 0x10000;
    ft_matrix->yx = -m->m12 * 0x10000;
    ft_matrix->yy =  m->m22 * 0x10000;
}

static BOOL get_glyph_transform(unsigned int simulations, const MATRIX_2X2 *m, FT_Matrix *ret)
{
    FT_Matrix ftm;

    ret->xx = 1 << 16;
    ret->xy = 0;
    ret->yx = 0;
    ret->yy = 1 << 16;

    /* Some fonts provide mostly bitmaps and very few outlines, for example for .notdef.
       Disable transform if that's the case. */
    if (!memcmp(m, &identity_2x2, sizeof(*m)) && !simulations)
        return FALSE;

    if (simulations & DWRITE_FONT_SIMULATIONS_OBLIQUE)
    {
        ftm.xx =  1 << 16;
        ftm.xy = (1 << 16) / 3;
        ftm.yx =  0;
        ftm.yy =  1 << 16;
        pFT_Matrix_Multiply(&ftm, ret);
    }

    ft_matrix_from_matrix_2x2(m, &ftm);
    pFT_Matrix_Multiply(&ftm, ret);

    return TRUE;
}

static NTSTATUS get_glyph_bbox(void *args)
{
    struct get_glyph_bbox_params *params = args;
    FT_Face face = FaceFromObject(params->object);
    FT_Glyph glyph = NULL;
    FT_BBox bbox = { 0 };
    BOOL needs_transform;
    FT_Matrix m;
    FT_Size size;

    SetRectEmpty(params->bbox);

    if (!(size = freetype_set_face_size(face, params->emsize)))
        return STATUS_UNSUCCESSFUL;

    needs_transform = FT_IS_SCALABLE(face) && get_glyph_transform(params->simulations, &params->m, &m);

    if (pFT_Load_Glyph(face, params->glyph, needs_transform ? (FT_LOAD_NO_BITMAP | FT_LOAD_NO_HINTING) : 0))
    {
        WARN("Failed to load glyph %u.\n", params->glyph);
        pFT_Done_Size(size);
        return STATUS_UNSUCCESSFUL;
    }

    pFT_Get_Glyph(face->glyph, &glyph);
    if (needs_transform)
    {
        if (params->simulations & DWRITE_FONT_SIMULATIONS_BOLD)
            embolden_glyph(glyph, params->emsize);

        /* Includes oblique and user transform. */
        pFT_Glyph_Transform(glyph, &m, NULL);
    }

    pFT_Glyph_Get_CBox(glyph, FT_GLYPH_BBOX_PIXELS, &bbox);
    pFT_Done_Glyph(glyph);
    pFT_Done_Size(size);

    /* flip Y axis */
    SetRect(params->bbox, bbox.xMin, -bbox.yMax, bbox.xMax, -bbox.yMin);

    return STATUS_SUCCESS;
}

static NTSTATUS freetype_get_aliased_glyph_bitmap(struct get_glyph_bitmap_params *params, FT_Glyph glyph)
{
    const RECT *bbox = &params->bbox;
    int width = bbox->right - bbox->left;
    int height = bbox->bottom - bbox->top;

    *params->is_1bpp = 1;

    if (glyph->format == FT_GLYPH_FORMAT_OUTLINE) {
        FT_OutlineGlyph outline = (FT_OutlineGlyph)glyph;
        const FT_Outline *src = &outline->outline;
        FT_Bitmap ft_bitmap;
        FT_Outline copy;

        ft_bitmap.width = width;
        ft_bitmap.rows = height;
        ft_bitmap.pitch = params->pitch;
        ft_bitmap.pixel_mode = FT_PIXEL_MODE_MONO;
        ft_bitmap.buffer = params->bitmap;

        /* Note: FreeType will only set 'black' bits for us. */
        if (pFT_Outline_New(library, src->n_points, src->n_contours, &copy) == 0) {
            pFT_Outline_Copy(src, &copy);
            pFT_Outline_Translate(&copy, -bbox->left << 6, bbox->bottom << 6);
            pFT_Outline_Get_Bitmap(library, &copy, &ft_bitmap);
            pFT_Outline_Done(library, &copy);
        }
    }
    else if (glyph->format == FT_GLYPH_FORMAT_BITMAP) {
        FT_Bitmap *ft_bitmap = &((FT_BitmapGlyph)glyph)->bitmap;
        BYTE *src = ft_bitmap->buffer, *dst = params->bitmap;
        int w = min(params->pitch, (ft_bitmap->width + 7) >> 3);
        int h = min(height, ft_bitmap->rows);

        while (h--) {
            memcpy(dst, src, w);
            src += ft_bitmap->pitch;
            dst += params->pitch;
        }
    }
    else
        FIXME("format %x not handled\n", glyph->format);

    return STATUS_SUCCESS;
}

static NTSTATUS freetype_get_aa_glyph_bitmap(struct get_glyph_bitmap_params *params, FT_Glyph glyph)
{
    const RECT *bbox = &params->bbox;
    int width = bbox->right - bbox->left;
    int height = bbox->bottom - bbox->top;

    *params->is_1bpp = 0;

    if (glyph->format == FT_GLYPH_FORMAT_OUTLINE) {
        FT_OutlineGlyph outline = (FT_OutlineGlyph)glyph;
        const FT_Outline *src = &outline->outline;
        FT_Bitmap ft_bitmap;
        FT_Outline copy;

        ft_bitmap.width = width;
        ft_bitmap.rows = height;
        ft_bitmap.pitch = params->pitch;
        ft_bitmap.pixel_mode = FT_PIXEL_MODE_GRAY;
        ft_bitmap.buffer = params->bitmap;

        /* Note: FreeType will only set 'black' bits for us. */
        if (pFT_Outline_New(library, src->n_points, src->n_contours, &copy) == 0) {
            pFT_Outline_Copy(src, &copy);
            pFT_Outline_Translate(&copy, -bbox->left << 6, bbox->bottom << 6);
            pFT_Outline_Get_Bitmap(library, &copy, &ft_bitmap);
            pFT_Outline_Done(library, &copy);
        }
    }
    else if (glyph->format == FT_GLYPH_FORMAT_BITMAP) {
        FT_Bitmap *ft_bitmap = &((FT_BitmapGlyph)glyph)->bitmap;
        BYTE *src = ft_bitmap->buffer, *dst = params->bitmap;
        int w = min(params->pitch, (ft_bitmap->width + 7) >> 3);
        int h = min(height, ft_bitmap->rows);

        while (h--) {
            memcpy(dst, src, w);
            src += ft_bitmap->pitch;
            dst += params->pitch;
        }

        *params->is_1bpp = 1;
    }
    else
    {
        FIXME("format %x not handled\n", glyph->format);
        return STATUS_NOT_IMPLEMENTED;
    }

    return STATUS_SUCCESS;
}

static NTSTATUS freetype_get_cleartype_glyph_bitmap(struct get_glyph_bitmap_params *params, FT_Glyph glyph)
{
    const RECT *bbox = &params->bbox;
    int width = bbox->right - bbox->left;
    int height = bbox->bottom - bbox->top;

    *params->is_1bpp = 0;

    if (glyph->format == FT_GLYPH_FORMAT_OUTLINE)
    {
        FT_Glyph bm = glyph;
        if (pFT_Glyph_To_Bitmap(&bm, FT_RENDER_MODE_LCD, NULL, 0) == 0)
        {
            FT_BitmapGlyph bmg = (FT_BitmapGlyph)bm;
            FT_Bitmap *ft_bitmap = &bmg->bitmap;
            int x_off = bmg->left - bbox->left;
            int y_off = -bmg->top - bbox->top;
            int sy;

            if (ft_bitmap->pixel_mode == FT_PIXEL_MODE_LCD)
            {
                int bm_width_px = ft_bitmap->width / 3;
                int sx_start = x_off < 0 ? -x_off : 0;
                int sx_end = bm_width_px < width - x_off ? bm_width_px : width - x_off;

                if (sx_end > sx_start)
                {
                    for (sy = 0; sy < (int)ft_bitmap->rows; sy++)
                    {
                        int dy = y_off + sy;
                        if (dy < 0 || dy >= height) continue;
                        memcpy(params->bitmap + dy * params->pitch + (x_off + sx_start) * 3,
                                ft_bitmap->buffer + sy * ft_bitmap->pitch + sx_start * 3,
                                (sx_end - sx_start) * 3);
                    }
                }
            }
            else if (ft_bitmap->pixel_mode == FT_PIXEL_MODE_GRAY)
            {
                int bm_width_px = ft_bitmap->width;
                int sx_start = x_off < 0 ? -x_off : 0;
                int sx_end = bm_width_px < width - x_off ? bm_width_px : width - x_off;
                int sx;

                for (sy = 0; sy < (int)ft_bitmap->rows; sy++)
                {
                    int dy = y_off + sy;
                    const BYTE *src_row;
                    BYTE *dst_row;
                    if (dy < 0 || dy >= height) continue;
                    src_row = ft_bitmap->buffer + sy * ft_bitmap->pitch;
                    dst_row = params->bitmap + dy * params->pitch;
                    for (sx = sx_start; sx < sx_end; sx++)
                    {
                        BYTE v = src_row[sx];
                        int dx = (x_off + sx) * 3;
                        dst_row[dx] = dst_row[dx + 1] = dst_row[dx + 2] = v;
                    }
                }
            }
            else if (ft_bitmap->pixel_mode == FT_PIXEL_MODE_MONO)
            {
                BYTE *src = ft_bitmap->buffer, *dst = params->bitmap;
                int w = min(params->pitch, (int)((ft_bitmap->width + 7) >> 3));
                int h = min(height, (int)ft_bitmap->rows);

                while (h--)
                {
                    memcpy(dst, src, w);
                    src += ft_bitmap->pitch;
                    dst += params->pitch;
                }
                *params->is_1bpp = 1;
            }

            if (bm != glyph)
                pFT_Done_Glyph(bm);
            return STATUS_SUCCESS;
        }
    }
    else if (glyph->format == FT_GLYPH_FORMAT_BITMAP)
    {
        FT_Bitmap *ft_bitmap = &((FT_BitmapGlyph)glyph)->bitmap;
        BYTE *src = ft_bitmap->buffer, *dst = params->bitmap;
        int w = min(params->pitch, (int)((ft_bitmap->width + 7) >> 3));
        int h = min(height, (int)ft_bitmap->rows);

        while (h--)
        {
            memcpy(dst, src, w);
            src += ft_bitmap->pitch;
            dst += params->pitch;
        }

        *params->is_1bpp = 1;
        return STATUS_SUCCESS;
    }

    return freetype_get_aa_glyph_bitmap(params, glyph);
}

static NTSTATUS get_glyph_bitmap(void *args)
{
    struct get_glyph_bitmap_params *params = args;
    FT_Face face = FaceFromObject(params->object);
    FT_Int32 load_flags;
    BOOL needs_transform;
    NTSTATUS ret = STATUS_UNSUCCESSFUL;
    FT_Glyph glyph;
    FT_Size size;
    FT_Matrix m;

    *params->is_1bpp = 0;

    if (!(size = freetype_set_face_size(face, params->emsize)))
        return STATUS_UNSUCCESSFUL;

    needs_transform = FT_IS_SCALABLE(face) && get_glyph_transform(params->simulations, &params->m, &m);
    load_flags = needs_transform ? (FT_LOAD_NO_BITMAP | FT_LOAD_NO_HINTING) : 0;
    if (params->cleartype && params->mode != DWRITE_RENDERING_MODE1_ALIASED)
        load_flags |= FT_LOAD_TARGET_LCD;

    if (!pFT_Load_Glyph(face, params->glyph, load_flags))
    {
        pFT_Get_Glyph(face->glyph, &glyph);

        if (needs_transform)
        {
            if (params->simulations & DWRITE_FONT_SIMULATIONS_BOLD)
                embolden_glyph(glyph, params->emsize);

            /* Includes oblique and user transform. */
            pFT_Glyph_Transform(glyph, &m, NULL);
        }

        if (params->mode == DWRITE_RENDERING_MODE1_ALIASED)
            ret = freetype_get_aliased_glyph_bitmap(params, glyph);
        else if (params->cleartype)
            ret = freetype_get_cleartype_glyph_bitmap(params, glyph);
        else
            ret = freetype_get_aa_glyph_bitmap(params, glyph);

        pFT_Done_Glyph(glyph);
    }

    pFT_Done_Size(size);

    return ret;
}

static NTSTATUS get_glyph_advance(void *args)
{
    struct get_glyph_advance_params *params = args;
    FT_Face face = FaceFromObject(params->object);
    FT_Size size;

    *params->advance = 0;
    *params->has_contours = FALSE;

    if (!(size = freetype_set_face_size(face, params->emsize)))
        return STATUS_UNSUCCESSFUL;

    if (!pFT_Load_Glyph(face, params->glyph, params->mode == DWRITE_MEASURING_MODE_NATURAL ? FT_LOAD_NO_HINTING : 0))
    {
        *params->advance = face->glyph->advance.x >> 6;
        *params->has_contours = freetype_glyph_has_contours(face);
    }

    pFT_Done_Size(size);

    return STATUS_SUCCESS;
}

#else /* SONAME_LIBFREETYPE */

static NTSTATUS process_attach(void *args)
{
    return STATUS_NOT_IMPLEMENTED;
}

static NTSTATUS process_detach(void *args)
{
    return STATUS_NOT_IMPLEMENTED;
}

static NTSTATUS create_font_object(void *args)
{
    return STATUS_NOT_IMPLEMENTED;
}

static NTSTATUS release_font_object(void *args)
{
    return STATUS_NOT_IMPLEMENTED;
}

static NTSTATUS get_glyph_outline(void *args)
{
    return STATUS_NOT_IMPLEMENTED;
}

static NTSTATUS get_glyph_count(void *args)
{
    return STATUS_NOT_IMPLEMENTED;
}

static NTSTATUS get_glyph_advance(void *args)
{
    struct get_glyph_advance_params *params = args;

    *params->has_contours = 0;
    *params->advance = 0;

    return STATUS_NOT_IMPLEMENTED;
}

static NTSTATUS get_glyph_bbox(void *args)
{
    struct get_glyph_bbox_params *params = args;
    SetRectEmpty(params->bbox);
    return STATUS_NOT_IMPLEMENTED;
}

static NTSTATUS get_glyph_bitmap(void *args)
{
    return STATUS_NOT_IMPLEMENTED;
}

static NTSTATUS get_design_glyph_metrics(void *args)
{
    return STATUS_NOT_IMPLEMENTED;
}

#endif /* SONAME_LIBFREETYPE */

/* FontConfig dynamic fallback (dlopen("libfontconfig.so.1"), zero configure.ac dependency) */
typedef struct _FcConfig FcConfig;
typedef struct _FcPattern FcPattern;
typedef struct _FcCharSet FcCharSet;
typedef unsigned char FcChar8;
typedef unsigned int FcChar32;
typedef int FcBool;

#define FC_RESULT_MATCH 0
#define FC_MATCH_PATTERN 0
#define FC_FAMILY_KEY "family"
#define FC_FILE_KEY "file"
#define FC_INDEX_KEY "index"
#define FC_WEIGHT_KEY "weight"
#define FC_SLANT_KEY "slant"
#define FC_WIDTH_KEY "width"
#define FC_CHARSET_KEY "charset"
#define FC_LANG_KEY "lang"
#define FC_COLOR_KEY "color"

#define FC_CACHE_SIZE 64
#define FC_MISS_CACHE_SIZE 64

struct fc_cache_entry
{
    unsigned int weight_bucket;
    unsigned int style;
    unsigned int is_emoji;
    char locale[16];
    char family[256];
    char file[512];
    unsigned int face_index;
    FcCharSet *charset;
    UINT64 last_used;
};

struct fc_miss_entry
{
    UINT32 ch;
    unsigned int is_emoji;
};

static pthread_mutex_t fc_mutex = PTHREAD_MUTEX_INITIALIZER;
static void *fc_handle = NULL;
static int fc_init_done = 0;
static int fc_available = 0;
static struct fc_cache_entry fc_cache[FC_CACHE_SIZE];
static struct fc_miss_entry fc_miss_cache[FC_MISS_CACHE_SIZE];
static unsigned int fc_miss_cursor = 0;
static UINT64 fc_clock = 0;

static FcBool (*pFcInit)(void);
static FcPattern *(*pFcPatternCreate)(void);
static void (*pFcPatternDestroy)(FcPattern *);
static FcBool (*pFcPatternAddCharSet)(FcPattern *, const char *, const FcCharSet *);
static FcBool (*pFcPatternAddInteger)(FcPattern *, const char *, int);
static FcBool (*pFcPatternAddString)(FcPattern *, const char *, const FcChar8 *);
static FcBool (*pFcPatternAddBool)(FcPattern *, const char *, FcBool);
static int (*pFcPatternGetString)(const FcPattern *, const char *, int, FcChar8 **);
static int (*pFcPatternGetInteger)(const FcPattern *, const char *, int, int *);
static int (*pFcPatternGetCharSet)(const FcPattern *, const char *, int, FcCharSet **);
static FcCharSet *(*pFcCharSetCreate)(void);
static void (*pFcCharSetDestroy)(FcCharSet *);
static FcBool (*pFcCharSetAddChar)(FcCharSet *, FcChar32);
static FcBool (*pFcCharSetHasChar)(const FcCharSet *, FcChar32);
static FcCharSet *(*pFcCharSetCopy)(FcCharSet *);
static FcBool (*pFcConfigSubstitute)(FcConfig *, FcPattern *, int);
static void (*pFcDefaultSubstitute)(FcPattern *);
static FcPattern *(*pFcFontMatch)(FcConfig *, FcPattern *, int *);

static int fc_init_locked(void)
{
    if (fc_init_done)
        return fc_available;
    fc_init_done = 1;

    fc_handle = dlopen("libfontconfig.so.1", RTLD_LAZY);
    if (!fc_handle)
        return 0;

#define LOAD_FC(f) if ((p##f = dlsym(fc_handle, #f)) == NULL) goto fail;
    LOAD_FC(FcInit)
    LOAD_FC(FcPatternCreate)
    LOAD_FC(FcPatternDestroy)
    LOAD_FC(FcPatternAddCharSet)
    LOAD_FC(FcPatternAddInteger)
    LOAD_FC(FcPatternAddString)
    LOAD_FC(FcPatternAddBool)
    LOAD_FC(FcPatternGetString)
    LOAD_FC(FcPatternGetInteger)
    LOAD_FC(FcPatternGetCharSet)
    LOAD_FC(FcCharSetCreate)
    LOAD_FC(FcCharSetDestroy)
    LOAD_FC(FcCharSetAddChar)
    LOAD_FC(FcCharSetHasChar)
    LOAD_FC(FcCharSetCopy)
    LOAD_FC(FcConfigSubstitute)
    LOAD_FC(FcDefaultSubstitute)
    LOAD_FC(FcFontMatch)
#undef LOAD_FC

    if (!pFcInit())
        goto fail;

    fc_available = 1;
    return 1;

fail:
    dlclose(fc_handle);
    fc_handle = NULL;
    fc_available = 0;
    return 0;
}

static void fontconfig_cleanup(void)
{
    unsigned int i;

    pthread_mutex_lock(&fc_mutex);
    for (i = 0; i < FC_CACHE_SIZE; ++i)
    {
        if (fc_cache[i].charset && pFcCharSetDestroy)
        {
            pFcCharSetDestroy(fc_cache[i].charset);
            fc_cache[i].charset = NULL;
        }
    }
    if (fc_handle)
    {
        dlclose(fc_handle);
        fc_handle = NULL;
    }
    fc_available = 0;
    fc_init_done = 0;
    pthread_mutex_unlock(&fc_mutex);
}

static int fc_weight_from_dwrite(UINT32 weight)
{
    if (weight <= 150) return 0;   /* FC_WEIGHT_THIN */
    if (weight <= 250) return 40;  /* FC_WEIGHT_EXTRALIGHT */
    if (weight <= 350) return 50;  /* FC_WEIGHT_LIGHT */
    if (weight <= 450) return 80;  /* FC_WEIGHT_REGULAR */
    if (weight <= 550) return 100; /* FC_WEIGHT_MEDIUM */
    if (weight <= 650) return 180; /* FC_WEIGHT_SEMIBOLD */
    if (weight <= 750) return 200; /* FC_WEIGHT_BOLD */
    if (weight <= 850) return 205; /* FC_WEIGHT_EXTRABOLD */
    return 210;                    /* FC_WEIGHT_BLACK */
}

static int fc_slant_from_dwrite(UINT32 style)
{
    if (style == DWRITE_FONT_STYLE_ITALIC) return 100;  /* FC_SLANT_ITALIC */
    if (style == DWRITE_FONT_STYLE_OBLIQUE) return 110; /* FC_SLANT_OBLIQUE */
    return 0;                                           /* FC_SLANT_ROMAN */
}

static int fc_width_from_dwrite(UINT32 stretch)
{
    switch (stretch)
    {
        case DWRITE_FONT_STRETCH_ULTRA_CONDENSED: return 50;
        case DWRITE_FONT_STRETCH_EXTRA_CONDENSED: return 63;
        case DWRITE_FONT_STRETCH_CONDENSED:       return 75;
        case DWRITE_FONT_STRETCH_SEMI_CONDENSED:  return 87;
        case DWRITE_FONT_STRETCH_SEMI_EXPANDED:   return 113;
        case DWRITE_FONT_STRETCH_EXPANDED:        return 125;
        case DWRITE_FONT_STRETCH_EXTRA_EXPANDED:  return 150;
        case DWRITE_FONT_STRETCH_ULTRA_EXPANDED:  return 200;
        default:                                  return 100;
    }
}

static FcPattern *fc_match_char_locked(UINT32 ch, int fc_weight, int fc_slant, int fc_width,
        const char *locale, int prefer_emoji)
{
    FcPattern *pat, *match;
    FcCharSet *cs, *match_cs = NULL;
    int result = 0;

    if (!(cs = pFcCharSetCreate()))
        return NULL;
    pFcCharSetAddChar(cs, ch);

    if (!(pat = pFcPatternCreate()))
    {
        pFcCharSetDestroy(cs);
        return NULL;
    }

    pFcPatternAddCharSet(pat, FC_CHARSET_KEY, cs);
    pFcPatternAddInteger(pat, FC_WEIGHT_KEY, fc_weight);
    pFcPatternAddInteger(pat, FC_SLANT_KEY, fc_slant);
    pFcPatternAddInteger(pat, FC_WIDTH_KEY, fc_width);
    if (locale && *locale)
        pFcPatternAddString(pat, FC_LANG_KEY, (const FcChar8 *)locale);
    if (prefer_emoji)
    {
        pFcPatternAddString(pat, FC_FAMILY_KEY, (const FcChar8 *)"emoji");
        pFcPatternAddBool(pat, FC_COLOR_KEY, 1);
    }

    pFcConfigSubstitute(NULL, pat, FC_MATCH_PATTERN);
    pFcDefaultSubstitute(pat);
    match = pFcFontMatch(NULL, pat, &result);

    pFcPatternDestroy(pat);
    pFcCharSetDestroy(cs);

    if (!match || result != FC_RESULT_MATCH)
    {
        if (match) pFcPatternDestroy(match);
        return NULL;
    }

    if (pFcPatternGetCharSet(match, FC_CHARSET_KEY, 0, &match_cs) != FC_RESULT_MATCH ||
            !match_cs || !pFcCharSetHasChar(match_cs, ch))
    {
        pFcPatternDestroy(match);
        return NULL;
    }

    return match;
}

static void fc_copy_str(char *dst, size_t dst_size, const char *src)
{
    size_t len;
    if (!dst || !dst_size) return;
    if (!src) { dst[0] = 0; return; }
    len = strlen(src);
    if (len >= dst_size) len = dst_size - 1;
    memcpy(dst, src, len);
    dst[len] = 0;
}

static NTSTATUS fontconfig_fallback(void *args)
{
    struct fontconfig_fallback_params *params = args;
    unsigned int i, slot = 0, is_emoji;
    int fc_weight, fc_slant, fc_width, face_index = 0;
    char locale_key[16];
    FcPattern *match = NULL;
    FcChar8 *fc_family = NULL, *fc_file = NULL;
    FcCharSet *match_cs = NULL;
    UINT64 oldest = ~(UINT64)0;

    if (!params || !params->family || !params->family_size)
        return STATUS_INVALID_PARAMETER;

    params->family[0] = 0;
    if (params->file && params->file_size)
        params->file[0] = 0;
    if (params->face_index)
        *params->face_index = 0;

    is_emoji = (params->ch >= 0x1F300 && params->ch <= 0x1FAFF) ||
               (params->ch >= 0x1F000 && params->ch <= 0x1F0FF);
    fc_weight = fc_weight_from_dwrite(params->weight);
    fc_slant = fc_slant_from_dwrite(params->style);
    fc_width = fc_width_from_dwrite(params->stretch);

    memset(locale_key, 0, sizeof(locale_key));
    if (params->locale && *params->locale)
        fc_copy_str(locale_key, sizeof(locale_key), params->locale);

    pthread_mutex_lock(&fc_mutex);

    if (!fc_init_locked())
    {
        pthread_mutex_unlock(&fc_mutex);
        return STATUS_NOT_FOUND;
    }

    /* Fast path: check cached matched fonts by charset coverage. */
    for (i = 0; i < FC_CACHE_SIZE; ++i)
    {
        struct fc_cache_entry *e = &fc_cache[i];
        if (!e->charset)
            continue;
        if (e->weight_bucket == (unsigned int)fc_weight &&
            e->style == params->style &&
            e->is_emoji == is_emoji &&
            !strcmp(e->locale, locale_key) &&
            pFcCharSetHasChar(e->charset, params->ch))
        {
            e->last_used = ++fc_clock;
            fc_copy_str(params->family, params->family_size, e->family);
            if (params->file && params->file_size)
                fc_copy_str(params->file, params->file_size, e->file);
            if (params->face_index)
                *params->face_index = e->face_index;
            pthread_mutex_unlock(&fc_mutex);
            return STATUS_SUCCESS;
        }
    }

    /* Check negative miss cache. */
    for (i = 0; i < FC_MISS_CACHE_SIZE; ++i)
    {
        if (fc_miss_cache[i].ch == params->ch && fc_miss_cache[i].is_emoji == is_emoji)
        {
            pthread_mutex_unlock(&fc_mutex);
            return STATUS_NOT_FOUND;
        }
    }

    if (is_emoji)
        match = fc_match_char_locked(params->ch, fc_weight, fc_slant, fc_width, locale_key, 1);
    if (!match)
        match = fc_match_char_locked(params->ch, fc_weight, fc_slant, fc_width, locale_key, 0);

    if (!match || pFcPatternGetString(match, FC_FAMILY_KEY, 0, &fc_family) != FC_RESULT_MATCH || !fc_family)
    {
        if (match) pFcPatternDestroy(match);
        fc_miss_cache[fc_miss_cursor].ch = params->ch;
        fc_miss_cache[fc_miss_cursor].is_emoji = is_emoji;
        fc_miss_cursor = (fc_miss_cursor + 1) % FC_MISS_CACHE_SIZE;
        pthread_mutex_unlock(&fc_mutex);
        return STATUS_NOT_FOUND;
    }

    pFcPatternGetString(match, FC_FILE_KEY, 0, &fc_file);
    pFcPatternGetInteger(match, FC_INDEX_KEY, 0, &face_index);
    pFcPatternGetCharSet(match, FC_CHARSET_KEY, 0, &match_cs);

    fc_copy_str(params->family, params->family_size, (const char *)fc_family);
    if (params->file && params->file_size && fc_file)
        fc_copy_str(params->file, params->file_size, (const char *)fc_file);
    if (params->face_index)
        *params->face_index = (UINT32)face_index;

    /* Insert into bounded LRU cache. */
    if (match_cs)
    {
        for (i = 0; i < FC_CACHE_SIZE; ++i)
        {
            if (!fc_cache[i].charset)
            {
                slot = i;
                break;
            }
            if (fc_cache[i].last_used < oldest)
            {
                oldest = fc_cache[i].last_used;
                slot = i;
            }
        }
        if (fc_cache[slot].charset)
            pFcCharSetDestroy(fc_cache[slot].charset);
        fc_cache[slot].weight_bucket = (unsigned int)fc_weight;
        fc_cache[slot].style = params->style;
        fc_cache[slot].is_emoji = is_emoji;
        memcpy(fc_cache[slot].locale, locale_key, sizeof(locale_key));
        fc_copy_str(fc_cache[slot].family, sizeof(fc_cache[slot].family), (const char *)fc_family);
        fc_copy_str(fc_cache[slot].file, sizeof(fc_cache[slot].file), (const char *)fc_file);
        fc_cache[slot].face_index = (unsigned int)face_index;
        fc_cache[slot].charset = pFcCharSetCopy(match_cs);
        fc_cache[slot].last_used = ++fc_clock;
    }

    pFcPatternDestroy(match);
    pthread_mutex_unlock(&fc_mutex);
    return STATUS_SUCCESS;
}

const unixlib_entry_t __wine_unix_call_funcs[] =
{
    process_attach,
    process_detach,
    create_font_object,
    release_font_object,
    get_glyph_outline,
    get_glyph_count,
    get_glyph_advance,
    get_glyph_bbox,
    get_glyph_bitmap,
    get_design_glyph_metrics,
    fontconfig_fallback,
};

C_ASSERT( ARRAYSIZE(__wine_unix_call_funcs) == unix_funcs_count );

#ifdef _WIN64

typedef ULONG PTR32;

static NTSTATUS wow64_create_font_object(void *args)
{
    struct
    {
        PTR32 data;
        UINT64 size;
        ULONG index;
        PTR32 object;
    } const *params32 = args;
    struct create_font_object_params params =
    {
        ULongToPtr(params32->data),
        params32->size,
        params32->index,
        ULongToPtr(params32->object),
    };

    return create_font_object(&params);
}

static NTSTATUS wow64_release_font_object(void *args)
{
    struct
    {
        UINT64 object;
    } const *params32 = args;
    struct release_font_object_params params =
    {
        params32->object
    };

    return release_font_object(&params);
}

struct dwrite_outline32
{
    struct
    {
        PTR32 values;
        ULONG count;
        ULONG size;
    } tags;

    struct
    {
        PTR32 values;
        ULONG count;
        ULONG size;
    } points;
};

static NTSTATUS wow64_get_glyph_outline(void *args)
{
    struct
    {
        UINT64 object;
        ULONG simulations;
        ULONG glyph;
        float emsize;
        PTR32 outline;
    } const *params32 = args;
    struct dwrite_outline32 *outline32 = ULongToPtr(params32->outline);
    struct dwrite_outline outline =
    {
        .tags.values = ULongToPtr(outline32->tags.values),
        .tags.count = outline32->tags.count,
        .tags.size = outline32->tags.size,
        .points.values = ULongToPtr(outline32->points.values),
        .points.count = outline32->points.count,
        .points.size = outline32->points.size,
    };

    struct get_glyph_outline_params params =
    {
        params32->object,
        params32->simulations,
        params32->glyph,
        params32->emsize,
        &outline,
    };
    NTSTATUS status;

    status = get_glyph_outline(&params);
    outline32->points.count = outline.points.count;
    outline32->tags.count = outline.tags.count;
    return status;
}

static NTSTATUS wow64_get_glyph_count(void *args)
{
    struct
    {
        UINT64 object;
        PTR32 count;
    } const *params32 = args;
    struct get_glyph_count_params params =
    {
        params32->object,
        ULongToPtr(params32->count),
    };

    return get_glyph_count(&params);
}

static NTSTATUS wow64_get_glyph_advance(void *args)
{
    struct
    {
        UINT64 object;
        ULONG glyph;
        ULONG mode;
        float emsize;
        PTR32 advance;
        PTR32 has_contours;
    } const *params32 = args;
    struct get_glyph_advance_params params =
    {
        params32->object,
        params32->glyph,
        params32->mode,
        params32->emsize,
        ULongToPtr(params32->advance),
        ULongToPtr(params32->has_contours),
    };

    return get_glyph_advance(&params);
}

static NTSTATUS wow64_get_glyph_bbox(void *args)
{
    struct
    {
        UINT64 object;
        ULONG simulations;
        ULONG glyph;
        float emsize;
        MATRIX_2X2 m;
        PTR32 bbox;
    } const *params32 = args;
    struct get_glyph_bbox_params params =
    {
        params32->object,
        params32->simulations,
        params32->glyph,
        params32->emsize,
        params32->m,
        ULongToPtr(params32->bbox),
    };

    return get_glyph_bbox(&params);
}

static NTSTATUS wow64_get_glyph_bitmap(void *args)
{
    struct
    {
        UINT64 object;
        ULONG simulations;
        ULONG glyph;
        ULONG mode;
        ULONG cleartype;
        float emsize;
        MATRIX_2X2 m;
        RECT bbox;
        int pitch;
        PTR32 bitmap;
        PTR32 is_1bpp;
    } const *params32 = args;
    struct get_glyph_bitmap_params params =
    {
        params32->object,
        params32->simulations,
        params32->glyph,
        params32->mode,
        params32->cleartype,
        params32->emsize,
        params32->m,
        params32->bbox,
        params32->pitch,
        ULongToPtr(params32->bitmap),
        ULongToPtr(params32->is_1bpp),
    };

    return get_glyph_bitmap(&params);
}

static NTSTATUS wow64_get_design_glyph_metrics(void *args)
{
    struct
    {
        UINT64 object;
        ULONG simulations;
        ULONG glyph;
        ULONG upem;
        ULONG ascent;
        PTR32 metrics;
    } const *params32 = args;
    struct get_design_glyph_metrics_params params =
    {
        params32->object,
        params32->simulations,
        params32->glyph,
        params32->upem,
        params32->ascent,
        ULongToPtr(params32->metrics),
    };

    return get_design_glyph_metrics(&params);
};

static NTSTATUS wow64_fontconfig_fallback(void *args)
{
    struct
    {
        UINT32 ch;
        UINT32 weight;
        UINT32 style;
        UINT32 stretch;
        PTR32 locale;
        PTR32 family;
        UINT32 family_size;
        PTR32 file;
        UINT32 file_size;
        PTR32 face_index;
    } const *params32 = args;
    struct fontconfig_fallback_params params =
    {
        params32->ch,
        params32->weight,
        params32->style,
        params32->stretch,
        ULongToPtr(params32->locale),
        ULongToPtr(params32->family),
        params32->family_size,
        ULongToPtr(params32->file),
        params32->file_size,
        ULongToPtr(params32->face_index),
    };

    return fontconfig_fallback(&params);
}

const unixlib_entry_t __wine_unix_call_wow64_funcs[] =
{
    process_attach,
    process_detach,
    wow64_create_font_object,
    wow64_release_font_object,
    wow64_get_glyph_outline,
    wow64_get_glyph_count,
    wow64_get_glyph_advance,
    wow64_get_glyph_bbox,
    wow64_get_glyph_bitmap,
    wow64_get_design_glyph_metrics,
    wow64_fontconfig_fallback,
};

C_ASSERT( ARRAYSIZE(__wine_unix_call_wow64_funcs) == unix_funcs_count );

#endif  /* _WIN64 */
