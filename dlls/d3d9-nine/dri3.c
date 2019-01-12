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

#include "dri3.h"

#include <fcntl.h>
#include <unistd.h>

#include <X11/Xlib-xcb.h>
#include <xcb/dri3.h>
#include <stdlib.h>

#include "winbase.h"

BOOL DRI3CheckExtension(Display *dpy, int major, int minor)
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

BOOL DRI3Open(Display *dpy, int screen, int *device_fd)
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

BOOL DRI3PixmapFromDmaBuf(Display *dpy, int screen, int fd, int width, int height,
        int stride, int depth, int bpp, Pixmap *pixmap)
{
    xcb_connection_t *xcb_connection = XGetXCBConnection(dpy);
    Window root = RootWindow(dpy, screen);
    xcb_void_cookie_t cookie;
    xcb_generic_error_t *error;

    cookie = xcb_dri3_pixmap_from_buffer_checked(xcb_connection,
            (*pixmap = xcb_generate_id(xcb_connection)), root, 0,
            width, height, stride, depth, bpp, fd);

    error = xcb_request_check(xcb_connection, cookie); /* performs a flush */
    if (error)
    {
        ERR("Error using DRI3 to convert a DmaBufFd to pixmap\n");
        return FALSE;
    }
    return TRUE;
}

BOOL DRI3DmaBufFromPixmap(Display *dpy, Pixmap pixmap, int *fd, int *width, int *height,
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