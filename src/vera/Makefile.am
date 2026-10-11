AM_CPPFLAGS = \
	@VICE_CPPFLAGS@ \
	@ARCH_INCLUDES@ \
	-I$(top_builddir)/src \
	-I$(top_srcdir)/src \
	-I$(top_srcdir)/src/raster \
	-I$(top_srcdir)/src/video

AM_CFLAGS = @VICE_CFLAGS@

AM_CXXFLAGS = @VICE_CXXFLAGS@

AM_LDFLAGS = @VICE_LDFLAGS@


noinst_LIBRARIES = libvera.a libverastubs.a

libverastubs_a_SOURCES = \
	vera-stubs.c

libvera_a_SOURCES = \
	vera-audio.c \
	vera-audio.h \
	vera-cmdline-options.c \
	vera-cmdline-options.h \
	vera-color.c \
	vera-color.h \
	vera-core.c \
	vera-core.h \
	vera-mem.c \
	vera-mem.h \
	vera-resources.c \
	vera-resources.h \
	vera-snapshot.c \
	vera-snapshot.h \
	vera.c \
	vera.h \
	veratypes.h

EXTRA_DIST = \
	LICENSE.x16-emulator
