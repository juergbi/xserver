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
#include <errno.h>
#include <sys/mman.h>
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
#include <xf86Priv.h>
#include <mipointrst.h>

#include "hosted.h"
#include "hosted-private.h"

#ifdef WITH_LIBDRM
#include "wayland-drm-client-protocol.h"
#endif

/*
 * TODO:
 *  - lose X kb focus when wayland surface loses it
 *  - active grabs, grab owner crack
 */

static DevPrivateKeyRec hosted_window_private_key;
static DevPrivateKeyRec hosted_screen_private_key;
static DevPrivateKeyRec hosted_cursor_private_key;
static DevPrivateKeyRec hosted_device_private_key;

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
    dixSetPrivate(&device->devPrivates, &hosted_device_private_key, d);

    atom = MakeAtom(name, strlen(name), TRUE);
    AssignTypeAndName(device, atom, name);

    device->coreEvents = TRUE;
    device->type = MASTER_POINTER;
    device->spriteInfo->spriteOwner = TRUE;

    labels[0] = MakeAtom("x", 1, TRUE);
    labels[1] = MakeAtom("y", 1, TRUE);

    if (!InitValuatorClassDeviceStruct(device, 2, labels,
				       GetMotionHistorySize(), Absolute))
	return !Success;

    /* Valuators */
    InitValuatorAxisStruct(device, 0, labels[0],
			   min_x, max_x, 10000, 0, 10000, Absolute);
    InitValuatorAxisStruct(device, 1, labels[1],
			   min_y, max_y, 10000, 0, 10000, Absolute);

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
    XkbRMLVOSet rmlvo;

    device = AddInputDevice(serverClient, input_proc, TRUE);
    d->keyboard = device;
    dixSetPrivate(&device->devPrivates, &hosted_device_private_key, d);

    atom = MakeAtom(name, strlen(name), TRUE);
    AssignTypeAndName(device, atom, name);

    device->coreEvents = TRUE;
    device->type = MASTER_KEYBOARD;
    device->spriteInfo->spriteOwner = FALSE;

    rmlvo.rules = "evdev";
    rmlvo.model = "evdev";
    rmlvo.layout = "us";
    rmlvo.variant = NULL;
    rmlvo.options = NULL;
    if (!InitKeyboardDeviceStruct(device, &rmlvo, NULL, input_kbd_ctrl))
	return !Success;

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
    struct hosted_output *output = xf86output->driver_private;

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

    hosted_output = calloc(sizeof *hosted_output, 1);
    if (hosted_output == NULL) {
	ErrorF("create_output ENOMEM");
	return NULL;
    }

    hosted_output->hosted_screen = hosted_screen;
    hosted_output->width = hosted_screen->width = 800;
    hosted_output->height = hosted_screen->height = 600;

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

static void
add_input_devices(struct hosted_screen *hosted_screen)
{
    struct hosted_input_device *hosted_input_device;

    if (!hosted_screen->initialized)
        return ;

    list_for_each_entry(hosted_input_device,
			&hosted_screen->input_device_list, link) {
        if (hosted_input_device->pointer || hosted_input_device->keyboard)
            continue ;
	input_init_pointer(hosted_input_device, hosted_screen);
	input_init_keyboard(hosted_input_device, hosted_screen);
    }
}

struct hosted_input_device *
hosted_input_device_create(struct hosted_screen *hosted_screen)
{
    struct hosted_input_device *hosted_input_device;

    hosted_input_device = calloc(sizeof *hosted_input_device, 1);
    if (hosted_input_device == NULL) {
	ErrorF("create_input ENOMEM");
	return NULL;
    }

    hosted_input_device->hosted_screen = hosted_screen;
    list_add(&hosted_input_device->link, &hosted_screen->input_device_list);

    add_input_devices(hosted_screen);

    return hosted_input_device;
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
    struct hosted_screen *hosted_screen = hosted_window->hosted_screen;

    if (hosted_window->buffer) {
	ErrorF("leaking a buffer");
    }

    hosted_screen->driver->create_window_buffer(hosted_window, pixmap);

    if (!hosted_window->buffer) {
        ErrorF("failed to create buffer\n");
	return;
    }

    wl_surface_attach(hosted_window->surface, hosted_window->buffer, 0, 0);
    wl_surface_map_toplevel(hosted_window->surface);

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

    /* First window is mapped, so now we can add input devices */
    if (!hosted_screen->initialized) {
        hosted_screen->initialized = 1;
        add_input_devices(hosted_screen);
    }

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

    hosted_window = calloc(sizeof *hosted_window, 1);
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
    if (hosted_window->buffer) {
        wl_buffer_destroy(hosted_window->buffer);
    }
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

    wl_surface_map_toplevel(hosted_window->surface);
}

static void
expand_source_and_mask(CursorPtr cursor, void *data)
{
    CARD32 *argb, *p, d, fg, bg;
    CursorBitsPtr bits = cursor->bits;
    int size;
    int x, y, stride, i, bit;

    size = bits->width * bits->height * 4;
    argb = malloc(size);
    if (argb == NULL)
	return;

    p = argb;
    fg = ((cursor->foreRed & 0xff00) << 8) |
	(cursor->foreGreen & 0xff00) | (cursor->foreGreen >> 8);
    bg = ((cursor->backRed & 0xff00) << 8) |
	(cursor->backGreen & 0xff00) | (cursor->backGreen >> 8);
    stride = (bits->width / 8 + 3) & ~3;
    for (y = 0; y < bits->height; y++)
	for (x = 0; x < bits->width; x++) {
	    i = y * stride + x / 8;
	    bit = 1 << (x & 7);
	    if (bits->mask[i] & bit)
		d = 0xff000000;
	    else
		d = 0x00000000;
	    if (bits->source[i] & bit)
		d |= fg;
	    else
		d |= bg;

	    *p++ = d;
	}

    memcpy(data, argb, size);
    free(argb);
}

static Bool
hosted_realize_cursor(DeviceIntPtr device, ScreenPtr screen, CursorPtr cursor)
{
    struct hosted_screen *hosted_screen;
    int size;
    char filename[] = "/tmp/wayland-shm-XXXXXX";
    int fd;
    struct wl_buffer *buffer;
    struct wl_visual *visual;
    void *data;

    hosted_screen = dixLookupPrivate(&screen->devPrivates,
				     &hosted_screen_private_key);
    size = cursor->bits->width * cursor->bits->height * 4;

    fd = mkstemp(filename);
    if (fd < 0) {
	ErrorF("open %s failed: %s", filename, strerror(errno));
	return FALSE;
    }
    if (ftruncate(fd, size) < 0) {
	ErrorF("ftruncate failed: %s", strerror(errno));
	close(fd);
	return FALSE;
    }

    data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    unlink(filename);

    if (data == MAP_FAILED) {
	ErrorF("mmap failed: %s", strerror(errno));
	close(fd);
	return FALSE;
    }

    if (cursor->bits->argb)
	memcpy(data, cursor->bits->argb, size);
    else
	expand_source_and_mask(cursor, data);
    munmap(data, size);

    visual = wl_display_get_argb_visual(hosted_screen->display);
    buffer = wl_shm_create_buffer(hosted_screen->shm, fd,
				  cursor->bits->width, cursor->bits->height,
				  cursor->bits->width * 4, visual);
    close(fd);

    dixSetPrivate(&cursor->devPrivates, &hosted_cursor_private_key, buffer);

    return TRUE;
}

static Bool
hosted_unrealize_cursor(DeviceIntPtr device,
			ScreenPtr screen, CursorPtr cursor)
{
    struct wl_buffer *buffer;

    buffer = dixGetPrivate(&cursor->devPrivates, &hosted_cursor_private_key);
    wl_buffer_destroy(buffer);

    return TRUE;
}

static void
hosted_set_cursor(DeviceIntPtr device,
		  ScreenPtr screen, CursorPtr cursor, int x, int y)
{
    struct hosted_input_device *hosted_input_device;
    struct wl_buffer *buffer;

    if (!cursor)
	return;

    hosted_input_device =
	dixGetPrivate(&device->devPrivates, &hosted_device_private_key);
    if (!hosted_input_device)
	return;

    buffer = dixGetPrivate(&cursor->devPrivates, &hosted_cursor_private_key);

    wl_input_device_attach(hosted_input_device->input_device,
			   hosted_input_device->time, buffer,
			   cursor->bits->xhot, cursor->bits->yhot);
}

static void
hosted_move_cursor(DeviceIntPtr device, ScreenPtr screen, int x, int y)
{
}

static Bool
hosted_device_cursor_initialize(DeviceIntPtr device, ScreenPtr screen)
{
    struct hosted_screen *hosted_screen;

    hosted_screen = dixLookupPrivate(&screen->devPrivates,
				     &hosted_screen_private_key);

    return hosted_screen->sprite_funcs->DeviceCursorInitialize(device,
							       screen);
}

static void
hosted_device_cursor_cleanup(DeviceIntPtr device, ScreenPtr screen)
{
    struct hosted_screen *hosted_screen;

    hosted_screen = dixLookupPrivate(&screen->devPrivates,
				     &hosted_screen_private_key);

    hosted_screen->sprite_funcs->DeviceCursorCleanup(device, screen);
}

static miPointerSpriteFuncRec hosted_pointer_sprite_funcs =
{
    hosted_realize_cursor,
    hosted_unrealize_cursor,
    hosted_set_cursor,
    hosted_move_cursor,
    hosted_device_cursor_initialize,
    hosted_device_cursor_cleanup
};

int
hosted_screen_init(struct hosted_screen *hosted_screen, ScreenPtr screen)
{
    miPointerScreenPtr pointer_priv;

    hosted_screen->screen = screen;

    if (!dixRegisterPrivateKey(&hosted_screen_private_key, PRIVATE_SCREEN, 0))
	return BadAlloc;

    if (!dixRegisterPrivateKey(&hosted_window_private_key, PRIVATE_WINDOW, 0))
	return BadAlloc;

    if (!dixRegisterPrivateKey(&hosted_cursor_private_key, PRIVATE_CURSOR, 0))
	return BadAlloc;

    if (!dixRegisterPrivateKey(&hosted_device_private_key, PRIVATE_DEVICE, 0))
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

    pointer_priv = dixLookupPrivate(&screen->devPrivates, miPointerScreenKey);
    hosted_screen->sprite_funcs = pointer_priv->spriteFuncs;
    pointer_priv->spriteFuncs = &hosted_pointer_sprite_funcs;

    return Success;
}

struct hosted_screen *
hosted_screen_pre_init(ScrnInfoPtr scrninfo,
		       uint32_t flags, struct hosted_driver *driver)
{
    struct hosted_screen *hosted_screen;

    hosted_screen = calloc(sizeof *hosted_screen, 1);
    if (hosted_screen == NULL) {
	ErrorF("calloc failed\n");
	return NULL;
    }

    list_init(&hosted_screen->input_device_list);
    list_init(&hosted_screen->damage_window_list);
    hosted_screen->scrninfo = scrninfo;
    hosted_screen->driver = driver;
    hosted_screen->flags = flags;

    if (xorgRootless)
	hosted_screen->flags |= HOSTED_FLAGS_ROOTLESS;

    xf86CrtcConfigInit(scrninfo, &config_funcs);

    xf86CrtcSetSizeRange(scrninfo, 320, 200, 8192, 8192);

    if (wayland_screen_init(hosted_screen, driver->use_drm) != Success)
	return NULL;

    xf86InitialConfiguration(scrninfo, TRUE);

    return hosted_screen;
}

int hosted_screen_get_drm_fd(struct hosted_screen *hosted_screen)
{
    return hosted_screen->drm_fd;
}


#ifdef WITH_LIBDRM
int
hosted_create_window_buffer_drm(struct hosted_window *hosted_window,
				PixmapPtr pixmap, uint32_t name)
{
    hosted_window->buffer =
      wl_drm_create_buffer(hosted_window->hosted_screen->drm,
			   name,
			   pixmap->drawable.width,
			   pixmap->drawable.height,
			   pixmap->devKind,
			   hosted_window->visual);

    return hosted_window->buffer ? Success : BadDrawable;
}
#endif

int
hosted_create_window_buffer_shm(struct hosted_window *hosted_window,
				PixmapPtr pixmap, int fd)
{
    hosted_window->buffer =
      wl_shm_create_buffer(hosted_window->hosted_screen->shm, fd,
			   pixmap->drawable.width, pixmap->drawable.height,
			   pixmap->drawable.width * 4, hosted_window->visual);

    return hosted_window->buffer ? Success : BadDrawable;
}

void hosted_screen_destroy(struct hosted_screen *hosted_screen)
{
    wl_display_destroy(hosted_screen->display);
    if (hosted_screen->drm_fd >= 0)
        close(hosted_screen->drm_fd);
    free(hosted_screen);
}

int hosted_screen_authenticate(struct hosted_screen *hosted_screen,
			       uint32_t magic)
{
    hosted_screen->authenticated = 0;
#ifdef WITH_LIBDRM
    if (hosted_screen->drm)
        wl_drm_authenticate (hosted_screen->drm, magic);
#endif
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
