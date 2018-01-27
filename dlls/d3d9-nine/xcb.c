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

#include "xcb.h"
#include "xcb_priv.h"

#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>

static struct PRESENT_DRI_func *drifunc;

extern struct PRESENT_DRI_func DRI3func;
#ifdef D3D9NINE_DRI2
extern struct PRESENT_DRI_func DRI2func;
#endif

static struct PRESENT_DRI_func *DRIbackends[] = {
        &DRI3func,
#ifdef D3D9NINE_DRI2
        &DRI2func,
#endif
};

BOOL PRESENTCheckExtension(Display *dpy, int major, int minor)
{
    xcb_connection_t *xcb_connection = XGetXCBConnection(dpy);
    xcb_present_query_version_cookie_t present_cookie;
    xcb_present_query_version_reply_t *present_reply;
    xcb_generic_error_t *error;
    const xcb_query_extension_reply_t *extension;

    xcb_prefetch_extension_data(xcb_connection, &xcb_present_id);

    extension = xcb_get_extension_data(xcb_connection, &xcb_present_id);
    if (!(extension && extension->present))
    {
        ERR("PRESENT extension is not present\n");
        return FALSE;
    }

    present_cookie = xcb_present_query_version(xcb_connection, major, minor);

    present_reply = xcb_present_query_version_reply(xcb_connection, present_cookie, &error);
    if (!present_reply)
    {
        free(error);
        ERR("Issue getting requested version of PRESENT: %d,%d\n", major, minor);
        return FALSE;
    }

    TRACE("PRESENT version %d,%d found. %u %u requested\n", major, minor,
            present_reply->major_version, present_reply->minor_version);

    free(present_reply);

    return TRUE;
}

static PRESENTPixmapPriv *PRESENTFindPixmapPriv(PRESENTpriv *present_priv, uint32_t serial)
{
    PRESENTPixmapPriv *current = present_priv->first_present_priv;

    while (current)
    {
        if (current->serial == serial)
            return current;
        current = current->next;
    }
    return NULL;
}

static void PRESENThandle_events(PRESENTpriv *present_priv, xcb_present_generic_event_t *ge)
{
    PRESENTPixmapPriv *present_pixmap_priv = NULL;

    switch (ge->evtype)
    {
        case XCB_PRESENT_COMPLETE_NOTIFY:
        {
            xcb_present_complete_notify_event_t *ce = (void *) ge;
            if (ce->kind == XCB_PRESENT_COMPLETE_KIND_NOTIFY_MSC)
            {
                if (ce->serial)
                    present_priv->notify_with_serial_pending = FALSE;
                free(ce);
                return;
            }
            present_pixmap_priv = PRESENTFindPixmapPriv(present_priv, ce->serial);
            if (!present_pixmap_priv || ce->kind != XCB_PRESENT_COMPLETE_KIND_PIXMAP)
            {
                ERR("FATAL ERROR: PRESENT handling failed\n");
                free(ce);
                return;
            }
            present_pixmap_priv->present_complete_pending--;
            switch (ce->mode)
            {
                case XCB_PRESENT_COMPLETE_MODE_FLIP:
                    present_pixmap_priv->last_present_was_flip = TRUE;
                    break;
                case XCB_PRESENT_COMPLETE_MODE_COPY:
                    present_pixmap_priv->last_present_was_flip = FALSE;
                    break;
            }
            present_priv->pixmap_present_pending--;
            present_priv->last_msc = ce->msc;
            break;
        }
        case XCB_PRESENT_EVENT_IDLE_NOTIFY:
        {
            xcb_present_idle_notify_event_t *ie = (void *) ge;
            present_pixmap_priv = PRESENTFindPixmapPriv(present_priv, ie->serial);
            if (!present_pixmap_priv || present_pixmap_priv->pixmap != ie->pixmap)
            {
                ERR("FATAL ERROR: PRESENT handling failed\n");
                free(ie);
                return;
            }
            present_pixmap_priv->released = TRUE;
            present_priv->idle_notify_since_last_check = TRUE;
            break;
        }
    }
    free(ge);
}

static void PRESENTflush_events(PRESENTpriv *present_priv, BOOL assert_no_other_thread_waiting)
{
    xcb_generic_event_t *ev;

    if ((present_priv->xcb_wait && !assert_no_other_thread_waiting) || /* don't steal events to someone waiting */
            !present_priv->special_event)
        return;

    while ((ev = xcb_poll_for_special_event(present_priv->xcb_connection,
            present_priv->special_event)) != NULL)
    {
        PRESENThandle_events(present_priv, (void *) ev);
    }
}

static BOOL PRESENTwait_events(PRESENTpriv *present_priv, BOOL allow_other_threads)
{
    xcb_generic_event_t *ev;

    if (allow_other_threads)
    {
        present_priv->xcb_wait = TRUE;
        EnterCriticalSection(&present_priv->mutex_xcb_wait);
        LeaveCriticalSection(&present_priv->mutex_present);
    }
    ev = xcb_wait_for_special_event(present_priv->xcb_connection, present_priv->special_event);
    if (allow_other_threads)
    {
        LeaveCriticalSection(&present_priv->mutex_xcb_wait);
        EnterCriticalSection(&present_priv->mutex_present);
        present_priv->xcb_wait = FALSE;
    }
    if (!ev)
    {
        ERR("FATAL error: xcb had an error\n");
        return FALSE;
    }

    PRESENThandle_events(present_priv, (void *) ev);
    return TRUE;
}

static struct xcb_connection_t *create_xcb_connection(Display *dpy)
{
    int screen_num;
    xcb_connection_t *ret;
    xcb_xfixes_query_version_cookie_t cookie;
    xcb_xfixes_query_version_reply_t *rep;

    ret = xcb_connect(DisplayString(dpy), &screen_num);
    cookie = xcb_xfixes_query_version_unchecked(ret, XCB_XFIXES_MAJOR_VERSION, XCB_XFIXES_MINOR_VERSION);
    rep = xcb_xfixes_query_version_reply(ret, cookie, NULL);
    if (rep)
        free(rep);
    return ret;
}

BOOL PRESENTPixmapInit(PRESENTpriv *present_priv,
        int fd, int width, int height, int stride, int depth,
        int bpp, PRESENTPixmapPriv **present_pixmap_priv)
{
    return drifunc->PRESENTPixmapInit(present_priv,
            fd, width, height, stride, depth, bpp, present_pixmap_priv);
}

BOOL PRESENTHelperCopyFront(Display *dpy, PRESENTPixmapPriv *present_pixmap_priv)
{
    return drifunc->PRESENTHelperCopyFront(dpy, present_pixmap_priv);
}

BOOL PRESENTInit(Display *dpy, PRESENTpriv **present_priv)
{
    if (!drifunc->Init(dpy, present_priv))
        return FALSE;

    (*present_priv)->xcb_connection = create_xcb_connection(dpy);
    (*present_priv)->xcb_connection_bis = create_xcb_connection(dpy);
    (*present_priv)->root = RootWindow(dpy, DefaultScreen(dpy));
    (*present_priv)->dpy = dpy;

    InitializeCriticalSection(&(*present_priv)->mutex_present);
    InitializeCriticalSection(&(*present_priv)->mutex_xcb_wait);
    return TRUE;
}

static void PRESENTForceReleases(PRESENTpriv *present_priv)
{
    PRESENTPixmapPriv *current = NULL;

    if (!present_priv->window)
        return;

    /* There should be no other thread listening for events here.
     * This can happen when hDestWindowOverride changes without reset.
     * This case should never happen, but can happen in theory.*/
    if (present_priv->xcb_wait)
    {
        xcb_present_notify_msc(present_priv->xcb_connection, present_priv->window, 0, 0, 0, 0);
        xcb_flush(present_priv->xcb_connection);
        EnterCriticalSection(&present_priv->mutex_xcb_wait);
        LeaveCriticalSection(&present_priv->mutex_xcb_wait);
        /* the problem here is that we don't have access to the event the other thread got.
         * It is either presented event, idle event or notify event.
         */
        while (present_priv->pixmap_present_pending >= 2)
            PRESENTwait_events(present_priv, FALSE);
        PRESENTflush_events(present_priv, TRUE);
        /* Remaining events to come can be a pair of present/idle,
         * or an idle, or nothing. To be sure we are after all pixmaps
         * have been presented, add an event to the queue that can only
         * be after the present event, then if we receive an event more,
         * we are sure all pixmaps were presented */
        present_priv->notify_with_serial_pending = TRUE;
        xcb_present_notify_msc(present_priv->xcb_connection, present_priv->window,
                1, present_priv->last_target + 5, 0, 0);

        xcb_flush(present_priv->xcb_connection);
        while (present_priv->notify_with_serial_pending)
            PRESENTwait_events(present_priv, FALSE);
        /* Now we are sure we are not expecting any new event */
    }
    else
    {
        while (present_priv->pixmap_present_pending) /* wait all sent pixmaps are presented */
            PRESENTwait_events(present_priv, FALSE);
        PRESENTflush_events(present_priv, TRUE); /* may be remaining idle event */
        /* Since idle events are send with the complete events when it is not flips,
         * we are not expecting any new event here */
    }

    current = present_priv->first_present_priv;
    while (current)
    {
        if (!current->released)
        {
            if (!current->last_present_was_flip && !present_priv->xcb_wait)
            {
                ERR("ERROR: a pixmap seems not released by PRESENT for no reason. Code bug.\n");
            }
            else
            {
                /* Present the same pixmap with a non-valid part to force the copy mode and the releases */
                xcb_xfixes_region_t valid, update;
                xcb_rectangle_t rect_update;
                rect_update.x = 0;
                rect_update.y = 0;
                rect_update.width = 8;
                rect_update.height = 1;
                valid = xcb_generate_id(present_priv->xcb_connection);
                update = xcb_generate_id(present_priv->xcb_connection);
                xcb_xfixes_create_region(present_priv->xcb_connection, valid, 1, &rect_update);
                xcb_xfixes_create_region(present_priv->xcb_connection, update, 1, &rect_update);
                /* here we know the pixmap has been presented. Thus if it is on screen,
                 * the following request can only make it released by the server if it is not */
                xcb_present_pixmap(present_priv->xcb_connection, present_priv->window,
                        current->pixmap, 0, valid, update, 0, 0, None, None,
                        None, XCB_PRESENT_OPTION_COPY | XCB_PRESENT_OPTION_ASYNC, 0, 0, 0, 0, NULL);
                xcb_flush(present_priv->xcb_connection);
                PRESENTwait_events(present_priv, FALSE); /* by assumption this can only be idle event */
                PRESENTflush_events(present_priv, TRUE); /* Shoudln't be needed */
            }
        }
        current = current->next;
    }
    /* Now all pixmaps are released (possibility if xcb_wait is true that one is not aware yet),
     * and we don't expect any new Present event to come from Xserver */
}

static void PRESENTFreeXcbQueue(PRESENTpriv *present_priv)
{
    if (present_priv->window)
    {
        xcb_unregister_for_special_event(present_priv->xcb_connection, present_priv->special_event);
        present_priv->last_msc = 0;
        present_priv->last_target = 0;
        present_priv->special_event = NULL;
    }
}

static BOOL PRESENTPrivChangeWindow(PRESENTpriv *present_priv, XID window)
{
    xcb_void_cookie_t cookie;
    xcb_generic_error_t *error;
    xcb_present_event_t eid;

    PRESENTForceReleases(present_priv);
    PRESENTFreeXcbQueue(present_priv);
    present_priv->window = window;

    if (window)
    {
        cookie = xcb_present_select_input_checked(present_priv->xcb_connection,
                (eid = xcb_generate_id(present_priv->xcb_connection)), window,
                XCB_PRESENT_EVENT_MASK_COMPLETE_NOTIFY | XCB_PRESENT_EVENT_MASK_IDLE_NOTIFY);

        present_priv->special_event = xcb_register_for_special_xge(present_priv->xcb_connection,
                &xcb_present_id, eid, NULL);

        error = xcb_request_check(present_priv->xcb_connection, cookie); /* performs a flush */
        if (error || !present_priv->special_event)
        {
            ERR("FAILED to use the X PRESENT extension. Was the destination a window ?\n");
            if (present_priv->special_event)
                xcb_unregister_for_special_event(present_priv->xcb_connection, present_priv->special_event);
            present_priv->special_event = NULL;
            present_priv->window = 0;
        }
    }
    return (present_priv->window != 0);
}

/* Destroy the content, except the link and the struct mem */
static void PRESENTDestroyPixmapContent(Display *dpy, PRESENTPixmapPriv *present_pixmap)
{
    xcb_connection_t *c = present_pixmap->present_priv->xcb_connection;

    drifunc->DestroyPixmapContent(dpy, present_pixmap);
    xcb_free_pixmap(c, present_pixmap->pixmap);
}

void PRESENTDestroy(Display *dpy, PRESENTpriv *present_priv)
{
    PRESENTPixmapPriv *current = NULL;

    EnterCriticalSection(&present_priv->mutex_present);

    PRESENTForceReleases(present_priv);

    current = present_priv->first_present_priv;
    while (current)
    {
        PRESENTPixmapPriv *next = current->next;

        PRESENTDestroyPixmapContent(dpy, current);
        HeapFree(GetProcessHeap(), 0, current);
        current = next;
    }

    PRESENTFreeXcbQueue(present_priv);

    xcb_disconnect(present_priv->xcb_connection);
    xcb_disconnect(present_priv->xcb_connection_bis);
    LeaveCriticalSection(&present_priv->mutex_present);
    DeleteCriticalSection(&present_priv->mutex_present);
    DeleteCriticalSection(&present_priv->mutex_xcb_wait);

    drifunc->Destroy(present_priv);
}

BOOL PRESENTTryFreePixmap(Display *dpy, PRESENTPixmapPriv *present_pixmap_priv)
{
    PRESENTpriv *present_priv = present_pixmap_priv->present_priv;
    PRESENTPixmapPriv *current;

    EnterCriticalSection(&present_priv->mutex_present);

    if (!present_pixmap_priv->released || present_pixmap_priv->present_complete_pending)
    {
        LeaveCriticalSection(&present_priv->mutex_present);
        return FALSE;
    }

    if (present_priv->first_present_priv == present_pixmap_priv)
    {
        present_priv->first_present_priv = present_pixmap_priv->next;
        goto free_priv;
    }

    current = present_priv->first_present_priv;
    while (current->next != present_pixmap_priv)
        current = current->next;
    current->next = present_pixmap_priv->next;
free_priv:
    PRESENTDestroyPixmapContent(dpy, present_pixmap_priv);
    HeapFree(GetProcessHeap(), 0, present_pixmap_priv);
    LeaveCriticalSection(&present_priv->mutex_present);
    return TRUE;
}

BOOL PRESENTPixmap(XID window, PRESENTPixmapPriv *present_pixmap_priv,
        const UINT PresentationInterval, const BOOL PresentAsync, const BOOL SwapEffectCopy,
        const RECT *pSourceRect, const RECT *pDestRect, const RGNDATA *pDirtyRegion)
{
    PRESENTpriv *present_priv = present_pixmap_priv->present_priv;
    xcb_void_cookie_t cookie;
    xcb_generic_error_t *error;
    int64_t target_msc, presentationInterval;
    xcb_xfixes_region_t valid, update;
    int16_t x_off, y_off;
    uint32_t options = XCB_PRESENT_OPTION_NONE;

    EnterCriticalSection(&present_priv->mutex_present);

    if (window != present_priv->window)
        PRESENTPrivChangeWindow(present_priv, window);

    if (!window)
    {
        ERR("ERROR: Try to Present a pixmap on a NULL window\n");
        LeaveCriticalSection(&present_priv->mutex_present);
        return FALSE;
    }

    PRESENTflush_events(present_priv, FALSE);
    /* Note: present_pixmap_priv->present_complete_pending may be non-0, because
     * on some paths the Xserver sends the complete event just after the idle
     * event. */
    if (!present_pixmap_priv->released)
    {
        ERR("FATAL ERROR: Trying to Present a pixmap not released\n");
        LeaveCriticalSection(&present_priv->mutex_present);
        return FALSE;
    }

    drifunc->PRESENTPixmap(present_pixmap_priv);

    target_msc = present_priv->last_msc;

    presentationInterval = PresentationInterval;
    if (PresentAsync)
        options |= XCB_PRESENT_OPTION_ASYNC;
    if (SwapEffectCopy)
        options |= XCB_PRESENT_OPTION_COPY;

    target_msc += presentationInterval * (present_priv->pixmap_present_pending + 1);

    /* Note: PRESENT defines some way to do partial copy:
     * presentproto:
     * 'x-off' and 'y-off' define the location in the window where
     *  the 0,0 location of the pixmap will be presented. valid-area
     *  and update-area are relative to the pixmap.
     */
    if (!pSourceRect && !pDestRect && !pDirtyRegion)
    {
        valid = 0;
        update = 0;
        x_off = 0;
        y_off = 0;
    }
    else
    {
        xcb_rectangle_t rect_update;
        xcb_rectangle_t *rect_updates;
        int i;

        rect_update.x = 0;
        rect_update.y = 0;
        rect_update.width = present_pixmap_priv->width;
        rect_update.height = present_pixmap_priv->height;
        x_off = 0;
        y_off = 0;
        if (pSourceRect)
        {
            x_off = -pSourceRect->left;
            y_off = -pSourceRect->top;
            rect_update.x = pSourceRect->left;
            rect_update.y = pSourceRect->top;
            rect_update.width = pSourceRect->right - pSourceRect->left;
            rect_update.height = pSourceRect->bottom - pSourceRect->top;
        }
        if (pDestRect)
        {
            x_off += pDestRect->left;
            y_off += pDestRect->top;
            rect_update.width = pDestRect->right - pDestRect->left;
            rect_update.height = pDestRect->bottom - pDestRect->top;
            /* Note: the size of pDestRect and pSourceRect are supposed to be the same size
             * because the driver would have done things to assure that. */
        }
        valid = xcb_generate_id(present_priv->xcb_connection_bis);
        update = xcb_generate_id(present_priv->xcb_connection_bis);
        xcb_xfixes_create_region(present_priv->xcb_connection_bis, valid, 1, &rect_update);
        if (pDirtyRegion && pDirtyRegion->rdh.nCount)
        {
            rect_updates = (void *) HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                    sizeof(xcb_rectangle_t) * pDirtyRegion->rdh.nCount);

            for (i = 0; i < pDirtyRegion->rdh.nCount; i++)
            {
                RECT rc;
                memcpy(&rc, pDirtyRegion->Buffer + i * sizeof(RECT), sizeof(RECT));
                rect_update.x = rc.left;
                rect_update.y = rc.top;
                rect_update.width = rc.right - rc.left;
                rect_update.height = rc.bottom - rc.top;
                memcpy(rect_updates + i * sizeof(xcb_rectangle_t), &rect_update, sizeof(xcb_rectangle_t));
            }
            xcb_xfixes_create_region(present_priv->xcb_connection_bis, update, pDirtyRegion->rdh.nCount, rect_updates);
            HeapFree(GetProcessHeap(), 0, rect_updates);
        } else
            xcb_xfixes_create_region(present_priv->xcb_connection_bis, update, 1, &rect_update);
    }

    cookie = xcb_present_pixmap_checked(present_priv->xcb_connection_bis,
            window, present_pixmap_priv->pixmap, present_pixmap_priv->serial,
            valid, update, x_off, y_off, None, None, None, options,
            target_msc, 0, 0, 0, NULL);
    error = xcb_request_check(present_priv->xcb_connection_bis, cookie); /* performs a flush */

    if (update)
        xcb_xfixes_destroy_region(present_priv->xcb_connection_bis, update);
    if (valid)
        xcb_xfixes_destroy_region(present_priv->xcb_connection_bis, valid);

    if (error)
    {
        xcb_get_geometry_cookie_t cookie_geom;
        xcb_get_geometry_reply_t *reply;

        cookie_geom = xcb_get_geometry(present_priv->xcb_connection_bis, window);
        reply = xcb_get_geometry_reply(present_priv->xcb_connection_bis, cookie_geom, NULL);

        ERR("Error using PRESENT. Here some debug info\n");
        if (!reply)
        {
            ERR("Error querying window info. Perhaps it doesn't exist anymore\n");
            LeaveCriticalSection(&present_priv->mutex_present);
            return FALSE;
        }
        ERR("Pixmap: width=%d, height=%d, depth=%d\n",
                present_pixmap_priv->width, present_pixmap_priv->height,
                present_pixmap_priv->depth);

        ERR("Window: width=%d, height=%d, depth=%d, x=%d, y=%d\n",
                (int) reply->width, (int) reply->height,
                (int) reply->depth, (int) reply->x, (int) reply->y);

        ERR("Present parameter: PresentationInterval=%d, Pending presentations=%d\n",
                PresentationInterval, present_priv->pixmap_present_pending);

        if (present_pixmap_priv->depth != reply->depth)
            ERR("Depths are different. PRESENT needs the pixmap and the window have same depth\n");
        free(reply);
        LeaveCriticalSection(&present_priv->mutex_present);
        return FALSE;
    }
    present_priv->last_target = target_msc;
    present_priv->pixmap_present_pending++;
    present_pixmap_priv->present_complete_pending++;
    present_pixmap_priv->released = FALSE;
    LeaveCriticalSection(&present_priv->mutex_present);
    return TRUE;
}

BOOL PRESENTWaitPixmapReleased(PRESENTPixmapPriv *present_pixmap_priv)
{
    PRESENTpriv *present_priv = present_pixmap_priv->present_priv;

    EnterCriticalSection(&present_priv->mutex_present);

    PRESENTflush_events(present_priv, FALSE);

    /* The part with present_pixmap_priv->present_complete_pending is legacy behaviour.
     * It matters for SwapEffectCopy with swapinterval > 0. */
    while (!present_pixmap_priv->released || present_pixmap_priv->present_complete_pending)
    {
        /* Note: following if should not happen because we'll never
         * use two PRESENTWaitPixmapReleased in parallels on same window.
         * However it would make it work in that case */
        if (present_priv->xcb_wait)
        {
            /* we allow only one thread to dispatch events */
            EnterCriticalSection(&present_priv->mutex_xcb_wait);
            /* here the other thread got an event but hasn't treated it yet */
            LeaveCriticalSection(&present_priv->mutex_xcb_wait);
            LeaveCriticalSection(&present_priv->mutex_present);
            Sleep(10); /* Let it treat the event */
            EnterCriticalSection(&present_priv->mutex_present);
        }
        else if (!PRESENTwait_events(present_priv, TRUE))
        {
            LeaveCriticalSection(&present_priv->mutex_present);
            return FALSE;
        }
    }
    LeaveCriticalSection(&present_priv->mutex_present);
    return TRUE;
}

BOOL PRESENTIsPixmapReleased(PRESENTPixmapPriv *present_pixmap_priv)
{
    PRESENTpriv *present_priv = present_pixmap_priv->present_priv;
    BOOL ret;

    EnterCriticalSection(&present_priv->mutex_present);

    PRESENTflush_events(present_priv, FALSE);

    ret = present_pixmap_priv->released;

    LeaveCriticalSection(&present_priv->mutex_present);
    return ret;
}

BOOL PRESENTWaitReleaseEvent(PRESENTpriv *present_priv)
{
    EnterCriticalSection(&present_priv->mutex_present);

    while (!present_priv->idle_notify_since_last_check)
    {
        /* Note: following if should not happen because we'll never
         * use two PRESENTWaitPixmapReleased in parallels on same window.
         * However it would make it work in that case */
        if (present_priv->xcb_wait)
        {
            /* we allow only one thread to dispatch events */
            EnterCriticalSection(&present_priv->mutex_xcb_wait);
            /* here the other thread got an event but hasn't treated it yet */
            LeaveCriticalSection(&present_priv->mutex_xcb_wait);
            LeaveCriticalSection(&present_priv->mutex_present);
            Sleep(10); /* Let it treat the event */
            EnterCriticalSection(&present_priv->mutex_present);
        }
        else if (!PRESENTwait_events(present_priv, TRUE))
        {
            ERR("Issue in PRESENTWaitReleaseEvent\n");
            LeaveCriticalSection(&present_priv->mutex_present);
            return FALSE;
        }
    }
    present_priv->idle_notify_since_last_check = FALSE;

    LeaveCriticalSection(&present_priv->mutex_present);
    return TRUE;
}

BOOL PRESENTCheckSupport(Display *dpy)
{
    for (int i = 0; i < sizeof(DRIbackends)/sizeof(DRIbackends[0]); i++)
    {
        if (DRIbackends[i]->CheckSupport(dpy))
        {
            drifunc = DRIbackends[i];
            return TRUE;
        }
        ERR("Failed to init DRI backend '%s'\n", DRIbackends[i]->name);
    }
    return FALSE;
}

BOOL PRESENTOpen(Display *dpy, int *fd)
{
    return drifunc->Open(dpy, DefaultScreen(dpy), fd);
}

const char* PRESENTGetBackendName(void)
{
    return drifunc ? drifunc->name : "";
}
