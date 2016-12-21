IGT_LIB_PATH := $(builddir)
GPU_TOOLS_PATH := $(top_srcdir)


include Makefile.sources

libintelhwcomposer_common_la_SOURCES = $(common_SOURCES) $(public_SOURCES)

noinst_LTLIBRARIES = libintelhwcomposer_common.la

AM_CPP_INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/public -I./core -I./utils -I./compositor -I./display  -I../os/linux
AM_CPPFLAGS = -std=c++11
AM_CPPFLAGS += $(AM_CPP_INCLUDES) $(CWARNFLAGS) $(DRM_CFLAGS) $(DEBUG_CFLAGS) -pthread
AM_CFLAGS = $(CWARNFLAGS) $(DRM_CFLAGS) $(DEBUG_CFLAGS) \
	    -pthread

AM_CFLAGS += $(CAIRO_CFLAGS)

libcommon_la_LIBADD = \
	$(DRM_LIBS) \
	$(LIBUDEV_LIBS) \
	-lm
