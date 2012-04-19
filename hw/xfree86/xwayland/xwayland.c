/*
 * Copyright © 2008-2011 Kristian Høgsberg
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
#include <wayland-util.h>
#include <wayland-client.h>

#include <xorg-server.h>
#include <extinit.h>
#include <xf86Xinput.h>
#include <xf86Crtc.h>
#include <xf86Priv.h>
#include <os.h>
#include <selection.h>

#include "xwayland.h"
#include "xwayland-private.h"
#include "xserver-client-protocol.h"

/*
 * TODO:
 *  - lose X kb focus when wayland surface loses it
 *  - active grabs, grab owner crack
 */

static DevPrivateKeyRec xwl_screen_private_key;
static Atom xdnd_atom;

static void
xserver_client(void *data, struct xserver *xserver, int fd)
{
    AddClientOnOpenFD(fd);
}

static void
xserver_listen_socket(void *data, struct xserver *xserver, int fd)
{
    ListenOnOpenFD(fd, TRUE);
}

static const struct xserver_listener xserver_listener = {
    xserver_client,
    xserver_listen_socket
};

static void
xwl_input_delayed_init(void *data, struct wl_callback *callback, uint32_t time)
{
    struct xwl_screen *xwl_screen = data;
    uint32_t id;

    ErrorF("xwl_input_delayed_init\n");

    wl_callback_destroy(callback);
    xwl_input_init(xwl_screen);

    id = wl_display_get_global(xwl_screen->display, "xserver", 1);
    if (id != 0) {
        xwl_screen->xorg_server = wl_display_bind(xwl_screen->display,
						  id, &xserver_interface);
	xserver_add_listener(xwl_screen->xorg_server,
			     &xserver_listener, xwl_screen);
    }
}

static const struct wl_callback_listener delayed_init_listner = {
	xwl_input_delayed_init
};

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
		wl_display_bind(xwl_screen->display,
				id, &wl_compositor_interface);
    } else if (strcmp (interface, "wl_shm") == 0) {
        xwl_screen->shm = wl_display_bind(xwl_screen->display,
					  id, &wl_shm_interface);
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

int
xwl_screen_init(struct xwl_screen *xwl_screen, ScreenPtr screen)
{
    struct wl_callback *callback;

    xwl_screen->screen = screen;

    if (!dixRegisterPrivateKey(&xwl_screen_private_key, PRIVATE_SCREEN, 0))
	return BadAlloc;

    dixSetPrivate(&screen->devPrivates,
		  &xwl_screen_private_key, xwl_screen);

    xwl_screen_init_window(xwl_screen, screen);

    xwl_screen_init_cursor(xwl_screen, screen);

    AddGeneralSocket(xwl_screen->wayland_fd);
    RegisterBlockAndWakeupHandlers(block_handler, wakeup_handler, xwl_screen);

    callback = wl_display_sync(xwl_screen->display);
    wl_callback_add_listener(callback, &delayed_init_listner, xwl_screen);

    return Success;
}

struct xwl_screen *
xwl_screen_get(ScreenPtr screen)
{
    return dixLookupPrivate(&screen->devPrivates, &xwl_screen_private_key);
}

static void
xwayland_selection_callback(CallbackListPtr *callbacks,
			    pointer data, pointer args)
{
    SelectionInfoRec *info = (SelectionInfoRec *) args;
    struct xwl_screen *xwl_screen = data;
    Selection *selection = info->selection;

    switch (info->kind) {
    case SelectionSetOwner:
	ErrorF("set selection: atom %s (%d), window %d\n",
	       NameForAtom(selection->selection),
	       selection->selection, selection->window);

	if (selection->selection == xdnd_atom) {
	    if (selection->window != None)
		ErrorF("client %p starts dnd\n", info->client);
	    else
		ErrorF("client %p stops dnd\n", info->client);
	}
	break;
    case SelectionWindowDestroy:
	ErrorF("selection window destroy\n");
	break;
    case SelectionClientClose:
	ErrorF("selection client close\n");
	break;
    }
}


struct xwl_screen *
xwl_screen_pre_init(ScrnInfoPtr scrninfo,
		    uint32_t flags, struct xwl_driver *driver)
{
    struct xwl_screen *xwl_screen;
    int ret;

    xwl_screen = calloc(sizeof *xwl_screen, 1);
    if (xwl_screen == NULL) {
	ErrorF("calloc failed\n");
	return NULL;
    }

    xdnd_atom = MakeAtom("XdndSelection", 13, 1);
    ErrorF("xdnd_atom: %d\n", xdnd_atom);
    if (!AddCallback(&SelectionCallback,
		     xwayland_selection_callback, xwl_screen)) {
	free(xwl_screen);
	return NULL;
    }

    xorg_list_init(&xwl_screen->input_device_list);
    xorg_list_init(&xwl_screen->damage_window_list);
    xorg_list_init(&xwl_screen->window_list);
    xwl_screen->scrninfo = scrninfo;
    xwl_screen->driver = driver;
    xwl_screen->flags = flags;

    if (xorgRootless)
	xwl_screen->flags |= XWL_FLAGS_ROOTLESS;

    xwl_screen->display = wl_display_connect(NULL);
    if (xwl_screen->display == NULL) {
	ErrorF("wl_display_create failed\n");
	return NULL;
    }

    /* Set up listener so we'll catch all events. */
    xwl_screen->global_listener =
	    wl_display_add_global_listener(xwl_screen->display,
					   global_handler, xwl_screen);

    /* Process connection events. */
    wl_display_iterate(xwl_screen->display, WL_DISPLAY_READABLE);

    xwl_screen->wayland_fd =
	wl_display_get_fd(xwl_screen->display, source_update, xwl_screen);

#ifdef WITH_LIBDRM
    if (xwl_screen->driver->use_drm)
	ret = xwl_drm_pre_init(xwl_screen);
    if (xwl_screen->driver->use_drm && ret != Success)
	return NULL;
#endif

    xwayland_screen_preinit_output(xwl_screen, scrninfo);

    return xwl_screen;
}

int
xwl_create_window_buffer_shm(struct xwl_window *xwl_window,
			     PixmapPtr pixmap, int fd)
{
    VisualID visual;
    uint32_t format;
    WindowPtr window = xwl_window->window;
    ScreenPtr screen = window->drawable.pScreen;
    struct wl_shm_pool *pool;
    int i, size;

    visual = wVisual(window);
    for (i = 0; i < screen->numVisuals; i++)
	if (screen->visuals[i].vid == visual)
	    break;

    if (screen->visuals[i].nplanes == 32)
	format = WL_SHM_FORMAT_ARGB8888;
    else
	format = WL_SHM_FORMAT_XRGB8888;

    size = pixmap->drawable.width * pixmap->drawable.height * 4;
    pool = wl_shm_create_pool(xwl_window->xwl_screen->shm, fd, size);
    xwl_window->buffer =  wl_shm_pool_create_buffer(pool, 0,
			   pixmap->drawable.width, pixmap->drawable.height,
			   pixmap->drawable.width * 4, format);
    wl_shm_pool_destroy(pool);

    return xwl_window->buffer ? Success : BadDrawable;
}

void xwl_screen_close(struct xwl_screen *xwl_screen)
{
    struct xwl_input_device *xwl_input_device, *itmp;
    struct xwl_window *xwl_window, *wtmp;

    if (xwl_screen->input_listener)
	wl_display_remove_global_listener(xwl_screen->display,
					  xwl_screen->input_listener);


    xorg_list_for_each_entry_safe(xwl_input_device, itmp,
				  &xwl_screen->input_device_list, link) {
	wl_input_device_destroy(xwl_input_device->input_device);
	free(xwl_input_device);
    }
    xorg_list_for_each_entry_safe(xwl_window, wtmp,
				  &xwl_screen->window_list, link) {
	wl_buffer_destroy(xwl_window->buffer);
	wl_surface_destroy(xwl_window->surface);
	free(xwl_window);
    }

    xwl_screen->input_listener = NULL;
    xorg_list_init(&xwl_screen->input_device_list);
    xorg_list_init(&xwl_screen->damage_window_list);
    xorg_list_init(&xwl_screen->window_list);
    xwl_screen->root_x = 0;
    xwl_screen->root_y = 0;

    wl_display_roundtrip(xwl_screen->display);
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

/* DDX driver must call this after submitting the rendering */
void xwl_screen_post_damage(struct xwl_screen *xwl_screen)
{
    struct xwl_window *xwl_window;
    RegionPtr region;
    BoxPtr box;
    int count, i;

    xorg_list_for_each_entry(xwl_window, &xwl_screen->damage_window_list,
			     link_damage) {

	region = DamageRegion(xwl_window->damage);
	count = RegionNumRects(region);
	for (i = 0; i < count; i++) {
	    box = &RegionRects(region)[i];
	    wl_surface_damage(xwl_window->surface,
			      box->x1, box->y1,
			      box->x2 - box->x1 + 1,
			      box->y2 - box->y1 + 1);
	}
	DamageEmpty(xwl_window->damage);
    }

    xorg_list_init(&xwl_screen->damage_window_list);
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
