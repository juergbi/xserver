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
#include <wayland-drm-client-protocol.h>

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
drm_handle_authenticated (void *data, struct wl_drm *drm)
{
    struct xwl_screen *xwl_screen = data;

    xwl_screen->authenticated = 1;
}

static const struct wl_drm_listener drm_listener =
{
  drm_handle_device,
  drm_handle_authenticated
};

int
wayland_drm_screen_init(struct xwl_screen *xwl_screen)
{
    uint32_t magic;

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
    return Success;
}
