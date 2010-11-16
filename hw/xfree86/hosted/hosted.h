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

#ifndef _HOSTED_H_
#define _HOSTED_H_

#define HOSTED_VERSION 1

struct hosted_driver {
    int version;
    int (*name_pixmap)(PixmapPtr pixmap, uint32_t *name);
};

#define HOSTED_FLAGS_ROOTLESS 0x01

extern _X_EXPORT int
hosted_version(void);

extern _X_EXPORT struct hosted_screen *
hosted_screen_pre_init(ScrnInfoPtr scrninfo,
		       uint32_t flags, struct hosted_driver *driver);

extern _X_EXPORT int
hosted_screen_init(struct hosted_screen *hosted_screen, ScreenPtr screen);

extern _X_EXPORT int
hosted_screen_get_drm_fd(struct hosted_screen *hosted_screen);

extern _X_EXPORT void
hosted_screen_destroy(struct hosted_screen *hosted_screen);

extern _X_EXPORT void
hosted_screen_post_damage(struct hosted_screen *hosted_screen);

extern _X_EXPORT int
hosted_screen_authenticate(struct hosted_screen *hosted_screen,
			   uint32_t magic);

#endif /* _HOSTED_H_ */
