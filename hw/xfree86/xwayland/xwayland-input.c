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

/*
 * TODO:
 *  - lose X kb focus when wayland surface loses it
 *  - active grabs, grab owner crack
 */

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
     * ctrl->leds */
}

static int
xwl_keyboard_proc(DeviceIntPtr device, int what)
{
    XkbRMLVOSet rmlvo;

    switch (what) {
    case DEVICE_INIT:
	device->public.on = FALSE;

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
device_added(struct xwl_input_device *xwl_input_device, const char *driver)
{
    DeviceIntPtr dev = NULL;
    InputInfoPtr pInfo;
    int rc;

    pInfo = xf86AllocateInput();
    if (!pInfo)
        return NULL;

    pInfo->driver = xstrdup(driver);

    if (asprintf(&pInfo->name, "%s:%d",
		 pInfo->driver, xwl_input_device->id) == -1) {
	free(pInfo);
	return NULL;
    }

    pInfo->private = xwl_input_device;

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
input_device_handle_motion(void *data, struct wl_input_device *input_device,
			   uint32_t time, wl_fixed_t sx_w, wl_fixed_t sy_w)
{
    struct xwl_input_device *xwl_input_device = data;
    struct xwl_screen *xwl_screen = xwl_input_device->xwl_screen;
    int32_t dx, dy, lx, ly;
    int sx = wl_fixed_to_int(sx_w);
    int sy = wl_fixed_to_int(sy_w);

    if (!xwl_input_device->focus_window)
	return ;

    dx = xwl_input_device->focus_window->window->drawable.x;
    dy = xwl_input_device->focus_window->window->drawable.y;

    lx = xf86ScaleAxis(sx + dx, 0xFFFF, 0, xwl_screen->scrninfo->virtualX, 0);
    ly = xf86ScaleAxis(sy + dy, 0xFFFF, 0, xwl_screen->scrninfo->virtualY, 0);

    xf86PostMotionEvent(xwl_input_device->pointer,
			TRUE, 0, 2, lx, ly);
}

static void
input_device_handle_button(void *data, struct wl_input_device *input_device,
			   uint32_t serial, uint32_t time, uint32_t button,
			   uint32_t state)
{
    struct xwl_input_device *xwl_input_device = data;
    int index;

    xwl_input_device->xwl_screen->serial = serial;

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
			uint32_t serial, uint32_t time, uint32_t key,
			uint32_t state)
{
    struct xwl_input_device *xwl_input_device = data;
    uint32_t modifier;

    xwl_input_device->xwl_screen->serial = serial;

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
input_device_handle_pointer_enter(void *data,
				  struct wl_input_device *input_device,
				  uint32_t serial, struct wl_surface *surface,
				  wl_fixed_t sx_w, wl_fixed_t sy_w)

{
    struct xwl_input_device *xwl_input_device = data;

    xwl_input_device->xwl_screen->serial = serial;
    xwl_input_device->pointer_enter_serial = serial;

    xwl_input_device->focus_window = wl_surface_get_user_data(surface);

    SetDeviceRedirectWindow(xwl_input_device->pointer,
                            xwl_input_device->focus_window->window);
}

static void
input_device_handle_keyboard_enter(void *data,
				   struct wl_input_device *input_device,
				   uint32_t serial,
				   struct wl_surface *surface,
				   struct wl_array *keys)
{
    struct xwl_input_device *xwl_input_device = data;
    uint32_t *k, *end;

    xwl_input_device->xwl_screen->serial = serial;

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

static void
input_device_handle_axis(void *data, struct wl_input_device *input_device,
                         uint32_t time, uint32_t axis, int32_t value)
{
    struct xwl_input_device *xwl_input_device = data;
    int index;

    if (value == 1)
        index = 4;
    else if (value == -1)
        index = 5;

    xf86PostButtonEvent(xwl_input_device->pointer, TRUE, index, 1, 0, 0);
    xf86PostButtonEvent(xwl_input_device->pointer, TRUE, index, 0, 0, 0);
}

static void
input_device_handle_pointer_leave(void *data,
                                  struct wl_input_device *input_device,
                                  uint32_t serial, struct wl_surface *surface)
{
    struct xwl_input_device *xwl_input_device = data;

    xwl_input_device->xwl_screen->serial = serial;

    xwl_input_device->focus_window = NULL;
    SetDeviceRedirectWindow(xwl_input_device->pointer, PointerRootWin);
}

static void
input_device_handle_keyboard_leave(void *data,
                                   struct wl_input_device *input_device,
                                   uint32_t serial,
                                   struct wl_surface *surface)
{
    struct xwl_input_device *xwl_input_device = data;

    xwl_input_device->xwl_screen->serial = serial;
}

static void
input_device_handle_touch_down(void *data,
                               struct wl_input_device *wl_input_device,
                               uint32_t serial, uint32_t time,
			       struct wl_surface *surface,
                               int32_t id, wl_fixed_t x, wl_fixed_t y)
{
}

static void
input_device_handle_touch_up(void *data,
                             struct wl_input_device *wl_input_device,
                             uint32_t serial, uint32_t time, int32_t id)
{
}

static void
input_device_handle_touch_motion(void *data,
                                 struct wl_input_device *wl_input_device,
                                 uint32_t time, int32_t id,
                                 wl_fixed_t x, wl_fixed_t y)
{
}

static void
input_device_handle_touch_frame(void *data,
				struct wl_input_device *wl_input_device)
{
}

static void
input_device_handle_touch_cancel(void *data,
				 struct wl_input_device *wl_input_device)
{
}

static const struct wl_input_device_listener input_device_listener = {
    input_device_handle_motion,
    input_device_handle_button,
    input_device_handle_axis,
    input_device_handle_key,
    input_device_handle_pointer_enter,
    input_device_handle_pointer_leave,
    input_device_handle_keyboard_enter,
    input_device_handle_keyboard_leave,
    input_device_handle_touch_down,
    input_device_handle_touch_up,
    input_device_handle_touch_motion,
    input_device_handle_touch_frame,
    input_device_handle_touch_cancel,
};

static void
create_input_device(struct xwl_screen *xwl_screen, uint32_t id,
		    uint32_t version)
{
    struct xwl_input_device *xwl_input_device;

    xwl_input_device = calloc(sizeof *xwl_input_device, 1);
    if (xwl_input_device == NULL) {
	ErrorF("create_input ENOMEM");
	return ;
    }

    xwl_input_device->xwl_screen = xwl_screen;
    xorg_list_add(&xwl_input_device->link, &xwl_screen->input_device_list);

    xwl_input_device->pointer =
	device_added(xwl_input_device, "xwayland-pointer");
    xwl_input_device->keyboard =
	device_added(xwl_input_device, "xwayland-keyboard");

    xwl_input_device->input_device =
        wl_display_bind(xwl_screen->display, id, &wl_input_device_interface);
    xwl_input_device->id = id;

    wl_input_device_add_listener(xwl_input_device->input_device,
				 &input_device_listener,
				 xwl_input_device);
}

static void
input_handler(struct wl_display *display,
	      uint32_t id,
	      const char *interface,
	      uint32_t version,
	      void *data)
{
    struct xwl_screen *xwl_screen = data;

    if (strcmp (interface, "wl_input_device") == 0) {
        create_input_device(xwl_screen, id, 1);
    }
}

void
xwl_input_init(struct xwl_screen *xwl_screen)
{
    xwl_screen->input_listener =
	wl_display_add_global_listener(xwl_screen->display,
				       input_handler, xwl_screen);
}
