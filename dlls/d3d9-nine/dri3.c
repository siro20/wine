/*
 * Wine DRI3 interface
 *
 * Copyright 2014-2015 Axel Davy
 * Copyright 2015 Patrick Rudolph
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
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(d3d9nine);

#include <d3dadapter/drm.h>

#include "xcb_priv.h"

#include <fcntl.h>
#include <unistd.h>

#include <X11/Xlib-xcb.h>
#include <xcb/dri3.h>
#include <xcb/present.h>

#include "winbase.h"

static BOOL DRI3Open(Display *dpy, int screen, int *device_fd)
{
    xcb_dri3_open_cookie_t cookie;
    xcb_dri3_open_reply_t *reply;
    xcb_connection_t *xcb_connection = XGetXCBConnection(dpy);
    int fd;
    Window root = RootWindow(dpy, screen);

    cookie = xcb_dri3_open(xcb_connection, root, 0);

    reply = xcb_dri3_open_reply(xcb_connection, cookie, NULL);
    if (!reply)
        return FALSE;

    if (reply->nfd != 1)
    {
        free(reply);
        return FALSE;
    }

    fd = xcb_dri3_open_reply_fds(xcb_connection, reply)[0];
    fcntl(fd, F_SETFD, FD_CLOEXEC);

    *device_fd = fd;
    free(reply);

    return TRUE;
}

static BOOL DRI3CheckExtension(Display *dpy, int major, int minor)
{
    xcb_connection_t *xcb_connection = XGetXCBConnection(dpy);
    xcb_dri3_query_version_cookie_t dri3_cookie;
    xcb_dri3_query_version_reply_t *dri3_reply;
    xcb_generic_error_t *error;
    const xcb_query_extension_reply_t *extension;
    int fd;

    xcb_prefetch_extension_data(xcb_connection, &xcb_dri3_id);

    extension = xcb_get_extension_data(xcb_connection, &xcb_dri3_id);
    if (!(extension && extension->present))
    {
        ERR("DRI3 extension is not present\n");
        return FALSE;
    }

    dri3_cookie = xcb_dri3_query_version(xcb_connection, major, minor);

    dri3_reply = xcb_dri3_query_version_reply(xcb_connection, dri3_cookie, &error);
    if (!dri3_reply)
    {
        free(error);
        ERR("Issue getting requested version of DRI3: %d,%d\n", major, minor);
        return FALSE;
    }

    if (!DRI3Open(dpy, DefaultScreen(dpy), &fd))
    {
        ERR("DRI3 advertised, but not working\n");
        return FALSE;
    }
    close(fd);

    TRACE("DRI3 version %d,%d found. %d %d requested\n", major, minor,
            (int)dri3_reply->major_version, (int)dri3_reply->minor_version);
    free(dri3_reply);

    return TRUE;
}

static BOOL DRI3CheckSupport(Display *dpy)
{
    return DRI3CheckExtension(dpy, 1, 0);
}

static BOOL DRI3PixmapFromDmaBuf(PRESENTpriv *present_priv, int fd, int width, int height,
        int stride, int depth, int bpp, Pixmap *pixmap)
{
    xcb_connection_t *xcb_connection = present_priv->xcb_connection;
    xcb_void_cookie_t cookie;
    xcb_generic_error_t *error;

    cookie = xcb_dri3_pixmap_from_buffer_checked(xcb_connection,
            (*pixmap = xcb_generate_id(xcb_connection)), present_priv->root, 0,
            width, height, stride, depth, bpp, fd);

    error = xcb_request_check(xcb_connection, cookie); /* performs a flush */
    if (error)
    {
        ERR("Error using DRI3 to convert a DmaBufFd to pixmap\n");
        return FALSE;
    }
    return TRUE;
}

/* unused */
#if 0
static BOOL DRI3DmaBufFromPixmap(Display *dpy, Pixmap pixmap, int *fd, int *width, int *height,
        int *stride, int *depth, int *bpp)
{
    xcb_connection_t *xcb_connection = XGetXCBConnection(dpy);
    xcb_dri3_buffer_from_pixmap_cookie_t bp_cookie;
    xcb_dri3_buffer_from_pixmap_reply_t  *bp_reply;

    bp_cookie = xcb_dri3_buffer_from_pixmap(xcb_connection, pixmap);
    bp_reply = xcb_dri3_buffer_from_pixmap_reply(xcb_connection, bp_cookie, NULL);
    if (!bp_reply)
        return FALSE;
    *fd = xcb_dri3_buffer_from_pixmap_reply_fds(xcb_connection, bp_reply)[0];
    *width = bp_reply->width;
    *height = bp_reply->height;
    *stride = bp_reply->stride;
    *depth = bp_reply->depth;
    *bpp = bp_reply->depth;
    return TRUE;
}
#endif

static BOOL DRI3PRESENTPixmapInit(PRESENTpriv *present_priv,
        int fd, int width, int height, int stride, int depth,
        int bpp, PRESENTPixmapPriv **present_pixmap_priv)
{
    Pixmap pixmap;

    xcb_get_geometry_cookie_t cookie;
    xcb_get_geometry_reply_t *reply;

    if (!DRI3PixmapFromDmaBuf(present_priv, fd, width, height,
           stride, depth, bpp, &pixmap))
    {
        ERR("DRI3PixmapFromDmaBuf failed\n");
        return D3DERR_DRIVERINTERNALERROR;
    }

    cookie = xcb_get_geometry(present_priv->xcb_connection, pixmap);
    reply = xcb_get_geometry_reply(present_priv->xcb_connection, cookie, NULL);

    if (!reply)
        return FALSE;

    *present_pixmap_priv = (PRESENTPixmapPriv *) HeapAlloc(GetProcessHeap(),
            HEAP_ZERO_MEMORY, sizeof(PRESENTPixmapPriv));

    if (!*present_pixmap_priv)
    {
        free(reply);
        return FALSE;
    }
    EnterCriticalSection(&present_priv->mutex_present);

    (*present_pixmap_priv)->released = TRUE;
    (*present_pixmap_priv)->pixmap = pixmap;
    (*present_pixmap_priv)->present_priv = present_priv;
    (*present_pixmap_priv)->next = present_priv->first_present_priv;
    (*present_pixmap_priv)->width = reply->width;
    (*present_pixmap_priv)->height = reply->height;
    (*present_pixmap_priv)->depth = reply->depth;
    free(reply);

    present_priv->last_serial_given++;
    (*present_pixmap_priv)->serial = present_priv->last_serial_given;
    present_priv->first_present_priv = *present_pixmap_priv;

    LeaveCriticalSection(&present_priv->mutex_present);
    return TRUE;
}

static BOOL DRI3PRESENTHelperCopyFront(Display *dpy, PRESENTPixmapPriv *present_pixmap_priv)
{
    PRESENTpriv *present_priv = present_pixmap_priv->present_priv;
    xcb_void_cookie_t cookie;
    xcb_generic_error_t *error;
    uint32_t v = 0;
    xcb_gcontext_t gc;

    EnterCriticalSection(&present_priv->mutex_present);

    if (!present_priv->window)
    {
        LeaveCriticalSection(&present_priv->mutex_present);
        return FALSE;
    }

    gc = xcb_generate_id(present_priv->xcb_connection);
    xcb_create_gc(present_priv->xcb_connection, gc, present_priv->window,
             XCB_GC_GRAPHICS_EXPOSURES, &v);

    cookie = xcb_copy_area_checked(present_priv->xcb_connection,
             present_priv->window, present_pixmap_priv->pixmap, gc,
             0, 0, 0, 0, present_pixmap_priv->width, present_pixmap_priv->height);

    error = xcb_request_check(present_priv->xcb_connection, cookie);
    xcb_free_gc(present_priv->xcb_connection, gc);
    LeaveCriticalSection(&present_priv->mutex_present);
    return (error != NULL);
}

static BOOL DRI3Init(Display *dpy, PRESENTpriv **priv)
{
    *priv = (PRESENTpriv *) HeapAlloc(GetProcessHeap(),
            HEAP_ZERO_MEMORY, sizeof(PRESENTpriv));

    if (!*priv)
        return FALSE;

    return TRUE;
}

static void DRI3Destroy(PRESENTpriv *priv)
{
    HeapFree(GetProcessHeap(), 0, priv);
}

static void DRI3DestroyPixmapContent(Display *dpy,
        PRESENTPixmapPriv *present_pixmap)
{
}

static void DRI3PRESENTPixmap(PRESENTPixmapPriv *present_pixmap_priv)
{
}

struct PRESENT_DRI_func DRI3func = {
    .name = "DRI3",
    .Init = DRI3Init,
    .Open = DRI3Open,
    .CheckSupport = DRI3CheckSupport,
    .Destroy = DRI3Destroy,
    .PRESENTPixmapInit = DRI3PRESENTPixmapInit,
    .DestroyPixmapContent = DRI3DestroyPixmapContent,
    .PRESENTPixmap = DRI3PRESENTPixmap,
    .PRESENTHelperCopyFront = DRI3PRESENTHelperCopyFront,
};

