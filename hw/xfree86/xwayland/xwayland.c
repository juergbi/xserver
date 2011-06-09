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
#include "drm-client-protocol.h"

#ifdef WITH_LIBDRM
#include "wayland-drm-client-protocol.h"
#endif

/*
 * TODO:
 *  - lose X kb focus when wayland surface loses it
 *  - active grabs, grab owner crack
 */

static DevPrivateKeyRec xwl_window_private_key;
static DevPrivateKeyRec xwl_screen_private_key;
static DevPrivateKeyRec xwl_cursor_private_key;

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
    struct xwl_output *output = xf86output->driver_private;
    struct monitor_ranges *ranges;
    DisplayModePtr modes;

    modes = xf86CVTMode(output->width, output->height, 60, TRUE, FALSE);
    output->xf86monitor.det_mon[0].type = DS_RANGES;
    ranges = &output->xf86monitor.det_mon[0].section.ranges;
    ranges->min_h = modes->HSync - 10;
    ranges->max_h = modes->HSync + 10;
    ranges->min_v = modes->VRefresh - 10;
    ranges->max_v = modes->VRefresh + 10;
    ranges->max_clock = modes->Clock + 100;
    output->xf86monitor.det_mon[1].type = DT;
    output->xf86monitor.det_mon[2].type = DT;
    output->xf86monitor.det_mon[3].type = DT;
    output->xf86monitor.no_sections = 0;

    xf86output->MonInfo = &output->xf86monitor;

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

struct xwl_output *
xwl_output_create(struct xwl_screen *xwl_screen)
{
    struct xwl_output *xwl_output;
    xf86OutputPtr xf86output;
    xf86CrtcPtr xf86crtc;

    xwl_output = calloc(sizeof *xwl_output, 1);
    if (xwl_output == NULL) {
	ErrorF("create_output ENOMEM");
	return NULL;
    }

    xwl_output->xwl_screen = xwl_screen;
    xwl_output->width = xwl_screen->width = 800;
    xwl_output->height = xwl_screen->height = 600;

    xf86output = xf86OutputCreate(xwl_screen->scrninfo,
				  &output_funcs, "XWAYLAND-1");
    xf86output->driver_private = xwl_output;
    xf86output->mm_width = 300;
    xf86output->mm_height = 240;
    xf86output->subpixel_order = SubPixelHorizontalRGB;
    xf86output->possible_crtcs = 1;
    xf86output->possible_clones = 1;

    xf86crtc = xf86CrtcCreate(xwl_screen->scrninfo, &crtc_funcs);
    xf86crtc->driver_private = xwl_output;

    xwl_output->xf86output = xf86output;
    xwl_output->xf86crtc = xf86crtc;

    xwl_screen->xwl_output = xwl_output;

    return xwl_output;
}

static Bool
resize(ScrnInfoPtr scrn, int width, int height)
{
    if (scrn->virtualX == width && scrn->virtualY == height)
	return TRUE;
    /* We don't handle resize at all, we must match the compositor size */
    return FALSE;
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
xwl_window_attach(struct xwl_window *xwl_window, PixmapPtr pixmap)
{
    struct xwl_screen *xwl_screen = xwl_window->xwl_screen;

    /* We can safely destroy the buffer because we only use one buffer
     * per surface in xwayland model */
    if (xwl_window->buffer)
        wl_buffer_destroy(xwl_window->buffer);

    xwl_screen->driver->create_window_buffer(xwl_window, pixmap);

    if (!xwl_window->buffer) {
        ErrorF("failed to create buffer\n");
	return;
    }

    wl_surface_map_toplevel(xwl_window->surface);
    wl_surface_attach(xwl_window->surface, xwl_window->buffer, 0, 0);

    wl_display_sync_callback(xwl_screen->display, free_pixmap, pixmap);
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
xwl_create_window(WindowPtr window)
{
    ScreenPtr screen = window->drawable.pScreen;
    struct xwl_screen *xwl_screen;
    Selection *selection;
    char buffer[32];
    int len, rc;
    Atom name;
    Bool ret;

    xwl_screen = dixLookupPrivate(&screen->devPrivates,
				     &xwl_screen_private_key);

    screen->CreateWindow = xwl_screen->CreateWindow;
    ret = (*screen->CreateWindow)(window);
    xwl_screen->CreateWindow = screen->CreateWindow;
    screen->CreateWindow = xwl_create_window;

    if (!(xwl_screen->flags & XWL_FLAGS_ROOTLESS) ||
	window->parent == NULL)
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
    struct xwl_window *xwl_window = data;
    struct xwl_screen *xwl_screen = xwl_window->xwl_screen;

    list_add(&xwl_window->link_damage, &xwl_screen->damage_window_list);
}

static void
damage_destroy(DamagePtr pDamage, void *data)
{
}

static Bool
xwl_realize_window(WindowPtr window)
{
    ScreenPtr screen = window->drawable.pScreen;
    struct xwl_screen *xwl_screen;
    struct xwl_window *xwl_window;
    VisualID visual;
    Bool ret;
    int i;

    xwl_screen = dixLookupPrivate(&screen->devPrivates,
				     &xwl_screen_private_key);

    screen->RealizeWindow = xwl_screen->RealizeWindow;
    ret = (*screen->RealizeWindow)(window);
    xwl_screen->RealizeWindow = xwl_screen->RealizeWindow;
    screen->RealizeWindow = xwl_realize_window;

    if (xwl_screen->flags & XWL_FLAGS_ROOTLESS) {
	if (window->redirectDraw != RedirectDrawManual)
	    return ret;
    } else {
	if (window->parent)
	    return ret;
    }

    xwl_window = calloc(sizeof *xwl_window, 1);
    xwl_window->xwl_screen = xwl_screen;
    xwl_window->window = window;
    xwl_window->surface =
	wl_compositor_create_surface(xwl_screen->compositor);
    if (xwl_window->surface == NULL) {
	ErrorF("wl_display_create_surface failed\n");
	return FALSE;
    }

    visual = wVisual(window);
    for (i = 0; i < screen->numVisuals; i++)
	if (screen->visuals[i].vid == visual)
	    break;

    if (screen->visuals[i].nplanes == 32)
	xwl_window->visual = xwl_screen->premultiplied_argb_visual;
    else
	xwl_window->visual = xwl_screen->rgb_visual;

    wl_surface_set_user_data(xwl_window->surface, xwl_window);
    xwl_window_attach(xwl_window, (*screen->GetWindowPixmap)(window));

    dixSetPrivate(&window->devPrivates,
		  &xwl_window_private_key, xwl_window);

    xwl_window->damage =
	DamageCreate(damage_report, damage_destroy, DamageReportNonEmpty,
		     FALSE, screen, xwl_window);
    DamageRegister(&window->drawable, xwl_window->damage);

    list_add(&xwl_window->link, &xwl_screen->window_list);
    list_init(&xwl_window->link_damage);
    return ret;
}

static Bool
xwl_unrealize_window(WindowPtr window)
{
    ScreenPtr screen = window->drawable.pScreen;
    struct xwl_screen *xwl_screen;
    struct xwl_window *xwl_window;
    struct xwl_input_device *xwl_input_device;
    Bool ret;

    xwl_screen = dixLookupPrivate(&screen->devPrivates,
				     &xwl_screen_private_key);

    list_for_each_entry(xwl_input_device,
			&xwl_screen->input_device_list, link) {
	if (!xwl_input_device->focus_window)
	    continue ;
	if (xwl_input_device->focus_window->window == window) {
	    xwl_input_device->focus_window = NULL;
	    SetDeviceRedirectWindow(xwl_input_device->pointer, PointerRootWin);
	}
    }

    screen->UnrealizeWindow = xwl_screen->UnrealizeWindow;
    ret = (*screen->UnrealizeWindow)(window);
    xwl_screen->UnrealizeWindow = screen->UnrealizeWindow;
    screen->UnrealizeWindow = xwl_unrealize_window;

    xwl_window =
	dixLookupPrivate(&window->devPrivates, &xwl_window_private_key);
    if (!xwl_window)
	return ret;

    if (xwl_window->buffer)
	wl_buffer_destroy(xwl_window->buffer);
    wl_surface_destroy(xwl_window->surface);
    list_del(&xwl_window->link);
    list_del(&xwl_window->link_damage);
    DamageUnregister(&window->drawable, xwl_window->damage);
    DamageDestroy(xwl_window->damage);
    free(xwl_window);
    dixSetPrivate(&window->devPrivates, &xwl_window_private_key, NULL);

    return ret;
}

static void
xwl_set_window_pixmap(WindowPtr window, PixmapPtr pixmap)
{
    ScreenPtr screen = window->drawable.pScreen;
    struct xwl_screen *xwl_screen;
    struct xwl_window *xwl_window;

    xwl_screen = dixLookupPrivate(&screen->devPrivates,
				     &xwl_screen_private_key);

    screen->SetWindowPixmap = xwl_screen->SetWindowPixmap;
    (*screen->SetWindowPixmap)(window, pixmap);
    xwl_screen->SetWindowPixmap = screen->SetWindowPixmap;
    screen->SetWindowPixmap = xwl_set_window_pixmap;

    xwl_window =
	dixLookupPrivate(&window->devPrivates, &xwl_window_private_key);
    if (xwl_window)
	xwl_window_attach(xwl_window, pixmap);
}

static void
xwl_move_window(WindowPtr window, int x, int y,
		   WindowPtr sibling, VTKind kind)
{
    ScreenPtr screen = window->drawable.pScreen;
    struct xwl_screen *xwl_screen;
    struct xwl_window *xwl_window;

    xwl_screen = dixLookupPrivate(&screen->devPrivates,
				     &xwl_screen_private_key);

    screen->MoveWindow = xwl_screen->MoveWindow;
    (*screen->MoveWindow)(window, x, y, sibling, kind);
    xwl_screen->MoveWindow = screen->MoveWindow;
    screen->MoveWindow = xwl_move_window;

    xwl_window =
	dixLookupPrivate(&window->devPrivates, &xwl_window_private_key);
    if (xwl_window == NULL)
	return;

    wl_surface_map_toplevel(xwl_window->surface);
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
xwl_realize_cursor(DeviceIntPtr device, ScreenPtr screen, CursorPtr cursor)
{
    struct xwl_screen *xwl_screen;
    int size;
    char filename[] = "/tmp/wayland-shm-XXXXXX";
    int fd;
    struct wl_buffer *buffer;
    struct wl_visual *visual;
    void *data;

    xwl_screen = dixLookupPrivate(&screen->devPrivates,
				     &xwl_screen_private_key);
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

    visual = xwl_screen->argb_visual;
    buffer = wl_shm_create_buffer(xwl_screen->shm, fd,
				  cursor->bits->width, cursor->bits->height,
				  cursor->bits->width * 4, visual);
    close(fd);

    dixSetPrivate(&cursor->devPrivates, &xwl_cursor_private_key, buffer);

    return TRUE;
}

static Bool
xwl_unrealize_cursor(DeviceIntPtr device,
			ScreenPtr screen, CursorPtr cursor)
{
    struct wl_buffer *buffer;

    buffer = dixGetPrivate(&cursor->devPrivates, &xwl_cursor_private_key);
    wl_buffer_destroy(buffer);

    return TRUE;
}

static void
xwl_set_cursor(DeviceIntPtr device,
	       ScreenPtr screen, CursorPtr cursor, int x, int y)
{
    struct xwl_screen *xwl_screen;
    struct xwl_input_device *xwl_input_device;
    struct wl_buffer *buffer;

    if (!cursor)
	return;

    xwl_screen = dixLookupPrivate(&screen->devPrivates,
				  &xwl_screen_private_key);

    if (!xwl_screen || list_is_empty(&xwl_screen->input_device_list))
	return ;

    xwl_input_device = list_first_entry(&xwl_screen->input_device_list,
					struct xwl_input_device, link);

    buffer = dixGetPrivate(&cursor->devPrivates, &xwl_cursor_private_key);

    wl_input_device_attach(xwl_input_device->input_device,
			   xwl_input_device->time, buffer,
			   cursor->bits->xhot, cursor->bits->yhot);
}

static void
xwl_move_cursor(DeviceIntPtr device, ScreenPtr screen, int x, int y)
{
}

static Bool
xwl_device_cursor_initialize(DeviceIntPtr device, ScreenPtr screen)
{
    struct xwl_screen *xwl_screen;

    xwl_screen = dixLookupPrivate(&screen->devPrivates,
				  &xwl_screen_private_key);

    return xwl_screen->sprite_funcs->DeviceCursorInitialize(device,
							       screen);
}

static void
xwl_device_cursor_cleanup(DeviceIntPtr device, ScreenPtr screen)
{
    struct xwl_screen *xwl_screen;

    xwl_screen = dixLookupPrivate(&screen->devPrivates,
				  &xwl_screen_private_key);

    xwl_screen->sprite_funcs->DeviceCursorCleanup(device, screen);
}

static miPointerSpriteFuncRec xwl_pointer_sprite_funcs =
{
    xwl_realize_cursor,
    xwl_unrealize_cursor,
    xwl_set_cursor,
    xwl_move_cursor,
    xwl_device_cursor_initialize,
    xwl_device_cursor_cleanup
};

static CARD32
xwl_input_delayed_init(OsTimerPtr timer, CARD32 time, pointer data)
{
    struct xwl_screen *xwl_screen = data;

    xwl_input_init(xwl_screen);
    TimerFree(timer);

    return 0;
}

int
xwl_screen_init(struct xwl_screen *xwl_screen, ScreenPtr screen)
{
    miPointerScreenPtr pointer_priv;

    if (wayland_screen_init(xwl_screen, xwl_screen->driver->use_drm) != Success)
	return BadAlloc;

    xwl_screen->screen = screen;

    if (!dixRegisterPrivateKey(&xwl_screen_private_key, PRIVATE_SCREEN, 0))
	return BadAlloc;

    if (!dixRegisterPrivateKey(&xwl_window_private_key, PRIVATE_WINDOW, 0))
	return BadAlloc;

    if (!dixRegisterPrivateKey(&xwl_cursor_private_key, PRIVATE_CURSOR, 0))
	return BadAlloc;

    dixSetPrivate(&screen->devPrivates,
		  &xwl_screen_private_key, xwl_screen);

    xwl_screen->CreateWindow = screen->CreateWindow;
    screen->CreateWindow = xwl_create_window;

    xwl_screen->RealizeWindow = screen->RealizeWindow;
    screen->RealizeWindow = xwl_realize_window;

    xwl_screen->UnrealizeWindow = screen->UnrealizeWindow;
    screen->UnrealizeWindow = xwl_unrealize_window;

    xwl_screen->SetWindowPixmap = screen->SetWindowPixmap;
    screen->SetWindowPixmap = xwl_set_window_pixmap;

    xwl_screen->MoveWindow = screen->MoveWindow;
    screen->MoveWindow = xwl_move_window;

    pointer_priv = dixLookupPrivate(&screen->devPrivates, miPointerScreenKey);
    xwl_screen->sprite_funcs = pointer_priv->spriteFuncs;
    pointer_priv->spriteFuncs = &xwl_pointer_sprite_funcs;

    TimerSet(NULL, 0, 1, xwl_input_delayed_init, xwl_screen);
    return Success;
}

struct xwl_screen *
xwl_screen_pre_init(ScrnInfoPtr scrninfo,
		    uint32_t flags, struct xwl_driver *driver)
{
    struct xwl_screen *xwl_screen;

    xwl_screen = calloc(sizeof *xwl_screen, 1);
    if (xwl_screen == NULL) {
	ErrorF("calloc failed\n");
	return NULL;
    }

    list_init(&xwl_screen->input_device_list);
    list_init(&xwl_screen->damage_window_list);
    list_init(&xwl_screen->window_list);
    xwl_screen->scrninfo = scrninfo;
    xwl_screen->driver = driver;
    xwl_screen->flags = flags;

    if (xorgRootless)
	xwl_screen->flags |= XWL_FLAGS_ROOTLESS;

    xf86CrtcConfigInit(scrninfo, &config_funcs);

    xf86CrtcSetSizeRange(scrninfo, 320, 200, 8192, 8192);

    xf86InitialConfiguration(scrninfo, TRUE);

    return xwl_screen;
}

int xwl_screen_get_drm_fd(struct xwl_screen *xwl_screen)
{
    return xwl_screen->drm_fd;
}


#ifdef WITH_LIBDRM
int
xwl_create_window_buffer_drm(struct xwl_window *xwl_window,
				PixmapPtr pixmap, uint32_t name)
{
    xwl_window->buffer =
      wl_drm_create_buffer(xwl_window->xwl_screen->drm,
			   name,
			   pixmap->drawable.width,
			   pixmap->drawable.height,
			   pixmap->devKind,
			   xwl_window->visual);

    return xwl_window->buffer ? Success : BadDrawable;
}
#endif

int
xwl_create_window_buffer_shm(struct xwl_window *xwl_window,
				PixmapPtr pixmap, int fd)
{
    xwl_window->buffer =
      wl_shm_create_buffer(xwl_window->xwl_screen->shm, fd,
			   pixmap->drawable.width, pixmap->drawable.height,
			   pixmap->drawable.width * 4, xwl_window->visual);

    return xwl_window->buffer ? Success : BadDrawable;
}

void xwl_screen_close(struct xwl_screen *xwl_screen)
{
    wayland_screen_close(xwl_screen);
    xwl_screen->input_initialized = 0;
}

void xwl_screen_destroy(struct xwl_screen *xwl_screen)
{
    if (xwl_screen->xwl_output) {
	xf86OutputDestroy(xwl_screen->xwl_output->xf86output);
	xf86CrtcDestroy(xwl_screen->xwl_output->xf86crtc);
    }

    free(xwl_screen->xwl_output);
    free(xwl_screen);
}

int xwl_screen_authenticate(struct xwl_screen *xwl_screen,
			       uint32_t magic)
{
    xwl_screen->authenticated = 0;
#ifdef WITH_LIBDRM
    if (xwl_screen->drm)
        wl_drm_authenticate (xwl_screen->drm, magic);
#endif
    wl_display_iterate (xwl_screen->display, WL_DISPLAY_WRITABLE);
    while (!xwl_screen->authenticated)
	wl_display_iterate (xwl_screen->display, WL_DISPLAY_READABLE);

    return Success;
}

/* DDX driver must call this after submitting the rendering */
void xwl_screen_post_damage(struct xwl_screen *xwl_screen)
{
    struct xwl_window *xwl_window;
    RegionPtr region;
    BoxPtr box;
    int count, i;

    list_for_each_entry(xwl_window, &xwl_screen->damage_window_list,
			link_damage) {

	region = DamageRegion(xwl_window->damage);
	count = RegionNumRects(region);
	for (i = 0; i < count; i++) {
	    box = &RegionRects(region)[i];
	    wl_surface_damage(xwl_window->surface,
			      box->x1, box->y1,
			      box->x2 - box->x1,
			      box->y2 - box->y1);
	}
	DamageEmpty(xwl_window->damage);
    }

    list_init(&xwl_screen->damage_window_list);
}

static pointer
xwl_setup(pointer module, pointer opts, int *errmaj, int *errmin)
{
    return xwl_input_setup(module, opts, errmaj, errmin);
}

static void
xwl_teardown(pointer p)
{
    xwl_input_teardown(p);
}

static XF86ModuleVersionInfo xwl_version_info = {
    "xwayland",
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

_X_EXPORT const XF86ModuleData xwaylandModuleData = {
    &xwl_version_info,
    &xwl_setup,
    &xwl_teardown
};

int
xwl_version(void)
{
    return xwl_version_info.minorversion;
}
