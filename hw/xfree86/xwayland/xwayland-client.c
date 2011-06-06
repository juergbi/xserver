/*
 * Copyright © 2010 Kristian Høgsberg
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

#include <linux/input.h>
#include <wayland-util.h>
#include <wayland-client.h>

#include <xf86Xinput.h>
#include <xf86Crtc.h>
#include <xf86str.h>
#include <windowstr.h>
#include <input.h>
#include <inputstr.h>
#include <exevents.h>

#include "xwayland.h"
#include "xwayland-private.h"
#include "drm-client-protocol.h"

static void
compositor_handle_visual(void *data,
			 struct wl_compositor *compositor,
			 uint32_t id, uint32_t token)
{
    struct xwl_screen *xwl_screen = data;

    switch (token) {
    case WL_COMPOSITOR_VISUAL_ARGB32:
	xwl_screen->argb_visual =
	    wl_visual_create(xwl_screen->display, id, 1);
	break;
    case WL_COMPOSITOR_VISUAL_PREMULTIPLIED_ARGB32:
	xwl_screen->premultiplied_argb_visual =
	    wl_visual_create(xwl_screen->display, id, 1);
	break;
    case WL_COMPOSITOR_VISUAL_XRGB32:
	xwl_screen->rgb_visual =
	    wl_visual_create(xwl_screen->display, id, 1);
	break;
    }
}

static const struct wl_compositor_listener compositor_listener = {
    compositor_handle_visual,
};

static void
display_handle_geometry(void *data,
			struct wl_output *output,
			int32_t x, int32_t y,
			int32_t width, int32_t height)
{
    struct xwl_output *xwl_output = data;

    xwl_output->x = x;
    xwl_output->y = y;
    xwl_output->width = width;
    xwl_output->height = height;

    xwl_output->xwl_screen->width = width;
    xwl_output->xwl_screen->height = height;
}

static const struct wl_output_listener output_listener = {
    display_handle_geometry,
};

static void
create_output(struct xwl_screen *xwl_screen, uint32_t id,
	      uint32_t version)
{
    struct xwl_output *xwl_output;

    xwl_output = xwl_output_create(xwl_screen);

    xwl_output->output = wl_output_create (xwl_screen->display, id, 1);
    wl_output_add_listener(xwl_output->output,
			   &output_listener, xwl_output);
}

static void
input_device_handle_motion(void *data, struct wl_input_device *input_device,
			   uint32_t time,
			   int32_t x, int32_t y, int32_t sx, int32_t sy)
{
    struct xwl_input_device *xwl_input_device = data;
    int32_t dx, dy;

    xwl_input_device->time = time;

    dx = xwl_input_device->focus_window->window->drawable.x;
    dy = xwl_input_device->focus_window->window->drawable.y;
    xf86PostMotionEvent(xwl_input_device->pointer,
			TRUE, 0, 2, sx + dx, sy + dy);
}

static void
input_device_handle_button(void *data, struct wl_input_device *input_device,
			   uint32_t time, uint32_t button, uint32_t state)
{
    struct xwl_input_device *xwl_input_device = data;
    int index;

    xwl_input_device->time = time;

    switch (button) {
    case BTN_MIDDLE:
	index = 2;
	break;
    case BTN_RIGHT:
	index = 3;
	break;
    default:
	index = button - BTN_LEFT + 1;
	break;
    }

    xf86PostButtonEvent(xwl_input_device->pointer,
			TRUE, index, state, 0, 0);
}

static void
input_device_handle_key(void *data, struct wl_input_device *input_device,
			uint32_t time, uint32_t key, uint32_t state)
{
    struct xwl_input_device *xwl_input_device = data;
    uint32_t modifier;

    xwl_input_device->time = time;

    switch (key) {
    case KEY_LEFTMETA:
    case KEY_RIGHTMETA:
	modifier = MODIFIER_META;
	break;
    default:
	modifier = 0;
	break;
    }

    if (state)
	xwl_input_device->modifiers |= modifier;
    else
	xwl_input_device->modifiers &= ~modifier;

    xf86PostKeyboardEvent(xwl_input_device->keyboard, key + 8, state);
}

static void
input_device_handle_pointer_focus(void *data,
				  struct wl_input_device *input_device,
				  uint32_t time,
				  struct wl_surface *surface,
				  int32_t x, int32_t y, int32_t sx, int32_t sy)

{
    struct xwl_input_device *xwl_input_device = data;

    xwl_input_device->time = time;

    if (surface)
	xwl_input_device->focus_window = wl_surface_get_user_data(surface);
    else
	xwl_input_device->focus_window = NULL;

    if (xwl_input_device->focus_window)
	SetDeviceRedirectWindow(xwl_input_device->pointer,
				xwl_input_device->focus_window->window);
    else
	SetDeviceRedirectWindow(xwl_input_device->pointer,
				PointerRootWin);
}

static void
input_device_handle_keyboard_focus(void *data,
				   struct wl_input_device *input_device,
				   uint32_t time,
				   struct wl_surface *surface,
				   struct wl_array *keys)
{
    struct xwl_input_device *xwl_input_device = data;
    uint32_t *k, *end;

    xwl_input_device->time = time;

    xwl_input_device->modifiers = 0;
    end = (uint32_t *) ((char *) keys->data + keys->size);
    for (k = keys->data; k < end; k++) {
	switch (*k) {
	case KEY_LEFTMETA:
	case KEY_RIGHTMETA:
	    xwl_input_device->modifiers |= MODIFIER_META;
	    break;
	}
    }
}

static const struct wl_input_device_listener input_device_listener = {
    input_device_handle_motion,
    input_device_handle_button,
    input_device_handle_key,
    input_device_handle_pointer_focus,
    input_device_handle_keyboard_focus,
};

static void
create_input_device(struct xwl_screen *xwl_screen, uint32_t id,
		    uint32_t version)
{
    struct xwl_input_device *xwl_input_device;

    xwl_input_device = xwl_input_device_create(xwl_screen);
    xwl_input_device->input_device =
        wl_input_device_create (xwl_screen->display, id, 1);

    wl_input_device_add_listener(xwl_input_device->input_device,
				 &input_device_listener,
				 xwl_input_device);
}

static void
global_handler(struct wl_display *display,
	       uint32_t id,
	       const char *interface,
	       uint32_t version,
	       void *data)
{
    struct xwl_screen *xwl_screen = data;

    if (strcmp (interface, "wl_compositor") == 0) {
	xwl_screen->compositor =
	    wl_compositor_create (xwl_screen->display, id, 1);
	wl_compositor_add_listener(xwl_screen->compositor,
				   &compositor_listener, xwl_screen);
#ifdef WITH_LIBDRMM
    } else if (strcmp (interface, "wl_drm") == 0) {
	xwl_screen->drm = wl_drm_create (xwl_screen->display, id);
	wl_drm_add_listener (xwl_screen->drm,
			     &drm_listener, xwl_screen);
#endif
    } else if (strcmp (interface, "wl_shm") == 0) {
        xwl_screen->shm = wl_shm_create (xwl_screen->display, id,
					    1);
    } else if (strcmp (interface, "wl_output") == 0) {
        create_output(xwl_screen, id, 1);
    } else if (strcmp (interface, "wl_input_device") == 0) {
        create_input_device(xwl_screen, id, 1);
    }
}

static int
source_update(uint32_t mask, void *data)
{
    struct xwl_screen *xwl_screen = data;

    xwl_screen->mask = mask;

    return 0;
}

static void
wakeup_handler(pointer data, int err, pointer read_mask)
{
    struct xwl_screen *xwl_screen = data;

    if (err >= 0 && FD_ISSET(xwl_screen->wayland_fd, (fd_set *) read_mask))
	wl_display_iterate(xwl_screen->display, WL_DISPLAY_READABLE);
}

static void
block_handler(pointer data, struct timeval **tv, pointer read_mask)
{
    struct xwl_screen *xwl_screen = data;

    /* The X servers "main loop" doesn't let us select for
     * writable, so let's just do a blocking write here. */

    while (xwl_screen->mask & WL_DISPLAY_WRITABLE)
	wl_display_iterate(xwl_screen->display, WL_DISPLAY_WRITABLE);
}

static void
sync_callback(void *data)
{
	int *done = data;

	*done = 1;
}

int
wayland_screen_init(struct xwl_screen *xwl_screen, int use_drm)
{
    int done = 0;

    xwl_screen->display = wl_display_connect(NULL);
    if (xwl_screen->display == NULL) {
	ErrorF("wl_display_create failed\n");
	return BadAlloc;
    }

    /* Set up listener so we'll catch all events. */
    wl_display_add_global_listener(xwl_screen->display,
				   global_handler, xwl_screen);

    /* Process connection events. */
    wl_display_iterate(xwl_screen->display, WL_DISPLAY_READABLE);

    xwl_screen->wayland_fd =
	wl_display_get_fd(xwl_screen->display,
			  source_update, xwl_screen);

    AddGeneralSocket(xwl_screen->wayland_fd);
    RegisterBlockAndWakeupHandlers(block_handler, wakeup_handler,
				   xwl_screen);

#ifdef WITH_LIBDRM
    if (use_drm) {
        int ret;

        if ((ret = wayland_drm_screen_init(xwl_screen)) != Success)
	    return ret;

        wl_display_iterate(xwl_screen->display, WL_DISPLAY_WRITABLE);
        while (!xwl_screen->authenticated)
            wl_display_iterate(xwl_screen->display, WL_DISPLAY_READABLE);
    }
#endif

    if (!xwl_screen->premultiplied_argb_visual || !xwl_screen->rgb_visual) {
	    wl_display_sync_callback(xwl_screen->display, sync_callback, &done);
	    wl_display_iterate(xwl_screen->display, WL_DISPLAY_WRITABLE);
	    while (!done)
		    wl_display_iterate(xwl_screen->display, WL_DISPLAY_READABLE);
	    if (!xwl_screen->premultiplied_argb_visual ||
		!xwl_screen->rgb_visual) {
		    ErrorF("visuals missing");
		    exit(1);
	    }
    }

    return Success;
}
