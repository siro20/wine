/*
 * Wine D3D9 DRI backend interface
 *
 * Copyright 2019 Patrick Rudolph
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

#ifndef __WINE_D3D9_NINE_BACKEND_H
#define __WINE_D3D9_NINE_BACKEND_H

#ifndef __WINE_CONFIG_H
# error You must include config.h to use this header
#endif

#include <X11/Xlib.h>
#include <windef.h>

struct DRIBackend;
struct PRESENTpriv;
struct PRESENTPixmapPriv;
struct DRIpriv;
struct DRI2PixmapPriv;
typedef struct PRESENTPriv PRESENTpriv;
typedef struct PRESENTPixmapPriv PRESENTPixmapPriv;

struct D3DWindowBuffer
{
    PRESENTPixmapPriv *present_pixmap_priv;

    struct DRI2PixmapPriv *dri2_pixmap_priv;
};

BOOL DRIBackendOpen(Display *dpy, int screen, struct DRIBackend **dri_backend);

void DRIBackendClose(struct DRIBackend *dri_backend);

int DRIBackendFd(struct DRIBackend *dri_backend);

BOOL DRIBackendCheckExtension(Display *dpy);

BOOL DRIBackendD3DWindowBufferFromDmaBuf(struct DRIBackend *dri_backend,
        PRESENTpriv *present_priv, struct DRIpriv *dri_priv,
        int dmaBufFd, int width, int height, int stride, int depth,
        int bpp, struct D3DWindowBuffer **out);

BOOL DRIBackendHelperCopyFront(struct DRIBackend *dri_backend, PRESENTPixmapPriv *present_pixmap_priv);

BOOL DRIBackendInit(struct DRIBackend *dri_backend, struct DRIpriv **dri_priv);

void DRIBackendDestroy(struct DRIBackend *dri_backend, struct DRIpriv *dri_priv);

void DRIBackendPresentPixmap(struct DRIBackend *dri_backend, struct DRIpriv *dri_priv,
        struct DRI2PixmapPriv *dri2_pixmap_priv);

void DRIBackendDestroyPixmap(struct DRIBackend *dri_backend, struct DRIpriv *dri_priv,
        struct DRI2PixmapPriv *dri2_pixmap_priv);

#endif /* __WINE_D3D9_NINE_BACKEND_H */
