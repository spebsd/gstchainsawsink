SUBDIRS = chainsaw

# Note: plugindir is set in configure

##############################################################################
# TODO: change libgstchainsawsink.la to something else, e.g. libmysomething.la     #
##############################################################################
plugin_LTLIBRARIES = libgstchainsawsink.la

##############################################################################
# TODO: for the next set of variables, name the prefix if you named the .la, #
#  e.g. libmysomething.la => libmysomething_la_SOURCES                       #
#                            libmysomething_la_CFLAGS                        #
#                            libmysomething_la_LIBADD                        #
#                            libmysomething_la_LDFLAGS                       #
##############################################################################

libgstchainsawsink_la_CXXFLAGS = \
	$(GST_CFLAGS)

# sources used to compile this plug-in
libgstchainsawsink_la_SOURCES = \
	gstchainsawsink.cc

# compiler and linker flags used to compile this plugin, set in configure.ac
libgstchainsawsink_la_CFLAGS = $(GST_CFLAGS)
libgstchainsawsink_la_LIBADD = \
	$(GST_LIBS) \
	$(top_srcdir)/src/chainsaw/libchainsawsink.la
libgstchainsawsink_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstchainsawsink_la_LIBTOOLFLAGS = --tag=disable-static

# headers we need but don't want installed
noinst_HEADERS = gstplugin.h
