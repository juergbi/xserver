/*
 * Copyright © 2008 Kristian Høgsberg
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

#include <errno.h>
#include <stdint.h>
#include <unistd.h>
#include <linux/input.h>
#include <wayland-util.h>
#include <wayland-client.h>
#include <X11/extensions/compositeproto.h>
#include <xserver-properties.h>

#include <compositeext.h>
#include <selection.h>
#include <extinit.h>
#include <exevents.h>
#include <input.h>
#include <inpututils.h>
#include <inputstr.h>
#include <exevents.h>
#include <xkbsrv.h>
#include <xf86Xinput.h>
#include <xf86Crtc.h>
#include <xf86str.h>
#include <windowstr.h>
#include <xf86Priv.h>
#include <mipointrst.h>

#include "xwayland.h"
#include "xwayland-private.h"
#include "xserver-client-protocol.h"

extern const struct xserver_listener xwl_server_listener;

static void
xwl_pointer_control(DeviceIntPtr device, PtrCtrl *ctrl)
{
	/* Nothing to do, dix handles all settings */
}

static int
xwl_pointer_proc(DeviceIntPtr device, int what)
{
#define NBUTTONS 10
#define NAXES 2
    BYTE map[NBUTTONS + 1];
    int i = 0;
    Atom btn_labels[NBUTTONS] = {0};
    Atom axes_labels[NAXES] = {0};

    switch (what) {
    case DEVICE_INIT:
	device->public.on = FALSE;

        for (i = 1; i <= NBUTTONS; i++)
            map[i] = i;

        btn_labels[0] = XIGetKnownProperty(BTN_LABEL_PROP_BTN_LEFT);
        btn_labels[1] = XIGetKnownProperty(BTN_LABEL_PROP_BTN_MIDDLE);
        btn_labels[2] = XIGetKnownProperty(BTN_LABEL_PROP_BTN_RIGHT);
        btn_labels[3] = XIGetKnownProperty(BTN_LABEL_PROP_BTN_WHEEL_UP);
        btn_labels[4] = XIGetKnownProperty(BTN_LABEL_PROP_BTN_WHEEL_DOWN);
        btn_labels[5] = XIGetKnownProperty(BTN_LABEL_PROP_BTN_HWHEEL_LEFT);
        btn_labels[6] = XIGetKnownProperty(BTN_LABEL_PROP_BTN_HWHEEL_RIGHT);
        /* don't know about the rest */

        axes_labels[0] = XIGetKnownProperty(AXIS_LABEL_PROP_ABS_X);
        axes_labels[1] = XIGetKnownProperty(AXIS_LABEL_PROP_ABS_Y);

        if (!InitValuatorClassDeviceStruct(device, 2, btn_labels,
                                           GetMotionHistorySize(), Absolute))
            return BadValue;

        /* Valuators */
        InitValuatorAxisStruct(device, 0, axes_labels[0],
                               0, 0xFFFF, 10000, 0, 10000, Absolute);
        InitValuatorAxisStruct(device, 1, axes_labels[1],
                               0, 0xFFFF, 10000, 0, 10000, Absolute);

        if (!InitPtrFeedbackClassDeviceStruct(device, xwl_pointer_control))
            return BadValue;

        if (!InitButtonClassDeviceStruct(device, 3, btn_labels, map))
            return BadValue;

        return Success;

    case DEVICE_ON:
	device->public.on = TRUE;
        return Success;

    case DEVICE_OFF:
    case DEVICE_CLOSE:
	device->public.on = FALSE;
        return Success;
    }

    return BadMatch;

#undef NBUTTONS
#undef NAXES
}

static void
xwl_keyboard_control(DeviceIntPtr device, KeybdCtrl *ctrl)
{
    /* FIXME: Set keyboard leds based on CAPSFLAG etc being set in
     * ctrl->leds - needs private protocol. */
}

static int
xwl_keyboard_proc(DeviceIntPtr device, int what)
{
    XkbRMLVOSet rmlvo;

    switch (what) {
    case DEVICE_INIT:
	device->public.on = FALSE;

        /* FIXME: Get the keymap from wl_keyboard::keymap events, which
         *        requires more X server API to set a keymap from a string
         *        rather than RMLVO. */
        rmlvo.rules = "evdev";
        rmlvo.model = "evdev";
        rmlvo.layout = "us";
        rmlvo.variant = NULL;
        rmlvo.options = NULL;

        if (!InitKeyboardDeviceStruct(device, &rmlvo, NULL, xwl_keyboard_control))
            return BadValue;

        return Success;
    case DEVICE_ON:
	device->public.on = TRUE;
        return Success;

    case DEVICE_OFF:
    case DEVICE_CLOSE:
	device->public.on = FALSE;
        return Success;
    }

    return BadMatch;
}

static void
xwl_keyboard_uninit(InputDriverPtr drv, InputInfoPtr pInfo, int flags)
{
}

static int
xwl_keyboard_init(InputDriverPtr drv, InputInfoPtr pInfo, int flags)
{
    pInfo->type_name = "xwayland-keyboard";
    pInfo->device_control = xwl_keyboard_proc;
    pInfo->read_input = NULL;
    pInfo->control_proc = NULL;
    pInfo->switch_mode = NULL;
    pInfo->fd = -1;

    return Success;
}

_X_EXPORT InputDriverRec xwl_keyboard_driver = {
    1,
    "xwayland-keyboard",
    NULL,
    xwl_keyboard_init,
    xwl_keyboard_uninit,
    NULL,
};

static void
xwl_pointer_uninit(InputDriverPtr drv, InputInfoPtr pInfo, int flags)
{
}

static int
xwl_pointer_init(InputDriverPtr drv, InputInfoPtr pInfo, int flags)
{
    pInfo->type_name = "xwayland-pointer";
    pInfo->device_control = xwl_pointer_proc;
    pInfo->read_input = NULL;
    pInfo->control_proc = NULL;
    pInfo->switch_mode = NULL;
    pInfo->fd = -1;

    return Success;
}

_X_EXPORT InputDriverRec xwl_pointer_driver = {
    1,
    "xwayland-pointer",
    NULL,
    xwl_pointer_init,
    xwl_pointer_uninit,
    NULL,
};

void
xwl_input_teardown(pointer p)
{
}

pointer
xwl_input_setup(pointer module, pointer opts, int *errmaj, int *errmin)
{
    xf86AddInputDriver(&xwl_keyboard_driver, module, 0);
    xf86AddInputDriver(&xwl_pointer_driver, module, 0);

    return module;
}

static DeviceIntPtr
device_added(struct xwl_seat *xwl_seat, const char *driver)
{
    DeviceIntPtr dev = NULL;
    InputInfoPtr pInfo;
    int rc;

    pInfo = xf86AllocateInput();
    if (!pInfo)
        return NULL;

    pInfo->driver = xstrdup(driver);

    if (asprintf(&pInfo->name, "%s:%d", pInfo->driver, xwl_seat->id) == -1) {
	free(pInfo);
	return NULL;
    }

    pInfo->private = xwl_seat;

    rc = xf86NewInputDevice(pInfo, &dev, 1);
    if (rc != Success) {
	free(pInfo);
	return NULL;
    }

    LogMessage(X_INFO, "config/xwayland: Adding input device %s\n",
	       pInfo->name);

    return dev;
}

static void
pointer_handle_enter(void *data, struct wl_pointer *pointer,
		     uint32_t serial, struct wl_surface *surface,
		     wl_fixed_t sx_w, wl_fixed_t sy_w)

{
    struct xwl_seat *xwl_seat = data;
    DeviceIntPtr dev = xwl_seat->pointer;
    int i;
    int sx = wl_fixed_to_int(sx_w);
    int sy = wl_fixed_to_int(sy_w);
    ScreenPtr pScreen = xwl_seat->xwl_screen->screen;

    xwl_seat->xwl_screen->serial = serial;
    xwl_seat->pointer_enter_serial = serial;

    xwl_seat->focus_window = wl_surface_get_user_data(surface);

    (*pScreen->SetCursorPosition) (dev, pScreen, sx, sy, TRUE);

    SetDeviceRedirectWindow(xwl_seat->pointer, xwl_seat->focus_window->window);

    /* Ideally, X clients shouldn't see these button releases.  When
     * the pointer leaves a window with buttons down, it means that
     * the wayland compositor has grabbed the pointer.  The button
     * release event is consumed by whatever grab in the compositor
     * and won't be sent to clients (the X server is a client).
     * However, we need to reset X's idea of which buttons are up and
     * down, and they're all up (by definition) when the pointer
     * enters a window.  We should figure out a way to swallow these
     * events, perhaps using an X grab whenever the pointer is not in
     * any X window, but for now just send the events. */
    for (i = 0; i < dev->button->numButtons; i++)
	if (BitIsOn(dev->button->down, i))
		xf86PostButtonEvent(dev, TRUE, i, 0, 0, 0);

    (*pScreen->DisplayCursor)(dev, pScreen, dev->spriteInfo->sprite->current);
}

static void
pointer_handle_leave(void *data, struct wl_pointer *pointer,
		     uint32_t serial, struct wl_surface *surface)
{
    struct xwl_seat *xwl_seat = data;
    DeviceIntPtr dev = xwl_seat->pointer;
    ScreenPtr pScreen = xwl_seat->xwl_screen->screen;

    xwl_seat->xwl_screen->serial = serial;

    xwl_seat->focus_window = NULL;
    SetDeviceRedirectWindow(xwl_seat->pointer, PointerRootWin);
    (*pScreen->DisplayCursor)(dev, pScreen, NullCursor);
}

static void
pointer_handle_motion(void *data, struct wl_pointer *pointer,
		      uint32_t time, wl_fixed_t sx_w, wl_fixed_t sy_w)
{
    struct xwl_seat *xwl_seat = data;
    struct xwl_screen *xwl_screen = xwl_seat->xwl_screen;
    int32_t dx, dy, lx, ly;
    int sx = wl_fixed_to_int(sx_w);
    int sy = wl_fixed_to_int(sy_w);
    ValuatorMask mask;

    if (!xwl_seat->focus_window)
	return ;

    dx = xwl_seat->focus_window->window->drawable.x;
    dy = xwl_seat->focus_window->window->drawable.y;

    valuator_mask_zero(&mask);
    valuator_mask_set(&mask, 0, dx + sx);
    valuator_mask_set(&mask, 1, dy + sy);

    QueuePointerEvents(xwl_seat->pointer, MotionNotify, 0,
		       POINTER_ABSOLUTE | POINTER_SCREEN, &mask);
}

static void
pointer_handle_button(void *data, struct wl_pointer *pointer, uint32_t serial,
		      uint32_t time, uint32_t button, uint32_t state)
{
    struct xwl_seat *xwl_seat = data;
    int index;

    xwl_seat->xwl_screen->serial = serial;

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

    xf86PostButtonEvent(xwl_seat->pointer, TRUE, index, state, 0, 0);
}

static void
pointer_handle_axis(void *data, struct wl_pointer *pointer,
		    uint32_t time, uint32_t axis, wl_fixed_t value)
{
    struct xwl_seat *xwl_seat = data;
    int index;
    int val = wl_fixed_to_int(value);

    /* FIXME: Need to do proper smooth scrolling here! */
    if (val == 1)
        index = 4;
    else if (val == -1)
        index = 5;
    else
        return;

    xf86PostButtonEvent(xwl_seat->pointer, TRUE, index, 1, 0, 0);
    xf86PostButtonEvent(xwl_seat->pointer, TRUE, index, 0, 0, 0);
}

static const struct wl_pointer_listener pointer_listener = {
	pointer_handle_enter,
	pointer_handle_leave,
	pointer_handle_motion,
	pointer_handle_button,
	pointer_handle_axis,
};

static void
keyboard_handle_key(void *data, struct wl_keyboard *keyboard, uint32_t serial,
		    uint32_t time, uint32_t key, uint32_t state)
{
    struct xwl_seat *xwl_seat = data;
    uint32_t *k, *end;

    xwl_seat->xwl_screen->serial = serial;

    end = xwl_seat->keys.data + xwl_seat->keys.size;
    for (k = xwl_seat->keys.data; k < end; k++) {
	if (*k == key)
	    *k = *--end;
    }
    xwl_seat->keys.size = (void *) end - xwl_seat->keys.data;
    if (state) {
	k = wl_array_add(&xwl_seat->keys, sizeof *k);
	*k = key;
    }

    xf86PostKeyboardEvent(xwl_seat->keyboard, key + 8, state);
}

static void
keyboard_handle_keymap(void *data, struct wl_keyboard *keyboard,
		       uint32_t format, int fd, uint32_t size)
{
    /* FIXME: Handle keymap */

    close(fd);
}

static void
keyboard_handle_enter(void *data, struct wl_keyboard *keyboard,
		      uint32_t serial,
		      struct wl_surface *surface, struct wl_array *keys)
{
    struct xwl_seat *xwl_seat = data;
    uint32_t *k;

    xwl_seat->xwl_screen->serial = serial;

    wl_array_copy(&xwl_seat->keys, keys);
    wl_array_for_each(k, &xwl_seat->keys)
	xf86PostKeyboardEvent(xwl_seat->keyboard, *k + 8, 1);
}

static void
keyboard_handle_leave(void *data, struct wl_keyboard *keyboard,
		      uint32_t serial, struct wl_surface *surface)
{
    struct xwl_seat *xwl_seat = data;
    uint32_t *k;

    xwl_seat->xwl_screen->serial = serial;

    wl_array_for_each(k, &xwl_seat->keys)
	xf86PostKeyboardEvent(xwl_seat->keyboard, *k + 8, 0);
}

static void
keyboard_handle_modifiers(void *data, struct wl_keyboard *keyboard,
			  uint32_t serial, uint32_t mods_depressed,
			  uint32_t mods_latched, uint32_t mods_locked,
			  uint32_t group)
{
    /* FIXME: Need more server XKB API here. */
}

static const struct wl_keyboard_listener keyboard_listener = {
	keyboard_handle_keymap,
	keyboard_handle_enter,
	keyboard_handle_leave,
	keyboard_handle_key,
	keyboard_handle_modifiers,
};

static void
seat_handle_capabilities(void *data, struct wl_seat *seat,
			 enum wl_seat_capability caps)
{
	struct xwl_seat *xwl_seat = data;

	if (caps & WL_SEAT_CAPABILITY_POINTER) {
	    xwl_seat->pointer = device_added(xwl_seat, "xwayland-pointer");
	    xwl_seat->wl_pointer = wl_seat_get_pointer(seat);
	    wl_pointer_add_listener(xwl_seat->wl_pointer,
				    &pointer_listener, xwl_seat);
            xwl_seat_set_cursor(xwl_seat);
	}

	if (caps & WL_SEAT_CAPABILITY_KEYBOARD) {
	    xwl_seat->keyboard = device_added(xwl_seat, "xwayland-keyboard");
	    xwl_seat->wl_keyboard = wl_seat_get_keyboard(seat);
	    wl_keyboard_add_listener(xwl_seat->wl_keyboard,
				     &keyboard_listener, xwl_seat);
	}
        /* FIXME: Touch ... */
}

static const struct wl_seat_listener seat_listener = {
	seat_handle_capabilities,
};

static void
create_input_device(struct xwl_screen *xwl_screen, uint32_t id)
{
    struct xwl_seat *xwl_seat;

    xwl_seat = calloc(sizeof *xwl_seat, 1);
    if (xwl_seat == NULL) {
	ErrorF("create_input ENOMEM");
	return ;
    }

    xwl_seat->xwl_screen = xwl_screen;
    xorg_list_add(&xwl_seat->link, &xwl_screen->seat_list);

    xwl_seat->seat =
	wl_registry_bind(xwl_screen->registry, id, &wl_seat_interface, 1);
    xwl_seat->id = id;

    xwl_seat->cursor = wl_compositor_create_surface(xwl_screen->compositor);
    wl_seat_add_listener(xwl_seat->seat, &seat_listener, xwl_seat);
    wl_array_init(&xwl_seat->keys);
}

static void
input_handler(void *data, struct wl_registry *registry, uint32_t id,
	      const char *interface, uint32_t version)
{
    struct xwl_screen *xwl_screen = data;

    if (strcmp (interface, "wl_seat") == 0) {
        create_input_device(xwl_screen, id);
    } else if (strcmp(interface, "xserver") == 0) {
        xwl_screen->xorg_server =
            wl_registry_bind(registry, id, &xserver_interface, 1);
        xserver_add_listener(xwl_screen->xorg_server, &xwl_server_listener,
                             xwl_screen);
    }
}

static const struct wl_registry_listener input_listener = {
    input_handler,
};

void
xwl_input_init(struct xwl_screen *xwl_screen)
{
    xwl_screen->input_registry = wl_display_get_registry(xwl_screen->display);
    wl_registry_add_listener(xwl_screen->input_registry, &input_listener,
                             xwl_screen);
}
