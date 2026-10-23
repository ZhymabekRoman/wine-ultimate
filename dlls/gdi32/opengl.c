/*
 * OpenGL function forwarding to the display driver
 *
 * Copyright (c) 1999 Lionel Ulmer
 * Copyright (c) 2005 Raphael Junqueira
 * Copyright (c) 2006 Roderick Colenbrander
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
#include <string.h>
#include <stdlib.h>

#include "windef.h"
#include "winbase.h"
#include "wingdi.h"
#include "winerror.h"
#include "winternl.h"
#include "winnt.h"


static HMODULE opengl32;
static INT (WINAPI *wglChoosePixelFormat)(HDC,const PIXELFORMATDESCRIPTOR *);
static INT (WINAPI *wglDescribePixelFormat)(HDC,INT,UINT,PIXELFORMATDESCRIPTOR*);
static INT (WINAPI *wglGetPixelFormat)(HDC);
static BOOL (WINAPI *wglSetPixelFormat)(HDC,INT,const PIXELFORMATDESCRIPTOR*);
static BOOL (WINAPI *wglSwapBuffers)(HDC);

/* Resolve the opengl32 forwarding at process init (gdi32 DllMain), not lazily per
 * call. wined3d's GL init calls ChoosePixelFormat while holding the wined3d
 * critical section; loading opengl32 from there takes loader_section under a
 * client lock and forms an AB-BA cycle with a thread that enters the loader
 * before taking wined3d_cs (the other edge is d2d1's implicit device, see
 * dlls/d2d1). Never take the loader lock from a path that can be entered under a
 * client lock. When opengl32 is absent the functions keep failing as before. */
void opengl_init( void )
{
    if (!(opengl32 = LoadLibraryW( L"opengl32.dll" )))
        return;

    wglChoosePixelFormat = (void *)GetProcAddress( opengl32, "wglChoosePixelFormat" );
    wglDescribePixelFormat = (void *)GetProcAddress( opengl32, "wglDescribePixelFormat" );
    wglGetPixelFormat = (void *)GetProcAddress( opengl32, "wglGetPixelFormat" );
    wglSetPixelFormat = (void *)GetProcAddress( opengl32, "wglSetPixelFormat" );
    wglSwapBuffers = (void *)GetProcAddress( opengl32, "wglSwapBuffers" );
}

/******************************************************************************
 *		ChoosePixelFormat (GDI32.@)
 */
INT WINAPI ChoosePixelFormat( HDC hdc, const PIXELFORMATDESCRIPTOR *pfd )
{
    if (!wglChoosePixelFormat) return 0;
    return wglChoosePixelFormat( hdc, pfd );
}

/******************************************************************************
 *		DescribePixelFormat (GDI32.@)
 */
INT WINAPI DescribePixelFormat( HDC hdc, INT fmt, UINT size, PIXELFORMATDESCRIPTOR *pfd )
{
    if (!wglDescribePixelFormat) return 0;
    return wglDescribePixelFormat( hdc, fmt, size, pfd );
}

/******************************************************************************
 *		GetPixelFormat (GDI32.@)
 */
INT WINAPI GetPixelFormat( HDC hdc )
{
    if (!wglGetPixelFormat) return 0;
    return wglGetPixelFormat( hdc );
}

/******************************************************************************
 *		SetPixelFormat (GDI32.@)
 */
BOOL WINAPI SetPixelFormat( HDC hdc, INT fmt, const PIXELFORMATDESCRIPTOR *pfd )
{
    if (!wglSetPixelFormat) return FALSE;
    return wglSetPixelFormat( hdc, fmt, pfd );
}

/******************************************************************************
 *		SwapBuffers (GDI32.@)
 */
BOOL WINAPI SwapBuffers( HDC hdc )
{
    if (!wglSwapBuffers) return FALSE;
    return wglSwapBuffers( hdc );
}
