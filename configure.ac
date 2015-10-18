dnl TODO: fill in your package name and package version here
AC_INIT([gstchainsawsink],[1.0])

dnl required versions of gstreamer and plugins-base
GST_REQUIRED=1.0.0
GSTPB_REQUIRED=1.0.0

AC_CONFIG_SRCDIR([src/gstplugin.c])
AC_CONFIG_HEADERS([config.h])

dnl required version of automake
AM_INIT_AUTOMAKE([1.10])

dnl enable mainainer mode by default
AM_MAINTAINER_MODE([enable])

dnl check for tools (compiler etc.)
AC_PROG_CC
AC_PROG_CXX

dnl required version of libtool
LT_PREREQ([2.2.6])
LT_INIT

CFLAGS="-g -O0"
CXXFLAGS="-std=c++11 -g -O0"

dnl give error and exit if we don't have pkgconfig
AC_CHECK_PROG(HAVE_PKGCONFIG, pkg-config, [ ], [
  AC_MSG_ERROR([You need to have pkg-config installed!])
])

dnl Check for the required version of GStreamer core (and gst-plugins-base)
dnl This will export GST_CFLAGS and GST_LIBS variables for use in Makefile.am
dnl
dnl If you need libraries from gst-plugins-base here, also add:
dnl for libgstaudio-1.0: gstreamer-audio-1.0 >= $GST_REQUIRED
dnl for libgstvideo-1.0: gstreamer-video-1.0 >= $GST_REQUIRED
dnl for libgsttag-1.0: gstreamer-tag-1.0 >= $GST_REQUIRED
dnl for libgstpbutils-1.0: gstreamer-pbutils-1.0 >= $GST_REQUIRED
dnl for libgstfft-1.0: gstreamer-fft-1.0 >= $GST_REQUIRED
dnl for libgstinterfaces-1.0: gstreamer-interfaces-1.0 >= $GST_REQUIRED
dnl for libgstrtp-1.0: gstreamer-rtp-1.0 >= $GST_REQUIRED
dnl for libgstrtsp-1.0: gstreamer-rtsp-1.0 >= $GST_REQUIRED
dnl etc.
PKG_CHECK_MODULES(GST, [
  gstreamer-1.0 >= $GST_REQUIRED
  gstreamer-base-1.0 >= $GST_REQUIRED
  gstreamer-controller-1.0 >= $GST_REQUIRED
], [
  AC_SUBST(GST_CFLAGS)
  AC_SUBST(GST_LIBS)
], [
  AC_MSG_ERROR([
      You need to install or upgrade the GStreamer development
      packages on your system. On debian-based systems these are
      libgstreamer1.0-dev and libgstreamer-plugins-base1.0-dev.
      on RPM-based systems gstreamer1.0-devel, libgstreamer1.0-devel
      or similar. The minimum version required is $GST_REQUIRED.
  ])
])

PKG_CHECK_MODULES(LIBAVCODEC, [libavcodec])
PKG_CHECK_MODULES(LIBAVFORMAT, [libavformat])
PKG_CHECK_MODULES(LIBSWSCALE, [libswscale])
PKG_CHECK_MODULES(LIBMEMCACHED, [libmemcached])
PKG_CHECK_MODULES(LIBCURL, [libcurl])
CXXFLAGS="$CXXFLAGS $LIBAVCODEC_CFLAGS $LIBAVFORMAT_CFLAGS $LIBSWSCALE_CFLAGS $LIBMEMCACHED_CFLAGS $LIBCURL_CFLAGS"
LIBS="$LIBS $LIBAVCODEC_LIBS $LIBAVFORMAT_LIBS $LIBSWSCALE_LIBS $GST_LIBS $LIBMEMCACHED_LIBS $LIBCURL_LIBS"

dnl check if compiler understands -Wall (if yes, add -Wall to GST_CFLAGS)
AC_MSG_CHECKING([to see if compiler understands -Wall])
save_CFLAGS="$CFLAGS"
CFLAGS="$CFLAGS -Wall"
AC_COMPILE_IFELSE([AC_LANG_PROGRAM([ ], [ ])], [
  GST_CFLAGS="$GST_CFLAGS -Wall"
  AC_MSG_RESULT([yes])
], [
  AC_MSG_RESULT([no])
])

dnl set the plugindir where plugins should be installed (for src/Makefile.am)
if test "x${prefix}" = "x$HOME"; then
  plugindir="$HOME/.gstreamer-1.0/plugins"
else
  plugindir="\$(libdir)/gstreamer-1.0"
fi
AC_SUBST(plugindir)

dnl set proper LDFLAGS for plugins
GST_PLUGIN_LDFLAGS='-module -avoid-version -export-symbols-regex [_]*\(gst_\|Gst\|GST_\).*'
AC_SUBST(GST_PLUGIN_LDFLAGS)

#
# for mp4split
# CFLAGS="$CFLAGS -I/usr/local/include"
# LDFLAGS="$LDFLAGS -L/usr/local/lib"
# with_expat=yes
#AM_WITH_EXPAT

#
# libism
#PKG_CHECK_MODULES(LIBISM, [libism])
#CFLAGS="$CFLAGS $LIBISM_CFLAGS"
#CXXFLAGS="$CXXFLAGS $LIBISM_CFLAGS $LIBISM_CXXFLAGS"
#LIBS="$LIBS $LIBISM_LIBS"

AC_CONFIG_FILES([
  Makefile 
  src/Makefile 
  src/chainsaw/Makefile
])
AC_OUTPUT

