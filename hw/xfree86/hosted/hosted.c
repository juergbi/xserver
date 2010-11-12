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
#include <fcntl.h>
#include <wayland-util.h>
#include <wayland-client.h>
#include <X11/extensions/compositeproto.h>

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
#include <xf86drm.h>

#include "hosted.h"
#include "hosted-private.h"

/*
 * TODO:
 *  - lose X kb focus when wayland surface loses it
 *  - active grabs, grab owner crack
 */

static DevPrivateKeyRec hosted_window_private_key;
static DevPrivateKeyRec hosted_screen_private_key;

static int
input_proc(DeviceIntPtr device, int event)
{
    switch (event) {
    case DEVICE_INIT:
	break;
    case DEVICE_ON:
	break;
    case DEVICE_OFF:
	break;
    case DEVICE_CLOSE:
	break;
    }

    return Success;
}

static void
input_ptr_ctrl_proc(DeviceIntPtr device, PtrCtrl *ctrl)
{
	/* Nothing to do, dix handles all settings */
}

static void
input_kbd_ctrl(DeviceIntPtr device, KeybdCtrl *ctrl)
{
	/* FIXME: Set keyboard leds based on CAPSFLAG etc being set in
	 * ctrl->leds */
}

static int
input_init_pointer(struct hosted_input_device *d, struct hosted_screen *screen)
{
    DeviceIntPtr device;
    int min_x = 0, min_y = 0;
    int max_x = screen->width, max_y = screen->height;
    static unsigned char map[] = { 0, 1, 2, 3 };
    CARD32 atom;
    char *name = "hosted";
    Atom labels[3];

    device = AddInputDevice(serverClient, input_proc, TRUE);
    d->pointer = device;

    atom = MakeAtom(name, strlen(name), TRUE);
    AssignTypeAndName(device, atom, name);

    device->coreEvents = TRUE;
    device->type = SLAVE;
    device->spriteInfo->spriteOwner = FALSE;

    labels[0] = MakeAtom("x", 1, TRUE);
    labels[1] = MakeAtom("y", 1, TRUE);

    if (!InitValuatorClassDeviceStruct(device, 2, labels,
				       GetMotionHistorySize(), Absolute))
	return !Success;

    /* Valuators */
    InitValuatorAxisStruct(device, labels[0], 0, min_x, max_x, 10000, 0, 10000);
    InitValuatorAxisStruct(device, labels[1], 1, min_y, max_y, 10000, 0, 10000);

    if (!InitPtrFeedbackClassDeviceStruct(device, input_ptr_ctrl_proc))
	return !Success;

    /* FIXME: count number of actual buttons */
    labels[0] = MakeAtom("left", 4, TRUE);
    labels[1] = MakeAtom("middle", 6, TRUE);
    labels[2] = MakeAtom("right", 5, TRUE);
    if (!InitButtonClassDeviceStruct(device, 3, labels, map))
	return !Success;

    return Success;
}

static int
input_init_keyboard(struct hosted_input_device *d,
		    struct hosted_screen *screen)
{
    DeviceIntPtr device;
    CARD32 atom;
    char *name = "hosted";
    KeySymsRec key_syms;
    XkbRMLVOSet rmlvo;

    device = AddInputDevice(serverClient, input_proc, TRUE);
    d->keyboard = device;

    atom = MakeAtom(name, strlen(name), TRUE);
    AssignTypeAndName(device, atom, name);

    device->coreEvents = TRUE;
    device->type = SLAVE;
    device->spriteInfo->spriteOwner = FALSE;

    key_syms.map        = malloc(4 * 248 * sizeof(KeySym));
    key_syms.mapWidth   = 4;
    key_syms.minKeyCode = 8;
    key_syms.maxKeyCode = 254;

    rmlvo.rules = "evdev";
    rmlvo.model = "evdev";
    rmlvo.layout = "us";
    rmlvo.variant = NULL;
    rmlvo.options = NULL;
    if (!InitKeyboardDeviceStruct(device, &rmlvo, NULL, input_kbd_ctrl))
	return !Success;

    RegisterOtherDevice(device);
    ActivateDevice(device, FALSE);

    return Success;
}

static void
crtc_dpms(xf86CrtcPtr drmmode_crtc, int mode)
{
}

static Bool
crtc_set_mode_major(xf86CrtcPtr crtc, DisplayModePtr mode,
		    Rotation rotation, int x, int y)
{
	return TRUE;
}

static void
crtc_set_cursor_colors (xf86CrtcPtr crtc, int bg, int fg)
{
}

static void
crtc_set_cursor_position (xf86CrtcPtr crtc, int x, int y)
{
}

static void
crtc_show_cursor (xf86CrtcPtr crtc)
{
}

static void
crtc_hide_cursor (xf86CrtcPtr crtc)
{
}

static void
crtc_load_cursor_argb (xf86CrtcPtr crtc, CARD32 *image)
{
}

static PixmapPtr
crtc_shadow_create(xf86CrtcPtr crtc, void *data, int width, int height)
{
	return NULL;
}

static void *
crtc_shadow_allocate(xf86CrtcPtr crtc, int width, int height)
{
	return NULL;
}

static void
crtc_shadow_destroy(xf86CrtcPtr crtc, PixmapPtr rotate_pixmap, void *data)
{
}

static const xf86CrtcFuncsRec crtc_funcs = {
    .dpms                = crtc_dpms,
    .set_mode_major      = crtc_set_mode_major,
    .set_cursor_colors   = crtc_set_cursor_colors,
    .set_cursor_position = crtc_set_cursor_position,
    .show_cursor         = crtc_show_cursor,
    .hide_cursor         = crtc_hide_cursor,
    .load_cursor_argb    = crtc_load_cursor_argb,
    .shadow_create       = crtc_shadow_create,
    .shadow_allocate     = crtc_shadow_allocate,
    .shadow_destroy      = crtc_shadow_destroy,
    .destroy		 = NULL, /* XXX */
};

static void
output_dpms(xf86OutputPtr output, int mode)
{
	return;
}

static xf86OutputStatus
output_detect(xf86OutputPtr output)
{
	return XF86OutputStatusConnected;
}

static Bool
output_mode_valid(xf86OutputPtr output, DisplayModePtr pModes)
{
	return MODE_OK;
}

static DisplayModePtr
output_get_modes(xf86OutputPtr xf86output)
{
    struct hosted_output *output = xf86output->driver_private;
    struct monitor_ranges *ranges;
    DisplayModePtr modes;

    modes = xf86CVTMode(output->width, output->height, 60, TRUE, FALSE);
    output->monitor.det_mon[0].type = DS_RANGES;
    ranges = &output->monitor.det_mon[0].section.ranges;
    ranges->min_h = modes->HSync - 10;
    ranges->max_h = modes->HSync + 10;
    ranges->min_v = modes->VRefresh - 10;
    ranges->max_v = modes->VRefresh + 10;
    ranges->max_clock = modes->Clock + 100;
    output->monitor.det_mon[1].type = DT;
    output->monitor.det_mon[2].type = DT;
    output->monitor.det_mon[3].type = DT;

    xf86output->MonInfo = &output->monitor;

    return modes;
}

static void
output_destroy(xf86OutputPtr xf86output)
{
    struct xwl_output *output = xf86output->driver_private;

    free(output);
}

static const xf86OutputFuncsRec output_funcs = {
    .dpms	= output_dpms,
    .detect	= output_detect,
    .mode_valid	= output_mode_valid,
    .get_modes	= output_get_modes,
    .destroy	= output_destroy
};

struct hosted_output *
hosted_output_create(struct hosted_screen *hosted_screen)
{
    struct hosted_output *hosted_output;
    xf86OutputPtr xf86output;
    xf86CrtcPtr xf86crtc;

    hosted_output = malloc(sizeof *hosted_output);
    if (hosted_output == NULL) {
	ErrorF("create_output ENOMEM");
	return NULL;
    }
    hosted_output->hosted_screen = hosted_screen;

    xf86output = xf86OutputCreate(hosted_screen->scrninfo,
				  &output_funcs, "HOSTED-1");
    xf86output->driver_private = hosted_output;
    xf86output->mm_width = 300;
    xf86output->mm_height = 240;
    xf86output->subpixel_order = SubPixelHorizontalRGB;
    xf86output->possible_crtcs = 1;
    xf86output->possible_clones = 1;

    xf86crtc = xf86CrtcCreate(hosted_screen->scrninfo, &crtc_funcs);
    xf86crtc->driver_private = hosted_output;

    return hosted_output;
}

struct hosted_input_device *
hosted_input_device_create(struct hosted_screen *hosted_screen)
{
    struct hosted_input_device *hosted_input_device;

    hosted_input_device = malloc(sizeof *hosted_input_device);
    if (hosted_input_device == NULL) {
	ErrorF("create_output enomem");
	return NULL;
    }

    memset(hosted_input_device, 0, sizeof *hosted_input_device);
    hosted_input_device->hosted_screen = hosted_screen;

    list_add(&hosted_input_device->link, &hosted_screen->input_device_list);

    return hosted_input_device;
}

static void
add_input_devices(struct hosted_screen *hosted_screen)
{
    struct hosted_input_device *hosted_input_device;

    list_for_each_entry(hosted_input_device,
			&hosted_screen->input_device_list, link) {
	input_init_pointer(hosted_input_device, hosted_screen);
	input_init_keyboard(hosted_input_device, hosted_screen);
    }
}

static Bool
resize(ScrnInfoPtr scrn, int width, int height)
{
    if (scrn->virtualX == width && scrn->virtualY == height)
	return TRUE;

    scrn->virtualX = width;
    scrn->virtualY = height;

    return TRUE;
}

static const xf86CrtcConfigFuncsRec config_funcs = {
    resize
};

static void free_pixmap(void *data)
{
    PixmapPtr pixmap = data;
    ScreenPtr screen = pixmap->drawable.pScreen;

    (*screen->DestroyPixmap)(pixmap);
}

static void
hosted_window_attach(struct hosted_window *hosted_window, PixmapPtr pixmap)
{
    uint32_t name;
    struct hosted_screen *hosted_screen = hosted_window->hosted_screen;
    struct wl_buffer *buffer;

    if (hosted_screen->driver->name_pixmap (pixmap, &name) != Success) {
	ErrorF("failed to name buffer\n");
	return;
    }

    buffer = wl_drm_create_buffer(hosted_screen->drm,
				  name,
				   pixmap->drawable.width,
				  pixmap->drawable.height,
				  pixmap->devKind,
				  hosted_window->visual);
    wl_surface_attach(hosted_window->surface, buffer);
    wl_surface_map(hosted_window->surface,
		   hosted_window->window->drawable.x,
		   hosted_window->window->drawable.y,
		   pixmap->drawable.width,
		   pixmap->drawable.height);
    wl_buffer_destroy(buffer);

    wl_display_sync_callback(hosted_screen->display, free_pixmap, pixmap);
    pixmap->refcnt++;

    /*
     * DRI2 attach:
     *
     * Pixmap DRI2NameBuffer(DRI2BufferPtr *buffer);
     *
     * Not clear how we can render into the back buffer of a
     * compositing Xorg host.  We'd need a XCompositeSetPixmap or
     * something.
     */
}

static Bool
hosted_create_window(WindowPtr window)
{
    ScreenPtr screen = window->drawable.pScreen;
    struct hosted_screen *hosted_screen;
    Selection *selection;
    char buffer[32];
    int len, rc;
    Atom name;
    Bool ret;

    hosted_screen = dixLookupPrivate(&screen->devPrivates,
				     &hosted_screen_private_key);

    screen->CreateWindow = hosted_screen->CreateWindow;
    ret = (*screen->CreateWindow)(window);
    hosted_screen->CreateWindow = screen->CreateWindow;
    screen->CreateWindow = hosted_create_window;

    if (!(hosted_screen->flags & HOSTED_FLAGS_ROOTLESS) ||
	window->parent != NULL)
	return ret;

    len = snprintf(buffer, sizeof buffer, "_NET_WM_CM_S%d", screen->myNum);
    name = MakeAtom(buffer, len, TRUE);
    rc = AddSelection(&selection, name, serverClient);
    if (rc != Success)
	return ret;

    selection->lastTimeChanged = currentTime;
    selection->window = window->drawable.id;
    selection->pWin = window;
    selection->client = serverClient;

    CompositeRedirectSubwindows(window, CompositeRedirectManual);

    return ret;
}

static void
damage_report(DamagePtr pDamage, RegionPtr pRegion, void *data)
{
    struct hosted_window *hosted_window = data;
    struct hosted_screen *hosted_screen = hosted_window->hosted_screen;

    list_add(&hosted_window->link, &hosted_screen->damage_window_list);
}

static void
damage_destroy(DamagePtr pDamage, void *data)
{
}

static Bool
hosted_realize_window(WindowPtr window)
{
    ScreenPtr screen = window->drawable.pScreen;
    struct hosted_screen *hosted_screen;
    struct hosted_window *hosted_window;
    VisualID visual;
    Bool ret;
    int i;

    hosted_screen = dixLookupPrivate(&screen->devPrivates,
				     &hosted_screen_private_key);

    screen->RealizeWindow = hosted_screen->RealizeWindow;
    ret = (*screen->RealizeWindow)(window);
    hosted_screen->RealizeWindow = hosted_screen->RealizeWindow;
    screen->RealizeWindow = hosted_realize_window;

    if (hosted_screen->flags & HOSTED_FLAGS_ROOTLESS) {
	if (window->redirectDraw != RedirectDrawManual)
	    return ret;
    } else {
	if (window->parent)
	    return ret;
    }

    hosted_window = malloc(sizeof *hosted_window);
    hosted_window->hosted_screen = hosted_screen;
    hosted_window->window = window;
    hosted_window->surface =
	wl_compositor_create_surface(hosted_screen->compositor);
    if (hosted_window->surface == NULL) {
	ErrorF("wl_display_create_surface failed\n");
	return FALSE;
    }

    visual = wVisual(window);
    for (i = 0; i < screen->numVisuals; i++)
	if (screen->visuals[i].vid == visual)
	    break;

    if (screen->visuals[i].nplanes == 32)
	hosted_window->visual =
	    wl_display_get_premultiplied_argb_visual(hosted_screen->display);
    else
	hosted_window->visual =
	    wl_display_get_rgb_visual(hosted_screen->display);

    wl_surface_set_user_data(hosted_window->surface, hosted_window);
    hosted_window_attach(hosted_window, (*screen->GetWindowPixmap)(window));

    dixSetPrivate(&window->devPrivates,
		  &hosted_window_private_key, hosted_window);

    hosted_window->damage =
	DamageCreate(damage_report, damage_destroy, DamageReportNonEmpty,
		     FALSE, screen, hosted_window);
    DamageRegister(&window->drawable, hosted_window->damage);

    return ret;
}

static Bool
hosted_unrealize_window(WindowPtr window)
{
    ScreenPtr screen = window->drawable.pScreen;
    struct hosted_screen *hosted_screen;
    struct hosted_window *hosted_window;
    Bool ret;

    hosted_screen = dixLookupPrivate(&screen->devPrivates,
				     &hosted_screen_private_key);

    screen->UnrealizeWindow = hosted_screen->UnrealizeWindow;
    ret = (*screen->UnrealizeWindow)(window);
    hosted_screen->UnrealizeWindow = screen->UnrealizeWindow;
    screen->UnrealizeWindow = hosted_unrealize_window;

    hosted_window =
	dixLookupPrivate(&window->devPrivates, &hosted_window_private_key);
    if (hosted_window) {
	wl_surface_destroy(hosted_window->surface);
	free(hosted_window);
	dixSetPrivate(&window->devPrivates, &hosted_window_private_key, NULL);
    }

    return ret;
}

static void
hosted_set_window_pixmap(WindowPtr window, PixmapPtr pixmap)
{
    ScreenPtr screen = window->drawable.pScreen;
    struct hosted_screen *hosted_screen;
    struct hosted_window *hosted_window;

    hosted_screen = dixLookupPrivate(&screen->devPrivates,
				     &hosted_screen_private_key);

    screen->SetWindowPixmap = hosted_screen->SetWindowPixmap;
    (*screen->SetWindowPixmap)(window, pixmap);
    hosted_screen->SetWindowPixmap = screen->SetWindowPixmap;
    screen->SetWindowPixmap = hosted_set_window_pixmap;

    hosted_window =
	dixLookupPrivate(&window->devPrivates, &hosted_window_private_key);
    if (hosted_window)
	hosted_window_attach(hosted_window, pixmap);
}

static void
hosted_move_window(WindowPtr window, int x, int y,
		   WindowPtr sibling, VTKind kind)
{
    ScreenPtr screen = window->drawable.pScreen;
    struct hosted_screen *hosted_screen;
    struct hosted_window *hosted_window;

    hosted_screen = dixLookupPrivate(&screen->devPrivates,
				     &hosted_screen_private_key);

    screen->MoveWindow = hosted_screen->MoveWindow;
    (*screen->MoveWindow)(window, x, y, sibling, kind);
    hosted_screen->MoveWindow = screen->MoveWindow;
    screen->MoveWindow = hosted_move_window;

    hosted_window =
	dixLookupPrivate(&window->devPrivates, &hosted_window_private_key);
    if (hosted_window == NULL)
	return;

    wl_surface_map(hosted_window->surface,
		   hosted_window->window->drawable.x,
		   hosted_window->window->drawable.y,
		   hosted_window->window->drawable.width,
		   hosted_window->window->drawable.height);
}

int
hosted_screen_init(struct hosted_screen *hosted_screen, ScreenPtr screen)
{
    hosted_screen->screen = screen;

    if (!dixRegisterPrivateKey(&hosted_screen_private_key, PRIVATE_SCREEN, 0))
	return BadAlloc;

    if (!dixRegisterPrivateKey(&hosted_window_private_key, PRIVATE_WINDOW, 0))
	return BadAlloc;

    dixSetPrivate(&screen->devPrivates,
		  &hosted_screen_private_key, hosted_screen);

    hosted_screen->CreateWindow = screen->CreateWindow;
    screen->CreateWindow = hosted_create_window;

    hosted_screen->RealizeWindow = screen->RealizeWindow;
    screen->RealizeWindow = hosted_realize_window;

    hosted_screen->UnrealizeWindow = screen->UnrealizeWindow;
    screen->UnrealizeWindow = hosted_unrealize_window;

    hosted_screen->SetWindowPixmap = screen->SetWindowPixmap;
    screen->SetWindowPixmap = hosted_set_window_pixmap;

    hosted_screen->MoveWindow = screen->MoveWindow;
    screen->MoveWindow = hosted_move_window;

    add_input_devices(hosted_screen);

    return Success;
}

struct hosted_screen *
hosted_screen_pre_init(ScrnInfoPtr scrninfo,
		       uint32_t flags, struct hosted_driver *driver)
{
    struct hosted_screen *hosted_screen;

    /* FIXME: check hosted enabled flags and fullscreen/rootless flags
     * here */

    hosted_screen = malloc(sizeof *hosted_screen);
    if (hosted_screen == NULL) {
	ErrorF("malloc failed\n");
	return NULL;
    }

    memset(hosted_screen, 0, sizeof *hosted_screen);
    list_init(&hosted_screen->input_device_list);
    list_init(&hosted_screen->damage_window_list);
    hosted_screen->scrninfo = scrninfo;
    hosted_screen->driver = driver;
    hosted_screen->flags = flags;

    xf86CrtcConfigInit(scrninfo, &config_funcs);

    xf86CrtcSetSizeRange(scrninfo, 320, 200, 8192, 8192);

    if (wayland_screen_init(hosted_screen) != Success)
	return NULL;

    xf86InitialConfiguration(scrninfo, TRUE);

    return hosted_screen;
}

int hosted_screen_get_drm_fd(struct hosted_screen *hosted_screen)
{
    return hosted_screen->drm_fd;
}

void hosted_screen_destroy(struct hosted_screen *hosted_screen)
{
    wl_display_destroy(hosted_screen->display);
    close(hosted_screen->drm_fd);
    free(hosted_screen);
}

int hosted_screen_authenticate(struct hosted_screen *hosted_screen,
			       uint32_t magic)
{
    hosted_screen->authenticated = 0;
    wl_drm_authenticate (hosted_screen->drm, magic);
    wl_display_iterate (hosted_screen->display, WL_DISPLAY_WRITABLE);
    while (!hosted_screen->authenticated)
	wl_display_iterate (hosted_screen->display, WL_DISPLAY_READABLE);

    return Success;
}

/* DDX driver must call this after submitting the rendering */
void hosted_screen_post_damage(struct hosted_screen *hosted_screen)
{
    struct hosted_window *hosted_window;
    struct hosted_backend *backend = hosted_screen->backend;
    RegionPtr region;
    BoxPtr box;
    int count, i;

    list_for_each_entry(hosted_window,
			&hosted_screen->damage_window_list, link) {

	region = DamageRegion(hosted_window->damage);
	count = RegionNumRects(region);
	for (i = 0; i < count; i++) {
	    box = &RegionRects(region)[i];
	    backend->flush(hosted_window, box);
	}
	DamageEmpty(hosted_window->damage);
    }

    list_init(&hosted_screen->damage_window_list);
}

static pointer
hosted_setup(pointer module, pointer opts, int *errmaj, int *errmin)
{
    return (pointer) 1;
}

static XF86ModuleVersionInfo hosted_version_info = {
    "hosted",
    MODULEVENDORSTRING,
    MODINFOSTRING1,
    MODINFOSTRING2,
    XORG_VERSION_CURRENT,
    1, 0, 0,
    ABI_CLASS_EXTENSION,
    ABI_EXTENSION_VERSION,
    MOD_CLASS_NONE,
    { 0, 0, 0, 0 }
};

_X_EXPORT const XF86ModuleData hostedModuleData = {
    &hosted_version_info,
    &hosted_setup,
    NULL
};

int
hosted_version(void)
{
    return hosted_version_info.minorversion;
}
