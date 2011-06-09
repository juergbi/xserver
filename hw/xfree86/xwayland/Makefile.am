INCLUDES =					\
	$(XORG_INCS)				\
	-I$(srcdir)/../ddc			\
	-I$(srcdir)/../ramdac			\
	-I$(srcdir)/../i2c			\
	-I$(srcdir)/../parser			\
	-I$(srcdir)/../modes

libxwayland_la_LTLIBRARIES = libxwayland.la
libxwayland_la_CFLAGS = \
	-DHAVE_XORG_CONFIG_H \
	@DIX_CFLAGS@ @XORG_CFLAGS@ @LIBDRM_CFLAGS@ \
	-I$(top_srcdir)/hw/xfree86/common \
	-I$(top_srcdir)/hw/xfree86/os-support/bus

libxwayland_la_LDFLAGS = -module -avoid-version @LIBDRM_LIBS@ -lwayland-client
libxwayland_ladir = $(moduledir)/extensions
libxwayland_la_SOURCES =			\
	xwayland.c				\
	xwayland-client.c			\
	xwayland-input.c			\
	xwayland-private.h			\
	drm-client-protocol.h			\
	drm-protocol.c

if DRM
libxwayland_la_SOURCES += xwayland-drm.c
endif

sdk_HEADERS = xwayland.h

BUILT_SOURCES =					\
	drm-client-protocol.h			\
	drm-protocol.c

CLEANFILES = $(BUILT_SOURCES)

@wayland_scanner_rules@
