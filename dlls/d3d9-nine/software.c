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
#include <xcb/present.h>
#include <xcb/xcb.h>
#include <xcb/xproto.h>
#include <xcb/xcb_image.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/shm.h>
#include <errno.h>

#include "winbase.h"


struct software_info {
    void *mmap_mem;
    xcb_image_t *img;
    int mmap_size;
    xcb_shm_segment_info_t xsi;
    int fd;
};

typedef struct PRESENTprivSoftware {
    PRESENTPixmapPriv base;
    struct software_info priv;
} PRESENTprivSoftware;

#define PRESENTBASE(x) ((struct PRESENTprivSoftware *)&(x))->base
#define PRESENTPRIV(x) ((struct PRESENTprivSoftware *)&(x))->priv

static BOOL SoftwareFallbackInit(Display *dpy, PRESENTpriv **priv)
{
    *priv = (PRESENTpriv *) HeapAlloc(GetProcessHeap(),
            HEAP_ZERO_MEMORY, sizeof(PRESENTpriv));

    if (!*priv)
        return FALSE;

    return TRUE;
}

/* hypothesis: at this step all textures, etc are destroyed */
static void SoftwareFallbackDestroy(PRESENTpriv *priv)
{
    HeapFree(GetProcessHeap(), 0, priv);
}

static BOOL SoftwareFallbackOpen(Display *dpy, int screen, int *device_fd)
{
    *device_fd = 0;
    return TRUE;
}

static BOOL SoftwareFallbackCheckForXcbShmSupport(Display *dpy)
{
    int screen_num = DefaultScreen(dpy);
    xcb_connection_t *ret;
    xcb_xfixes_query_version_cookie_t cookie;
    xcb_xfixes_query_version_reply_t *vrep;
    xcb_shm_query_version_reply_t *srep;

    ret = xcb_connect(DisplayString(dpy), &screen_num);
    cookie = xcb_xfixes_query_version_unchecked(ret, XCB_XFIXES_MAJOR_VERSION, XCB_XFIXES_MINOR_VERSION);
    vrep = xcb_xfixes_query_version_reply(ret, cookie, NULL);
    if (vrep)
        free(vrep);

    srep = xcb_shm_query_version_reply (ret,
                    xcb_shm_query_version (ret),
                    NULL);
    if (!srep) {
        ERR("Failed to get shared memory version\n");
        xcb_disconnect(ret);
        return FALSE;
    }

    if (!srep->shared_pixmaps || srep->major_version < 1 ||
             (srep->major_version == 1 && srep->minor_version == 0))
    {
        ERR("Shared pixmaps not supported major_version=%d minor_version=%d\n",
                srep->major_version ,srep->minor_version);

        xcb_disconnect(ret);
        free(srep);

        return FALSE;
    }
    xcb_disconnect(ret);

    free(srep);

    return TRUE;
}

static BOOL SoftwareFallbackCheckSupport(Display *dpy)
{
    PRESENTpriv *priv;

    if (!SoftwareFallbackCheckForXcbShmSupport(dpy))
        return FALSE;
#if 0
    if (!SoftwareFallbackOpen(dpy, DefaultScreen(dpy), &drm_fd))
        return FALSE;
#endif

    return TRUE;
}

/* Destroy the content, except the link and the struct mem */
static void SoftwareDestroyPixmapContent(Display *dpy,
        PRESENTPixmapPriv *present_pixmap)
{
    xcb_connection_t *c = present_pixmap->present_priv->xcb_connection;
    xcb_shm_detach(c, PRESENTPRIV(*present_pixmap).xsi.shmseg);
    shmdt(PRESENTPRIV(*present_pixmap).xsi.shmaddr);
}

static BOOL SoftwareFallbackPRESENTPixmapInit(PRESENTpriv *present_priv,
        int fd, int width, int height, int stride, int depth,
        int bpp, PRESENTPixmapPriv **present_pixmap_priv)
{
    xcb_shm_segment_info_t xsi;
    xcb_connection_t *c = present_priv->xcb_connection;

    xsi.shmid = shmget(IPC_PRIVATE, stride * height, IPC_CREAT | 0777);
    if (xsi.shmid <= 0) {
        ERR("shmget failed %s\n", strerror(errno));
        return FALSE;
    }

    xsi.shmaddr = shmat(xsi.shmid, 0, 0);
    if (xsi.shmaddr == (void *)-1) {
        ERR("shmat failed %s\n", strerror(errno));
        shmctl(xsi.shmid, IPC_RMID, 0);
        return FALSE;
    }
    xsi.shmseg = xcb_generate_id(c);

    xcb_shm_attach(c, xsi.shmseg, xsi.shmid, 0);

    shmctl( xsi.shmid, IPC_RMID, 0 );

    xcb_pixmap_t pix = xcb_generate_id(c);
    xcb_shm_create_pixmap(
        c,
        pix,
        XDefaultRootWindow(present_priv->dpy),
        width, height,
        depth,
        xsi.shmseg,
        0
    );

    if (!pix) {
        ERR("xcb_shm_create_pixmap failed %s\n", strerror(errno));
        shmdt(xsi.shmaddr);
        xcb_shm_detach(c, xsi.shmseg);
        return FALSE;
    }

    *present_pixmap_priv = (PRESENTPixmapPriv *) HeapAlloc(GetProcessHeap(),
             HEAP_ZERO_MEMORY, sizeof(PRESENTprivSoftware));

    if (!*present_pixmap_priv) {
        shmdt(xsi.shmaddr);
        xcb_shm_detach(c, xsi.shmseg);
        xcb_free_pixmap(c, pix);
        return FALSE;
    }

    EnterCriticalSection(&present_priv->mutex_present);

    PRESENTPRIV(**present_pixmap_priv).xsi = xsi;
    PRESENTPRIV(**present_pixmap_priv).mmap_mem = xsi.shmaddr;
    PRESENTPRIV(**present_pixmap_priv).mmap_size = stride * height;
    PRESENTPRIV(**present_pixmap_priv).fd = fd;

    PRESENTBASE(**present_pixmap_priv).released = TRUE;
    PRESENTBASE(**present_pixmap_priv).pixmap = pix;
    PRESENTBASE(**present_pixmap_priv).present_priv = present_priv;
    PRESENTBASE(**present_pixmap_priv).next = present_priv->first_present_priv;
    PRESENTBASE(**present_pixmap_priv).width = width;
    PRESENTBASE(**present_pixmap_priv).height = height;
    PRESENTBASE(**present_pixmap_priv).depth = depth;

    present_priv->last_serial_given++;
    (*present_pixmap_priv)->serial = present_priv->last_serial_given;
    present_priv->first_present_priv = *present_pixmap_priv;

    LeaveCriticalSection(&present_priv->mutex_present);

    return TRUE;
}

static void SoftwarePRESENTPixmap(PRESENTPixmapPriv *present_pixmap_priv)
{
    PRESENTprivSoftware *priv = (PRESENTprivSoftware *)present_pixmap_priv;
    void *usr_addr = mmap(0, priv->priv.mmap_size, PROT_READ, MAP_PRIVATE, priv->priv.fd, 0);

    if ((ssize_t)usr_addr == MAP_FAILED) {
        ERR("mmap failed %s\n", strerror(errno));
        return;
    }
    memcpy(priv->priv.mmap_mem, usr_addr, priv->priv.mmap_size);
    munmap(usr_addr, priv->priv.mmap_size);
}

static BOOL SoftwarePRESENTHelperCopyFront(Display *dpy, PRESENTPixmapPriv *present_pixmap_priv)
{
    return FALSE;
}

struct PRESENT_DRI_func Softwarefunc = {
    .name = "Software (XSHM)",
    .Init = SoftwareFallbackInit,
    .Open = SoftwareFallbackOpen,
    .CheckSupport = SoftwareFallbackCheckSupport,
    .Destroy = SoftwareFallbackDestroy,
    .PRESENTPixmapInit = SoftwareFallbackPRESENTPixmapInit,
    .DestroyPixmapContent = SoftwareDestroyPixmapContent,
    .PRESENTPixmap = SoftwarePRESENTPixmap,
    .PRESENTHelperCopyFront = SoftwarePRESENTHelperCopyFront,
};
