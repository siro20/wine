/*
 * Wine D3D9 DRI backend interface
 *
 * Copyright 2019 Patrick Rudolph
 *
 * Copyright 2014-2015 Axel Davy
 * Copyright 2015-2019 Patrick Rudolph
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
#include "wine/port.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(d3d9nine);

#include <X11/Xlib-xcb.h>
#include <windows.h>
#include <stdlib.h>

#include "dri3.h"
#include "backend.h"

struct DRIBackend {
    Display *dpy;
    int fd;
    int screen;
};

#ifdef D3D9NINE_DRI2
extern int is_dri2_fallback;
#endif

BOOL DRIBackendOpen(Display *dpy, int screen, struct DRIBackend **dri_backend)
{
    TRACE("dpy=%p screen=%d dri_backend=%p\n", dpy, screen, dri_backend);

    if (!dri_backend)
        return FALSE;

    *dri_backend = HeapAlloc(GetProcessHeap(), 0, sizeof(struct DRIBackend));
    if (!*dri_backend)
        return FALSE;

    (*dri_backend)->dpy = dpy;
    (*dri_backend)->screen = screen;

    if (DRI3Open(dpy, screen, &((*dri_backend)->fd)))
        return TRUE;

    ERR("DRI3Open failed (fd=%d)\n", (*dri_backend)->fd);

#ifdef D3D9NINE_DRI2
    if (is_dri2_fallback && DRI2FallbackOpen(dpy, screen, &((*dri_backend)->fd)))
        return TRUE;
    ERR("DRI2Open failed (fd=%d)\n", (*dri_backend)->fd);
#endif

    HeapFree(GetProcessHeap(), 0, dri_backend);
    return FALSE;
}

int DRIBackendFd(struct DRIBackend *dri_backend)
{
    TRACE("dri_backend=%p\n", dri_backend);

    return dri_backend ? dri_backend->fd : -1;
}

void DRIBackendClose(struct DRIBackend *dri_backend)
{
    TRACE("dri_backend=%p\n", dri_backend);

    if (dri_backend)
    {
        HeapFree(GetProcessHeap(), 0, dri_backend);
    }
}