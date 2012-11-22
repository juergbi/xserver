/*
 * Copyright © 2011 Kristian Høgsberg
 *
 * Permission to use, copy, modify, distribute, and sell this software
 * and its documentation for any purpose is hereby granted without
 * fee, provided that the above copyright notice appear in all copies
 * and that both that copyright notice and this permission notice
 * appear in supporting documentation, and that the name of the
 * copyright holders not be used in advertising or publicity
 * pertaining to distribution of the software without specific,
 * written prior permission.  The copyright holders make no
 * representations about the suitability of this software for any
 * purpose.  It is provided "as is" without express or implied
 * warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS
 * SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS, IN NO EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN
 * AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING
 * OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS
 * SOFTWARE.
 */

#ifdef HAVE_XORG_CONFIG_H
#include "xorg-config.h"
#endif

#include <unistd.h>
#include <fcntl.h>

#include <xf86drm.h>
#include <wayland-util.h>
#include <wayland-client.h>
#include <drm-client-protocol.h>

#include <xf86Xinput.h>
#include <xf86Crtc.h>
#include <xf86str.h>
#include <windowstr.h>
#include <input.h>
#include <inputstr.h>
#include <exevents.h>

#include "xwayland.h"
#include "xwayland-private.h"

static void
drm_handle_device (void *data, struct wl_drm *drm, const char *device)
{
    struct xwl_screen *xwl_screen = data;

    xwl_screen->device_name = strdup (device);
}

static void
drm_handle_format(void *data, struct wl_drm *wl_drm, uint32_t format)
{
}

static void
drm_handle_authenticated (void *data, struct wl_drm *drm)
{
    struct xwl_screen *xwl_screen = data;

    xwl_screen->authenticated = 1;
}

static const struct wl_drm_listener xwl_drm_listener =
{
    drm_handle_device,
    drm_handle_format,
    drm_handle_authenticated
};

static void
drm_handler(void *data, struct wl_registry *registry, uint32_t id,
	    const char *interface, uint32_t version)
{
    struct xwl_screen *xwl_screen = data;

    if (strcmp (interface, "wl_drm") == 0) {
	xwl_screen->drm = wl_registry_bind(xwl_screen->registry, id,
                                           &wl_drm_interface, 1);
	wl_drm_add_listener(xwl_screen->drm, &xwl_drm_listener, xwl_screen);
    }
}

static const struct wl_registry_listener drm_listener = {
    drm_handler,
};

int
xwl_drm_pre_init(struct xwl_screen *xwl_screen)
{
    uint32_t magic;

    xwl_screen->drm_registry = wl_display_get_registry(xwl_screen->display);
    wl_registry_add_listener(xwl_screen->drm_registry, &drm_listener,
                             xwl_screen);

    /* Ensure drm_handler has seen all the interfaces */
    wl_display_roundtrip(xwl_screen->display);
    /* Ensure the xwl_drm_listener has seen the drm device, if any */
    wl_display_roundtrip(xwl_screen->display);

    ErrorF("wayland_drm_screen_init, device name %s\n",
	   xwl_screen->device_name);

    xwl_screen->drm_fd = open(xwl_screen->device_name, O_RDWR);
    if (xwl_screen->drm_fd < 0) {
	ErrorF("failed to open the drm fd\n");
	return BadAccess;
    }

    if (drmGetMagic(xwl_screen->drm_fd, &magic)) {
	ErrorF("failed to get drm magic");
	return BadAccess;
    }

    wl_drm_authenticate(xwl_screen->drm, magic);

    wl_display_roundtrip(xwl_screen->display);

    ErrorF("opened drm fd: %d\n", xwl_screen->drm_fd);

    if (!xwl_screen->authenticated) {
	ErrorF("Failed to auth drm fd\n");
	return BadAccess;
    }

    return Success;
}

Bool xwl_drm_initialised(struct xwl_screen *xwl_screen)
{
    return xwl_screen->authenticated;
}

int xwl_screen_get_drm_fd(struct xwl_screen *xwl_screen)
{
    return xwl_screen->drm_fd;
}

int xwl_drm_authenticate(struct xwl_screen *xwl_screen,
			    uint32_t magic)
{
    int ret;

    xwl_screen->authenticated = 0;

    if (xwl_screen->drm)
	wl_drm_authenticate (xwl_screen->drm, magic);

    ret = wl_display_roundtrip(xwl_screen->display);
    if (ret == -1)
	return BadAlloc;
    if (!xwl_screen->authenticated)
	return BadAlloc;

    return Success;
}


int
xwl_create_window_buffer_drm(struct xwl_window *xwl_window,
			     PixmapPtr pixmap, uint32_t name)
{
    VisualID visual;
    WindowPtr window = xwl_window->window;
    ScreenPtr screen = window->drawable.pScreen;
    int i;

    visual = wVisual(window);
    for (i = 0; i < screen->numVisuals; i++)
	if (screen->visuals[i].vid == visual)
	    break;

    xwl_window->buffer =
      wl_drm_create_buffer(xwl_window->xwl_screen->drm,
			   name,
			   pixmap->drawable.width,
			   pixmap->drawable.height,
			   pixmap->devKind,
			   WL_DRM_FORMAT_ARGB8888);

    return xwl_window->buffer ? Success : BadDrawable;
}

