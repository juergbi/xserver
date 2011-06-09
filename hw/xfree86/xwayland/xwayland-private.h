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

#ifndef _XWAYLAND_PRIVATE_H_
#define _XWAYLAND_PRIVATE_H_

struct xwl_window {
    struct xwl_screen		*xwl_screen;
    struct wl_surface		*surface;
    struct wl_visual		*visual;
    struct wl_buffer		*buffer;
    WindowPtr			 window;
    DamagePtr			 damage;
    struct list			 link;
    struct list			 link_damage;
};

struct xwl_output;

struct xwl_screen {
    struct xwl_driver		*driver;
    ScreenPtr			 screen;
    ScrnInfoPtr			 scrninfo;
    int				 drm_fd;
    int				 wayland_fd;
    struct xwl_output		*xwl_output;
    struct wl_display		*display;
    struct wl_compositor	*compositor;
    struct wl_global_listener   *global_listener;
    struct wl_drm		*drm;
    struct wl_shm		*shm;
    struct wl_visual		*argb_visual;
    struct wl_visual		*rgb_visual;
    struct wl_visual		*premultiplied_argb_visual;
    uint32_t			 mask;
    uint32_t			 flags;
    char			*device_name;
    uint32_t			 authenticated;
    uint32_t			 input_initialized;
    struct list			 input_device_list;
    struct list			 damage_window_list;
    struct list			 window_list;

    /* FIXME: Hack. */
    int32_t			 width, height;
    int32_t			 root_x, root_y;

    CreateWindowProcPtr		 CreateWindow;
    RealizeWindowProcPtr	 RealizeWindow;
    UnrealizeWindowProcPtr	 UnrealizeWindow;
    SetWindowPixmapProcPtr	 SetWindowPixmap;
    MoveWindowProcPtr		 MoveWindow;
    miPointerSpriteFuncPtr	 sprite_funcs;
};

struct xwl_output {
    struct wl_output		*output;
    struct xwl_screen		*xwl_screen;
    int32_t			 x, y, width, height;
    xf86Monitor			 xf86monitor;
    xf86OutputPtr		 xf86output;
    xf86CrtcPtr			 xf86crtc;
};


#define MODIFIER_META 0x01

struct xwl_input_device {
    DeviceIntPtr		 pointer;
    DeviceIntPtr		 keyboard;
    struct xwl_screen		*xwl_screen;
    struct wl_input_device	*input_device;
    int				 grab;
    struct xwl_window		*focus_window;
    int32_t			 grab_x, grab_y;
    uint32_t			 id;
    uint32_t			 modifiers;
    uint32_t			 time;
    struct list			 link;
};

struct xwl_output *
xwl_output_create(struct xwl_screen *xwl_screen);
struct xwl_input_device *
xwl_input_device_create(struct xwl_screen *xwl_screen);

void xwl_input_teardown(pointer p);
pointer xwl_input_setup(pointer module, pointer opts, int *errmaj, int *errmin);
void xwl_input_init(struct xwl_screen *screen);

int wayland_screen_init(struct xwl_screen *screen, int use_drm);
int wayland_screen_close(struct xwl_screen *screen);

int wayland_drm_screen_init(struct xwl_screen *screen);

#endif /* _XWAYLAND_PRIVATE_H_ */
