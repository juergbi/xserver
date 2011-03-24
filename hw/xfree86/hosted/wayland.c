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

#include "hosted.h"
#include "hosted-private.h"
#include "drm-client-protocol.h"

static void
wayland_flush(struct hosted_window *hosted_window, BoxPtr box)
{
    wl_surface_damage(hosted_window->surface,
		      box->x1, box->y1, box->x2 - box->x1, box->y2 - box->y1);
}

static struct hosted_backend wayland_backend = {
    wayland_flush
};

static void
display_handle_geometry(void *data,
			struct wl_output *output,
			int32_t x, int32_t y,
			int32_t width, int32_t height)
{
    struct hosted_output *hosted_output = data;
    fprintf(stderr, "geometry %d %d %d %d\n", x, y, width, height);
    hosted_output->x = x;
    hosted_output->y = y;
    hosted_output->width = width;
    hosted_output->height = height;

    hosted_output->hosted_screen->width = width;
    hosted_output->hosted_screen->height = height;
}

static const struct wl_output_listener output_listener = {
	display_handle_geometry,
};

static void
create_output(struct hosted_screen *hosted_screen, uint32_t id,
	      uint32_t version)
{
    struct hosted_output *hosted_output;

    hosted_output = hosted_output_create(hosted_screen);
    hosted_output->output = wl_output_create (hosted_screen->display, id,
					      version);
    wl_output_add_listener(hosted_output->output,
			   &output_listener, hosted_output);
}

static void
input_device_handle_motion(void *data, struct wl_input_device *input_device,
			   uint32_t time,
			   int32_t x, int32_t y, int32_t sx, int32_t sy)
{
    struct hosted_input_device *hosted_input_device = data;
    int32_t dx, dy;

    hosted_input_device->time = time;

    dx = hosted_input_device->focus_window->window->drawable.x;
    dy = hosted_input_device->focus_window->window->drawable.y;
    xf86PostMotionEvent(hosted_input_device->pointer,
			TRUE, 0, 2, sx + dx, sy + dy);
}

static void
input_device_handle_button(void *data, struct wl_input_device *input_device,
			   uint32_t time, uint32_t button, uint32_t state)
{
    struct hosted_input_device *hosted_input_device = data;
    int index;

    hosted_input_device->time = time;

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

    xf86PostButtonEvent(hosted_input_device->pointer,
			TRUE, index, state, 0, 0);
}

static void
input_device_handle_key(void *data, struct wl_input_device *input_device,
			uint32_t time, uint32_t key, uint32_t state)
{
    struct hosted_input_device *hosted_input_device = data;
    uint32_t modifier;

    hosted_input_device->time = time;

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
	hosted_input_device->modifiers |= modifier;
    else
	hosted_input_device->modifiers &= ~modifier;

    xf86PostKeyboardEvent(hosted_input_device->keyboard, key + 8, state);
}

static void
input_device_handle_pointer_focus(void *data,
				  struct wl_input_device *input_device,
				  uint32_t time,
				  struct wl_surface *surface,
				  int32_t x, int32_t y, int32_t sx, int32_t sy)

{
    struct hosted_input_device *hosted_input_device = data;

    hosted_input_device->time = time;

    if (surface)
	hosted_input_device->focus_window = wl_surface_get_user_data(surface);
    else
	hosted_input_device->focus_window = NULL;

    if (hosted_input_device->focus_window)
	SetDeviceRedirectWindow(hosted_input_device->pointer,
				hosted_input_device->focus_window->window);
    else
	SetDeviceRedirectWindow(hosted_input_device->pointer,
				PointerRootWin);
}

static void
input_device_handle_keyboard_focus(void *data,
				   struct wl_input_device *input_device,
				   uint32_t time,
				   struct wl_surface *surface,
				   struct wl_array *keys)
{
    struct hosted_input_device *hosted_input_device = data;
    uint32_t *k, *end;

    hosted_input_device->time = time;

    hosted_input_device->modifiers = 0;
    end = (uint32_t *) ((char *) keys->data + keys->size);
    for (k = keys->data; k < end; k++) {
	switch (*k) {
	case KEY_LEFTMETA:
	case KEY_RIGHTMETA:
	    hosted_input_device->modifiers |= MODIFIER_META;
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
create_input_device(struct hosted_screen *hosted_screen, uint32_t id,
		    uint32_t version)
{
    struct hosted_input_device *hosted_input_device;

    hosted_input_device = hosted_input_device_create(hosted_screen);
    hosted_input_device->input_device =
    wl_input_device_create (hosted_screen->display, id,
			    version);

    wl_input_device_add_listener(hosted_input_device->input_device,
				 &input_device_listener,
				 hosted_input_device);
}

static void
global_handler(struct wl_display *display,
	       uint32_t id,
	       const char *interface,
	       uint32_t version,
	       void *data)
{
    struct hosted_screen *hosted_screen = data;

    if (strcmp (interface, "wl_compositor") == 0) {
	hosted_screen->compositor =
	    wl_compositor_create (hosted_screen->display, id, version);
#ifdef WITH_LIBDRMM
    } else if (strcmp (interface, "wl_drm") == 0) {
	hosted_screen->drm = wl_drm_create (hosted_screen->display, id);
	wl_drm_add_listener (hosted_screen->drm,
			     &drm_listener, hosted_screen);
#endif
    } else if (strcmp (interface, "wl_shm") == 0) {
        hosted_screen->shm = wl_shm_create (hosted_screen->display, id,
					    version);
    } else if (strcmp (interface, "wl_output") == 0) {
        create_output(hosted_screen, id, version);
    } else if (strcmp (interface, "wl_input_device") == 0) {
        create_input_device(hosted_screen, id, version);
    }
}

static int
source_update(uint32_t mask, void *data)
{
    struct hosted_screen *hosted_screen = data;

    hosted_screen->mask = mask;

    return 0;
}

static void
wakeup_handler(pointer data, int err, pointer read_mask)
{
    struct hosted_screen *hosted_screen = data;

    if (err >= 0 && FD_ISSET(hosted_screen->wayland_fd, (fd_set *) read_mask))
	wl_display_iterate(hosted_screen->display, WL_DISPLAY_READABLE);
}

static void
block_handler(pointer data, struct timeval **tv, pointer read_mask)
{
    struct hosted_screen *hosted_screen = data;

    /* The X servers "main loop" doesn't let us select for
     * writable, so let's just do a blocking write here. */

    while (hosted_screen->mask & WL_DISPLAY_WRITABLE)
	wl_display_iterate(hosted_screen->display, WL_DISPLAY_WRITABLE);
}

int
wayland_screen_init(struct hosted_screen *hosted_screen, int use_drm)
{
    hosted_screen->backend = &wayland_backend;

    hosted_screen->display =
	wl_display_connect(NULL);
    if (hosted_screen->display == NULL) {
	ErrorF("wl_display_create failed\n");
	return BadAlloc;
    }

    /* Set up listener so we'll catch all events. */
    wl_display_add_global_listener(hosted_screen->display,
				   global_handler, hosted_screen);

    /* Process connection events. */
    wl_display_iterate(hosted_screen->display, WL_DISPLAY_READABLE);

    hosted_screen->wayland_fd =
	wl_display_get_fd(hosted_screen->display,
			  source_update, hosted_screen);

    AddGeneralSocket(hosted_screen->wayland_fd);
    RegisterBlockAndWakeupHandlers(block_handler, wakeup_handler,
				   hosted_screen);

#ifdef WITH_LIBDRM
    if (use_drm) {
        int ret;

        if ((ret = wayland_drm_screen_init(hosted_screen)) != Success)
	    return ret;

        wl_display_iterate(hosted_screen->display, WL_DISPLAY_WRITABLE);
        while (!hosted_screen->authenticated)
            wl_display_iterate(hosted_screen->display, WL_DISPLAY_READABLE);
    }

#endif

    return Success;
}
