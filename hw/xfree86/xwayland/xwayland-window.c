/*
 * Copyright © 2011 Intel Corporation
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
#include <errno.h>
#include <sys/mman.h>
#include <wayland-client.h>
#include <X11/extensions/compositeproto.h>

#include <xorg-server.h>
#include <xf86Crtc.h>
#include <selection.h>
#include <compositeext.h>
#include <exevents.h>

#include "xwayland.h"
#include "xwayland-private.h"
#include "xserver-client-protocol.h"

static DevPrivateKeyRec xwl_window_private_key;

static void
free_pixmap(void *data, struct wl_callback *callback, uint32_t time)
{
    PixmapPtr pixmap = data;
    ScreenPtr screen = pixmap->drawable.pScreen;

    (*screen->DestroyPixmap)(pixmap);
    wl_callback_destroy(callback);
}

static const struct wl_callback_listener free_pixmap_listener = {
	free_pixmap,
};

static void
xwl_window_attach(struct xwl_window *xwl_window, PixmapPtr pixmap)
{
    struct xwl_screen *xwl_screen = xwl_window->xwl_screen;
    struct wl_callback *callback;

    /* We can safely destroy the buffer because we only use one buffer
     * per surface in xwayland model */
    if (xwl_window->buffer)
        wl_buffer_destroy(xwl_window->buffer);

    xwl_screen->driver->create_window_buffer(xwl_window, pixmap);

    if (!xwl_window->buffer) {
        ErrorF("failed to create buffer\n");
	return;
    }

    wl_surface_attach(xwl_window->surface, xwl_window->buffer, 0, 0);
    wl_surface_damage(xwl_window->surface, 0, 0,
		      pixmap->drawable.width,
		      pixmap->drawable.height);

    callback = wl_display_sync(xwl_screen->display);
    wl_callback_add_listener(callback, &free_pixmap_listener, pixmap);
    pixmap->refcnt++;
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

    xwl_screen = xwl_screen_get(screen);

    screen->CreateWindow = xwl_screen->CreateWindow;
    ret = (*screen->CreateWindow)(window);
    xwl_screen->CreateWindow = screen->CreateWindow;
    screen->CreateWindow = xwl_create_window;

    if (!(xwl_screen->flags & XWL_FLAGS_ROOTLESS) ||
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
    Bool ret;

    xwl_screen = xwl_screen_get(screen);

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

    if (xwl_screen->xorg_server)
	xserver_set_window_id(xwl_screen->xorg_server,
			      xwl_window->surface, window->drawable.id);

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

    xwl_screen = xwl_screen_get(screen);

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

    xwl_screen = xwl_screen_get(screen);

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

    xwl_screen = xwl_screen_get(screen);

    screen->MoveWindow = xwl_screen->MoveWindow;
    (*screen->MoveWindow)(window, x, y, sibling, kind);
    xwl_screen->MoveWindow = screen->MoveWindow;
    screen->MoveWindow = xwl_move_window;

    xwl_window =
	dixLookupPrivate(&window->devPrivates, &xwl_window_private_key);
    if (xwl_window == NULL)
	return;
}

int
xwl_screen_init_window(struct xwl_screen *xwl_screen, ScreenPtr screen)
{
    if (!dixRegisterPrivateKey(&xwl_window_private_key, PRIVATE_WINDOW, 0))
	return BadAlloc;

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

    return Success;
}
