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
#include <wayland-util.h>
#include <wayland-client.h>
#include <X11/extensions/compositeproto.h>
#include <xserver-properties.h>

#include <compositeext.h>
#include <selection.h>
#include <extinit.h>
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

    fprintf(stderr, "input proc %p %d\n", device, what);

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
    fprintf(stderr, "keyboard proc %p %d\n", device, what);
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
    xwl_keyboard_proc(pInfo->dev, DEVICE_OFF);
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
    xwl_pointer_proc(pInfo->dev, DEVICE_OFF);
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

static void
add_option(InputOption **options, const char *key, const char *value)
{
    if (!value || *value == '\0')
        return;

    for (; *options; options = &(*options)->next)
        ;
    *options = calloc(sizeof(**options), 1);
    if (!*options) /* Yeesh. */
        return;
    (*options)->key = strdup(key);
    (*options)->value = strdup(value);
    (*options)->next = NULL;
}

static DeviceIntPtr
device_added(struct xwl_input_device *xwl_input_device, const char *driver)
{
    InputOption *options = NULL, *tmpo = NULL;
    InputAttributes attrs = {0};
    DeviceIntPtr dev = NULL;
    char *config_info = NULL;
    char *xwl_input_ptr = NULL;
    char *name = NULL;
    int rc;

    if (asprintf(&config_info, "%s:%d", driver, xwl_input_device->id) == -1) {
        config_info = NULL;
        goto unwind;
    }

    if (asprintf(&xwl_input_ptr, "%p", xwl_input_device) == -1) {
	xwl_input_ptr = NULL;
        goto unwind;
    }

    name = config_info;

    options = calloc(sizeof(*options), 1);
    if (!options)
        return NULL;

    options->key = strdup("_source");
    options->value = strdup("server/xwayland");
    if (!options->key || !options->value)
        goto unwind;

    add_option(&options, "name", name);
    add_option(&options, "config_info", config_info);
    add_option(&options, "driver", driver);

    /* Wow ! super ugly ! */
    add_option(&options, "xwl_input_ptr", xwl_input_ptr);

    if (strstr(driver, "keyboard"))
	attrs.flags |= ATTR_KEYBOARD;
    if (strstr(driver, "pointer"))
	attrs.flags |= ATTR_POINTER;

    LogMessage(X_INFO, "config/xwayland: Adding input device %s\n", name);
    if ((rc = NewInputDeviceRequest(options, &attrs, &dev)) != Success) {
        LogMessage(X_ERROR, "config/xwayland: NewInputDeviceRequest failed (%d)\n", rc);
    }

    if (dev) {
	InputInfoPtr pInfo  = dev->public.devicePrivate;
	pInfo->private = xwl_input_device;
    }

    return dev;

unwind:
    free(config_info);
    free(xwl_input_ptr);
    while ((tmpo = options)) {
        options = tmpo->next;
        free(tmpo->key);        /* NULL if dev != NULL */
        free(tmpo->value);      /* NULL if dev != NULL */
        free(tmpo);
    }

    return NULL;
}

/* Use that code if the compositor want to delete an input device
static void
device_removed(DeviceIntPtr dev)
{
    LogMessage(X_INFO, "config/xwayland: removing device %s\n", dev->name);

    OsBlockSignals();
    ProcessInputEvents();
    DeleteInputDeviceRequest(dev);
    OsReleaseSignals();
}
*/

static void
add_input_device(struct xwl_input_device *xwl_input_device)
{
    if (!xwl_input_device->pointer)
	xwl_input_device->pointer =
	    device_added(xwl_input_device, "xwayland-pointer");
    if (!xwl_input_device->keyboard)
	xwl_input_device->keyboard =
	    device_added(xwl_input_device, "xwayland-keyboard");
}

static void
add_input_devices(struct xwl_screen *xwl_screen)
{
    struct xwl_input_device *xwl_input_device;

    list_for_each_entry(xwl_input_device,
			&xwl_screen->input_device_list, link)
	add_input_device(xwl_input_device);
}

void
xwl_input_init(struct xwl_screen *xwl_screen)
{
    xwl_screen->input_initialized = 1;
    add_input_devices(xwl_screen);
}

struct xwl_input_device *
xwl_input_device_create(struct xwl_screen *xwl_screen)
{
    struct xwl_input_device *xwl_input_device;

    xwl_input_device = calloc(sizeof *xwl_input_device, 1);
    if (xwl_input_device == NULL) {
	ErrorF("create_input ENOMEM");
	return NULL;
    }

    xwl_input_device->xwl_screen = xwl_screen;
    list_add(&xwl_input_device->link, &xwl_screen->input_device_list);

    if (xwl_screen->input_initialized)
	add_input_device(xwl_input_device);

    return xwl_input_device;
}
