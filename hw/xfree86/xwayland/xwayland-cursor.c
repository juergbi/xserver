/*
 * Copyright © 2011 Kristian Høgsberg
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

#include <xorg-server.h>
#include <cursorstr.h>
#include <xf86Crtc.h>
#include <mipointrst.h>

#include "xwayland.h"
#include "xwayland-private.h"
#include "xserver-client-protocol.h"

static DevPrivateKeyRec xwl_cursor_private_key;

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
	    if (bits->source[i] & bit)
		d = fg;
	    else
		d = bg;
	    if (bits->mask[i] & bit)
		d |= 0xff000000;
	    else
		d = 0x00000000;

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
    struct wl_shm_pool *pool;
    struct wl_buffer *buffer;
    void *data;

    xwl_screen = xwl_screen_get(screen);
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

    pool = wl_shm_create_pool(xwl_screen->shm, fd, size);
    close(fd);
    buffer = wl_shm_pool_create_buffer(pool, 0,
				  cursor->bits->width, cursor->bits->height,
				  cursor->bits->width * 4,
				  WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);

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

void
xwl_seat_set_cursor(struct xwl_seat *xwl_seat)
{
    struct wl_buffer *buffer;

    if (!xwl_seat->x_cursor || !xwl_seat->wl_pointer)
        return;

    buffer = dixGetPrivate(&xwl_seat->x_cursor->devPrivates,
                           &xwl_cursor_private_key);

    wl_pointer_set_cursor(xwl_seat->wl_pointer,
			  xwl_seat->pointer_enter_serial,
			  xwl_seat->cursor,
			  xwl_seat->x_cursor->bits->xhot,
			  xwl_seat->x_cursor->bits->yhot);
    wl_surface_attach(xwl_seat->cursor, buffer, 0, 0);
    wl_surface_damage(xwl_seat->cursor, 0, 0,
		      xwl_seat->x_cursor->bits->width,
		      xwl_seat->x_cursor->bits->height);
    wl_surface_commit(xwl_seat->cursor);
}

static void
xwl_set_cursor(DeviceIntPtr device,
	       ScreenPtr screen, CursorPtr cursor, int x, int y)
{
    struct xwl_screen *xwl_screen;
    struct xwl_seat *xwl_seat;

    xwl_screen = xwl_screen_get(screen);

    if (!xwl_screen || xorg_list_is_empty(&xwl_screen->seat_list))
	return;

    xwl_seat = xorg_list_first_entry(&xwl_screen->seat_list,
		                     struct xwl_seat, link);

    xwl_seat->x_cursor = cursor;
    xwl_seat_set_cursor(xwl_seat);
}

static void
xwl_move_cursor(DeviceIntPtr device, ScreenPtr screen, int x, int y)
{
}

static Bool
xwl_device_cursor_initialize(DeviceIntPtr device, ScreenPtr screen)
{
    struct xwl_screen *xwl_screen;

    xwl_screen = xwl_screen_get(screen);

    return xwl_screen->sprite_funcs->DeviceCursorInitialize(device,
							       screen);
}

static void
xwl_device_cursor_cleanup(DeviceIntPtr device, ScreenPtr screen)
{
    struct xwl_screen *xwl_screen;

    xwl_screen = xwl_screen_get(screen);

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

int
xwl_screen_init_cursor(struct xwl_screen *xwl_screen, ScreenPtr screen)
{
    miPointerScreenPtr pointer_priv;

    if (!dixRegisterPrivateKey(&xwl_cursor_private_key, PRIVATE_CURSOR, 0))
	return BadAlloc;

    pointer_priv = dixLookupPrivate(&screen->devPrivates, miPointerScreenKey);
    xwl_screen->sprite_funcs = pointer_priv->spriteFuncs;
    pointer_priv->spriteFuncs = &xwl_pointer_sprite_funcs;

    return Success;
}
