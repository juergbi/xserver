INCLUDES =					\
	$(XORG_INCS)				\
	-I$(srcdir)/../ddc			\
	-I$(srcdir)/../ramdac			\
	-I$(srcdir)/../i2c			\
	-I$(srcdir)/../parser			\
	-I$(srcdir)/../modes

libhosted_la_LTLIBRARIES = libhosted.la
libhosted_la_CFLAGS = \
	-DHAVE_XORG_CONFIG_H \
	@DIX_CFLAGS@ @XORG_CFLAGS@ @LIBDRM_CFLAGS@ \
	-I$(top_srcdir)/hw/xfree86/common \
	-I$(top_srcdir)/hw/xfree86/os-support/bus

libhosted_la_LDFLAGS = -module -avoid-version @LIBDRM_LIBS@ -lwayland-client
libhosted_ladir = $(moduledir)/extensions
libhosted_la_SOURCES =				\
	hosted.c				\
	hosted-private.h			\
	wayland.c				\
	x11.c

sdk_HEADERS = hosted.h
