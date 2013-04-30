# Process this file with autoconf to produce a configure script.
AC_INIT(openglad, 0.98.1)
AM_INIT_AUTOMAKE(openglad, 0.98.1)
AC_CONFIG_SRCDIR(src/glad.cpp)
AM_CONFIG_HEADER(src/config.h)

test "x$prefix" = "xNONE" && prefix=/usr/local
test "x$exec_prefix" = "xNONE" && exec_prefix=$prefix
test "x$sysconfdir" = 'x${prefix}/etc' && sysconfdir=${prefix}/etc
test "x$datadir" = 'x${prefix}/share' && datadir=${prefix}/share/games/openglad
test "x$bindir" = 'x${exec_prefix}/bin' && bindir=${exec_prefix}/games

AC_DEFINE_UNQUOTED(SYSCONFDIR, "$sysconfdir/",  [Config file directory])
AC_DEFINE_UNQUOTED(OG_DATADIR, "$datadir/", [Data file directory])

# Checks for programs.
AC_PROG_CXX
AC_PROG_CC
AC_PROG_MAKE_SET

# Checks for libraries.
dnl Check for SDL
SDL_VERSION=1.2.0
AM_PATH_SDL($SDL_VERSION,
            :,
            AC_MSG_ERROR([*** SDL version $SDL_VERSION not found!])
)
CXXFLAGS="$CXXFLAGS $SDL_CFLAGS"
LIBS="$LIBS $SDL_LIBS"
AC_CHECK_LIB(SDL_mixer, Mix_OpenAudio, LIBS="$LIBS -lSDL_mixer")

# Checks for header files.
AC_HEADER_STDC
AC_CHECK_HEADERS([malloc.h stdlib.h string.h])

# Checks for typedefs, structures, and compiler characteristics.
dnl AC_C_CONST
dnl AC_C_INLINE
AC_STRUCT_TM

# Checks for library functions.
AC_FUNC_MALLOC
AC_CHECK_FUNCS([memset pow])

AC_CONFIG_FILES([Makefile src/Makefile util/Makefile sound/Makefile])
AC_OUTPUT
