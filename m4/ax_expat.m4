dnl Check if --with-expat[=PREFIX] is specified and
dnl Expat >= 1.95.0 is installed in the system.
dnl If yes, substitute EXPAT_CFLAGS, EXPAT_LIBS with regard to
dnl the specified PREFIX and set with_expat to PREFIX, or 'yes' if PREFIX
dnl has not been specified. Also HAVE_LIBEXPAT, HAVE_EXPAT_H are defined.
dnl If --with-expat has not been specified, set with_expat to 'no'.
dnl In addition, an Automake conditional EXPAT_INSTALLED is set accordingly.
dnl This is necessary to adapt a whole lot of packages that have expat
dnl bundled as a static library.
AC_DEFUN([AM_WITH_EXPAT],
[ 
  AC_ARG_WITH([expat], 
              AC_HELP_STRING([--with-expat=PREFIX], [Use system Expat library]), 
	      [], 
	      [])
  dnl AM_CONDITIONAL(EXPAT_INSTALLED, test $with_expat != no)

  EXPAT_CFLAGS=
  EXPAT_LIBS=

  if test "$with_expat" != ""; then
    EXPAT_CFLAGS="-I$with_expat/include"
    EXPAT_LIBS="-L$with_expat/lib"
  fi
  
  AC_CHECK_LIB(expat, 
               XML_ParserCreate,
              [ EXPAT_LIBS="$EXPAT_LIBS -lexpat" expat_found=yes ],
              [ expat_found=no ],
	      "$EXPAT_LIBS")

  if test $expat_found = no; then
    AC_MSG_ERROR([Could not find the Expat library])
  fi

  expat_save_CFLAGS="$CFLAGS"
  expat_save_CXXFLAGS="$CFLAGS"
  CFLAGS="$CFLAGS $EXPAT_CFLAGS"
  CXXFLAGS="$CFLAGS $EXPAT_CFLAGS"
  AC_CHECK_HEADER([expat.h], [expat_found=yes], [expat_found=no])

  if test $expat_found = no; then
    AC_MSG_ERROR([Could not find expat.h])
    CFLAGS=expat_save_CFLAGS
    CXXFLAGSexpat_save_CXXFLAGS
  fi

  AC_SUBST(EXPAT_CFLAGS)
  AC_SUBST(EXPAT_LIBS)
])
