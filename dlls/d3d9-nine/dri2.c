/*
 * Wine DRI2 interface
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
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(d3d9nine);

#include "dri2.h"

#include <fcntl.h>
#include <unistd.h>

#include "winbase.h"

#ifdef D3D9NINE_DRI2
#include <sys/ioctl.h>

#define BOOL X_BOOL
#define BYTE X_BYTE
#define INT8 X_INT8
#define INT16 X_INT16
#define INT32 X_INT32
#define INT64 X_INT64
#include <X11/Xmd.h>
#undef BOOL
#undef BYTE
#undef INT8
#undef INT16
#undef INT32
#undef INT64
#undef LONG64

#include <X11/Xlibint.h>
#include <X11/extensions/dri2tokens.h>
#include <X11/extensions/dri2proto.h>
#include <X11/extensions/extutil.h>
#define GL_GLEXT_PROTOTYPES 1
#define EGL_EGLEXT_PROTOTYPES 1
#define GL_GLEXT_LEGACY 1

/* workaround for broken ABI on x86_64 due to windef.h */
#undef APIENTRY
#undef APIENTRYP
#include <GL/gl.h>

/* workaround gl header bug */
#define glBlendColor glBlendColorLEV
#define glBlendEquation glBlendEquationLEV
#include <GL/glext.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <libdrm/drm_fourcc.h>
#include <libdrm/drm.h>

static EGLDisplay display = NULL;
static int display_ref = 0;

struct DRI2PixmapPriv;
struct DRI2priv {
    struct DRI2PixmapPriv *first_dri2_priv;
    Display *dpy;
    EGLDisplay display;
    EGLContext context;
    PFNGLEGLIMAGETARGETTEXTURE2DOESPROC glEGLImageTargetTexture2DOES_func;
    PFNEGLCREATEIMAGEKHRPROC eglCreateImageKHR_func;
    PFNEGLDESTROYIMAGEKHRPROC eglDestroyImageKHR_func;
};
struct DRI2PixmapPriv {
    GLuint fbo_read;
    GLuint fbo_write;
    GLuint texture_read;
    GLuint texture_write;
    unsigned int width;
    unsigned int height;
    struct DRI2PixmapPriv *next;
};


static XExtensionInfo _dri2_info_data;
static XExtensionInfo *dri2_info = &_dri2_info_data;
static char dri2_name[] = DRI2_NAME;

#define DRI2CheckExtension(dpy, i, val) \
  XextCheckExtension(dpy, i, dri2_name, val)

static int close_display(Display *dpy, XExtCodes *codes);
static Bool wire_to_event(Display *dpy, XEvent *re, xEvent *event);
static Status event_to_wire(Display *dpy, XEvent *re, xEvent *event);
static int error( Display *dpy, xError *err, XExtCodes *codes, int *ret_code );

static XExtensionHooks dri2_hooks = {
    NULL, /* create_gc */
    NULL, /* copy_gc */
    NULL, /* flush_gc */
    NULL, /* free_gc */
    NULL, /* create_font */
    NULL, /* free_font */
    close_display, /* close_display */
    wire_to_event, /* wire_to_event */
    event_to_wire, /* event_to_wire */
    error, /* error */
    NULL, /* error_string */
};
static XEXT_GENERATE_CLOSE_DISPLAY(close_display, dri2_info);
static XEXT_GENERATE_FIND_DISPLAY(find_display, dri2_info,
                                  dri2_name, &dri2_hooks, 0, NULL);
static Bool wire_to_event(Display *dpy, XEvent *re, xEvent *event)
{
    XExtDisplayInfo *info = find_display(dpy);
    DRI2CheckExtension(dpy, info, False);
    TRACE("dri2 wire_to_event\n");
    return False;
}

static Status event_to_wire(Display *dpy, XEvent *re, xEvent *event)
{
    XExtDisplayInfo *info = find_display(dpy);
    DRI2CheckExtension(dpy, info, False);
    TRACE("dri2 event_to_wire\n");
    return False;
}

static int error(Display *dpy, xError *err, XExtCodes *codes, int *ret_code)
{
    TRACE("dri2 error\n");
    return False;
}

#define XALIGN(x) (((x) + 3) & (~3))

static BOOL DRI2Connect(Display *dpy, XID window, unsigned driver_type, char **device)
{
    XExtDisplayInfo *info = find_display(dpy);
    xDRI2ConnectReply rep;
    xDRI2ConnectReq *req;
    int dev_len, driv_len;
    char *driver;

    DRI2CheckExtension(dpy, info, False);

    *device = NULL;

    LockDisplay(dpy);
    GetReq(DRI2Connect, req);
    req->reqType = info->codes->major_opcode;
    req->dri2ReqType = X_DRI2Connect;
    req->window = window;
    req->driverType = driver_type;
    if (!_XReply(dpy, (xReply *)&rep, 0, xFalse))
    {
        UnlockDisplay(dpy);
        SyncHandle();
        return False;
    }

    /* check string lengths */
    dev_len = rep.deviceNameLength;
    driv_len = rep.driverNameLength;
    if (dev_len == 0 || driv_len == 0)
    {
        _XEatData(dpy, XALIGN(dev_len) + XALIGN(driv_len));
        UnlockDisplay(dpy);
        SyncHandle();
        return False;
    }

    /* read out driver */
    driver = HeapAlloc(GetProcessHeap(), 0, driv_len + 1);
    if (!driver)
    {
        _XEatData(dpy, XALIGN(dev_len) + XALIGN(driv_len));
        UnlockDisplay(dpy);
        SyncHandle();
        return False;
    }
    _XReadPad(dpy, driver, driv_len);
    HeapFree(GetProcessHeap(), 0, driver); /* we don't need the driver */

    /* read out device */
    *device = HeapAlloc(GetProcessHeap(), 0, dev_len + 1);
    if (!*device)
    {
        _XEatData(dpy, XALIGN(dev_len));
        UnlockDisplay(dpy);
        SyncHandle();
        return False;
    }
    _XReadPad(dpy, *device, dev_len);
    (*device)[dev_len] = '\0';

    UnlockDisplay(dpy);
    SyncHandle();

    return True;
}

static Bool DRI2Authenticate(Display *dpy, XID window, uint32_t token)
{
    XExtDisplayInfo *info = find_display(dpy);
    xDRI2AuthenticateReply rep;
    xDRI2AuthenticateReq *req;

    DRI2CheckExtension(dpy, info, False);

    LockDisplay(dpy);
    GetReq(DRI2Authenticate, req);
    req->reqType = info->codes->major_opcode;
    req->dri2ReqType = X_DRI2Authenticate;
    req->window = window;
    req->magic = token;
    if (!_XReply(dpy, (xReply *)&rep, 0, xFalse))
    {
        UnlockDisplay(dpy);
        SyncHandle();
        return False;
    }
    UnlockDisplay(dpy);
    SyncHandle();

    return rep.authenticated ? True : False;
}

BOOL DRI2FallbackOpen(Display *dpy, int screen, int *device_fd)
{
    char *device;
    int fd;
    Window root = RootWindow(dpy, screen);
    drm_auth_t auth;

    if (!DRI2Connect(dpy, root, DRI2DriverDRI, &device))
        return FALSE;

    fd = open(device, O_RDWR);
    HeapFree(GetProcessHeap(), 0, device);
    if (fd < 0)
        return FALSE;

    if (ioctl(fd, DRM_IOCTL_GET_MAGIC, &auth) != 0)
    {
        close(fd);
        return FALSE;
    }

    if (!DRI2Authenticate(dpy, root, auth.magic))
    {
        close(fd);
        return FALSE;
    }

    *device_fd = fd;

    return TRUE;
}

BOOL DRI2FallbackInit(Display *dpy, struct DRI2priv **priv)
{
    PFNGLEGLIMAGETARGETTEXTURE2DOESPROC glEGLImageTargetTexture2DOES_func;
    PFNEGLCREATEIMAGEKHRPROC eglCreateImageKHR_func;
    PFNEGLGETPLATFORMDISPLAYEXTPROC eglGetPlatformDisplayEXT_func;
    PFNEGLDESTROYIMAGEKHRPROC eglDestroyImageKHR_func;
    EGLint major, minor;
    EGLConfig config;
    EGLContext context;
    EGLint i;
    EGLBoolean b;
    EGLenum current_api = 0;
    const char *extensions;
    EGLint config_attribs[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
        EGL_NONE
    };
    EGLint context_compatibility_attribs[] = {
        EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR, EGL_CONTEXT_OPENGL_COMPATIBILITY_PROFILE_BIT_KHR,
        EGL_NONE
    };

    current_api = eglQueryAPI();
    eglGetPlatformDisplayEXT_func = (PFNEGLGETPLATFORMDISPLAYEXTPROC)
            eglGetProcAddress("eglGetPlatformDisplayEXT");

    if (!eglGetPlatformDisplayEXT_func)
        return FALSE;
    if (!display)
        display = eglGetPlatformDisplayEXT_func(EGL_PLATFORM_X11_EXT, dpy, NULL);
    if (!display)
        return FALSE;
    /* count references on display for multi device setups */
    display_ref++;

    if (eglInitialize(display, &major, &minor) != EGL_TRUE)
        goto clean_egl_display;

    extensions = eglQueryString(display, EGL_CLIENT_APIS);
    if (!extensions || !strstr(extensions, "OpenGL"))
        goto clean_egl_display;

    extensions = eglQueryString(display, EGL_EXTENSIONS);
    if (!extensions || !strstr(extensions, "EGL_EXT_image_dma_buf_import") ||
            !strstr(extensions, "EGL_KHR_create_context") ||
            !strstr(extensions, "EGL_KHR_surfaceless_context") ||
            !strstr(extensions, "EGL_KHR_image_base"))
        goto clean_egl_display;

    if (!eglChooseConfig(display, config_attribs, &config, 1, &i))
        goto clean_egl_display;

    b = eglBindAPI(EGL_OPENGL_API);
    if (b == EGL_FALSE)
        goto clean_egl_display;
    context = eglCreateContext(display, config, EGL_NO_CONTEXT, context_compatibility_attribs);
    if (context == EGL_NO_CONTEXT)
        goto clean_egl_display;

    glEGLImageTargetTexture2DOES_func = (PFNGLEGLIMAGETARGETTEXTURE2DOESPROC)
            eglGetProcAddress("glEGLImageTargetTexture2DOES");

    eglCreateImageKHR_func = (PFNEGLCREATEIMAGEKHRPROC)
            eglGetProcAddress("eglCreateImageKHR");

    eglDestroyImageKHR_func = (PFNEGLDESTROYIMAGEKHRPROC)
            eglGetProcAddress("eglDestroyImageKHR");

    if (!eglCreateImageKHR_func ||
            !glEGLImageTargetTexture2DOES_func ||
            !eglDestroyImageKHR_func)
    {
        ERR("eglGetProcAddress failed !");
        goto clean_egl;
    }

    eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);

    *priv = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
            sizeof(struct DRI2priv));
    if (!*priv)
        goto clean_egl;
    (*priv)->dpy = dpy;
    (*priv)->display = display;
    (*priv)->context = context;
    (*priv)->glEGLImageTargetTexture2DOES_func = glEGLImageTargetTexture2DOES_func;
    (*priv)->eglCreateImageKHR_func = eglCreateImageKHR_func;
    (*priv)->eglDestroyImageKHR_func = eglDestroyImageKHR_func;
    eglBindAPI(current_api);
    return TRUE;

clean_egl:
    eglDestroyContext(display, context);

clean_egl_display:
    eglTerminate(display);
    eglBindAPI(current_api);
    return FALSE;
}

/* hypothesis: at this step all textures, etc are destroyed */
void DRI2FallbackDestroy(struct DRI2priv *priv)
{
    EGLenum current_api;
    struct DRI2PixmapPriv *current;

    current = priv->first_dri2_priv;
    while (current)
    {
        struct DRI2PixmapPriv *next = current->next;
        DRI2DestroyPixmap(priv, current);
        current = next;
    }

    current_api = eglQueryAPI();
    eglBindAPI(EGL_OPENGL_API);
    eglMakeCurrent(priv->display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(priv->display, priv->context);
    if (display)
    {
        /* destroy display connection with last device */
        display_ref--;
        if (!display_ref)
        {
            eglTerminate(display);
            display = NULL;
        }
    }
    eglBindAPI(current_api);

    HeapFree(GetProcessHeap(), 0, priv);
}

BOOL DRI2FallbackCheckSupport(Display *dpy)
{
    struct DRI2priv *priv;
    int fd;
    if (!DRI2FallbackInit(dpy, &priv))
        return FALSE;
    DRI2FallbackDestroy(priv);
    if (!DRI2FallbackOpen(dpy, DefaultScreen(dpy), &fd))
        return FALSE;
    close(fd);
    return TRUE;
}

BOOL DRI2PresentPixmap(struct DRI2priv *dri2_priv, struct DRI2PixmapPriv *dri2_pixmap_priv)
{
    EGLenum current_api = 0;

    current_api = eglQueryAPI();
    eglBindAPI(EGL_OPENGL_API);
    if (eglMakeCurrent(dri2_priv->display, EGL_NO_SURFACE,
            EGL_NO_SURFACE, dri2_priv->context))
    {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, dri2_pixmap_priv->fbo_read);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, dri2_pixmap_priv->fbo_write);

        glBlitFramebuffer(0, 0, dri2_pixmap_priv->width, dri2_pixmap_priv->height,
                0, 0, dri2_pixmap_priv->width, dri2_pixmap_priv->height,
                GL_COLOR_BUFFER_BIT, GL_NEAREST);
        glFlush(); /* Perhaps useless */
    }
    else
    {
        ERR("eglMakeCurrent failed with 0x%0X\n", eglGetError());
        return FALSE;
    }

    eglMakeCurrent(dri2_priv->display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglBindAPI(current_api);

    return TRUE;
}


BOOL DRI2FallbackPRESENTPixmap(struct DRI2priv *dri2_priv,
        int fd, int width, int height, int stride, int depth,
        int bpp, struct DRI2PixmapPriv **dri2_pixmap_priv,
        Pixmap *pixmap)
{
    EGLImageKHR image;
    GLuint texture_read, texture_write, fbo_read, fbo_write;
    EGLint attribs[] = {
        EGL_WIDTH, 0,
        EGL_HEIGHT, 0,
        EGL_LINUX_DRM_FOURCC_EXT, DRM_FORMAT_ARGB8888,
        EGL_DMA_BUF_PLANE0_FD_EXT, 0,
        EGL_DMA_BUF_PLANE0_OFFSET_EXT, 0,
        EGL_DMA_BUF_PLANE0_PITCH_EXT, 0,
        EGL_NONE
    };
    EGLenum current_api = 0;
    int status;

    TRACE("fd=%d, width=%d, height=%d, stride=%d, depth=%d, bpp=%d\n",
            fd, width, height, stride, depth, bpp);

    attribs[1] = width;
    attribs[3] = height;
    attribs[7] = fd;
    attribs[11] = stride;

    current_api = eglQueryAPI();
    eglBindAPI(EGL_OPENGL_API);

    /* We bind the dma-buf to a EGLImage, then to a texture, and then to a fbo.
     * Note that we can delete the EGLImage, but we shouldn't delete the texture,
     * else the fbo is invalid */

    image = dri2_priv->eglCreateImageKHR_func(dri2_priv->display,
            EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, NULL, attribs);

    if (image == EGL_NO_IMAGE_KHR) {
        ERR("eglCreateImageKHR failed with 0x%0X\n", eglGetError());
        goto fail;
    }
    close(fd);

    if (eglMakeCurrent(dri2_priv->display, EGL_NO_SURFACE, EGL_NO_SURFACE, dri2_priv->context))
    {
        glGenTextures(1, &texture_read);
        glBindTexture(GL_TEXTURE_2D, texture_read);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        dri2_priv->glEGLImageTargetTexture2DOES_func(GL_TEXTURE_2D, image);
        glGenFramebuffers(1, &fbo_read);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo_read);
        glFramebufferTexture2D(GL_FRAMEBUFFER,
                               GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, texture_read,
                               0);
        status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE)
            goto fail;
        glBindTexture(GL_TEXTURE_2D, 0);
        dri2_priv->eglDestroyImageKHR_func(dri2_priv->display, image);

        /* We bind a newly created pixmap (to which we want to copy the content)
         * to an EGLImage, then to a texture, then to a fbo. */
        image = dri2_priv->eglCreateImageKHR_func(dri2_priv->display,
                                                  dri2_priv->context,
                                                  EGL_NATIVE_PIXMAP_KHR,
                                                  (void *)*pixmap, NULL);
        if (image == EGL_NO_IMAGE_KHR)
            goto fail;

        glGenTextures(1, &texture_write);
        glBindTexture(GL_TEXTURE_2D, texture_write);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        dri2_priv->glEGLImageTargetTexture2DOES_func(GL_TEXTURE_2D, image);
        glGenFramebuffers(1, &fbo_write);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo_write);
        glFramebufferTexture2D(GL_FRAMEBUFFER,
                               GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, texture_write,
                               0);
        status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE)
            goto fail;
        glBindTexture(GL_TEXTURE_2D, 0);
        dri2_priv->eglDestroyImageKHR_func(dri2_priv->display, image);
    }
    else
        ERR("eglMakeCurrent failed with 0x%0X\n", eglGetError());

    eglMakeCurrent(dri2_priv->display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);

    *dri2_pixmap_priv = (struct DRI2PixmapPriv *) HeapAlloc(GetProcessHeap(),
             HEAP_ZERO_MEMORY, sizeof(struct DRI2PixmapPriv));

    if (!*dri2_pixmap_priv)
        goto fail;

    (*dri2_pixmap_priv)->fbo_read = fbo_read;
    (*dri2_pixmap_priv)->fbo_write = fbo_write;
    (*dri2_pixmap_priv)->texture_read = texture_read;
    (*dri2_pixmap_priv)->texture_write = texture_write;
    (*dri2_pixmap_priv)->width = width;
    (*dri2_pixmap_priv)->height = height;
    (*dri2_pixmap_priv)->next = dri2_priv->first_dri2_priv;
    dri2_priv->first_dri2_priv = *dri2_pixmap_priv;

    eglBindAPI(current_api);

    return TRUE;
fail:
    eglMakeCurrent(dri2_priv->display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglBindAPI(current_api);
    return FALSE;
}

void DRI2DestroyPixmap(struct DRI2priv *dri2_priv, struct DRI2PixmapPriv *dri2_pixmap_priv)
{
    EGLenum current_api;

    if (dri2_priv->first_dri2_priv == dri2_pixmap_priv)
    {
        dri2_priv->first_dri2_priv = dri2_pixmap_priv->next;
    }
    else
    {
        struct DRI2PixmapPriv *current;

        current = dri2_priv->first_dri2_priv;
        while (current->next != dri2_pixmap_priv)
            current = current->next;
        current->next = dri2_pixmap_priv->next;
    }

    current_api = eglQueryAPI();

    eglBindAPI(EGL_OPENGL_API);
    if (eglMakeCurrent(dri2_priv->display, EGL_NO_SURFACE, EGL_NO_SURFACE,
             dri2_priv->context))
    {
        glDeleteFramebuffers(1, &dri2_pixmap_priv->fbo_read);
        glDeleteFramebuffers(1, &dri2_pixmap_priv->fbo_write);
        glDeleteTextures(1, &dri2_pixmap_priv->texture_read);
        glDeleteTextures(1, &dri2_pixmap_priv->texture_write);
    }
    else
        ERR("eglMakeCurrent failed with 0x%0X\n", eglGetError());

    eglMakeCurrent(dri2_priv->display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglBindAPI(current_api);

    HeapFree(GetProcessHeap(), 0, dri2_pixmap_priv);
}
#endif