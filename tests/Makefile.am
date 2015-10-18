check_PROGRAMS = \
	check-ism

#
# sort by strength
TESTS =  \
	check_ism.sh

INCLUDES = \
	-I$(top_srcdir)/src \
	$(GST_CFLAGS)

#
#
check_ism_SOURCES = \
	check_ism.cpp
check_ism_LDADD = \
	$(top_builddir)/src/chainsaw/libchainsawsink.la
