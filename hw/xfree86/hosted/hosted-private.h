/*
 * Copyright © 2010 Kristian Høgsberg
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

#ifndef _HOSTED_PRIVATE_H_
#define _HOSTED_PRIVATE_H_


struct hosted_window {
    struct hosted_screen	*hosted_screen;
    struct wl_surface		*surface;
    struct wl_visual		*visual;
    WindowPtr			 window;
    DamagePtr			 damage;
    struct list			 link;
};

struct hosted_screen {
    struct hosted_driver	*driver;
    struct hosted_backend	*backend;
    ScreenPtr			 screen;
    ScrnInfoPtr			 scrninfo;
    int				 drm_fd;
    int				 wayland_fd;
    struct wl_display		*display;
    struct wl_compositor	*compositor;
    struct wl_drm		*drm;
    uint32_t			 mask;
    uint32_t			 flags;
    char			*device_name;
    uint32_t			 authenticated;
    struct list			 input_device_list;
    struct list			 damage_window_list;

    /* FIXME: Hack. */
    int32_t			 width, height;
    int32_t			 root_x, root_y;

    CreateWindowProcPtr		 CreateWindow;
    RealizeWindowProcPtr	 RealizeWindow;
    UnrealizeWindowProcPtr	 UnrealizeWindow;
    SetWindowPixmapProcPtr	 SetWindowPixmap;
    MoveWindowProcPtr		 MoveWindow;
};

struct hosted_output {
    struct wl_output		*output;
    struct hosted_screen	*hosted_screen;
    int32_t			 x, y, width, height;
    xf86Monitor			 monitor;
};

#define MODIFIER_META 0x01

struct hosted_input_device {
    DeviceIntPtr		 pointer;
    DeviceIntPtr		 keyboard;
    struct hosted_screen	*hosted_screen;
    struct wl_input_device	*input_device;
    int				 grab;
    struct hosted_window	*focus_window;
    int32_t			 grab_x, grab_y;
    uint32_t			 modifiers;
    struct list			 link;
};

struct hosted_backend {
    void (*flush)(struct hosted_window *window, BoxPtr box);
};

struct hosted_output *
hosted_output_create(struct hosted_screen *hosted_screen);
struct hosted_input_device *
hosted_input_device_create(struct hosted_screen *hosted_screen);

int wayland_screen_init(struct hosted_screen *screen);
int x11_screen_init(struct hosted_screen *screen);


#endif /* _HOSTED_H_ */
