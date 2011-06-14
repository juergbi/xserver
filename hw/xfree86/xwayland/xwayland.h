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

#ifndef _XWAYLAND_H_
#define _XWAYLAND_H_

#define XWL_VERSION 2

struct xwl_window;
struct xwl_screen;

struct xwl_driver {
    int version;
    int use_drm;
    int (*create_window_buffer)(struct xwl_window *xwl_window,
                                PixmapPtr pixmap);
};

#define XWL_FLAGS_ROOTLESS 0x01

extern _X_EXPORT int
xwl_version(void);

extern _X_EXPORT struct xwl_screen *
xwl_screen_pre_init(ScrnInfoPtr scrninfo,
		    uint32_t flags, struct xwl_driver *driver);

extern _X_EXPORT int
xwl_screen_init(struct xwl_screen *xwl_screen, ScreenPtr screen);

extern _X_EXPORT int
xwl_screen_get_drm_fd(struct xwl_screen *xwl_screen);

extern _X_EXPORT void
xwl_screen_close(struct xwl_screen *xwl_screen);

extern _X_EXPORT void
xwl_screen_destroy(struct xwl_screen *xwl_screen);

extern _X_EXPORT void
xwl_screen_post_damage(struct xwl_screen *xwl_screen);

extern _X_EXPORT int
xwl_drm_authenticate(struct xwl_screen *xwl_screen,
		     uint32_t magic);

extern _X_EXPORT int
xwl_create_window_buffer_drm(struct xwl_window *xwl_window,
			     PixmapPtr pixmap, uint32_t name);

extern _X_EXPORT int
xwl_create_window_buffer_shm(struct xwl_window *xwl_window,
			     PixmapPtr pixmap, int fd);

#endif /* _XWAYLAND_H_ */
