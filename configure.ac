# Process this file with autoconf to produce a configure script.
AC_INIT(openglad, 0.9)
AM_INIT_AUTOMAKE(openglad, 0.9)
AC_CONFIG_SRCDIR(src/glad.cpp)
AM_CONFIG_HEADER(config.h)

# Checks for programs.
AC_PROG_CXX
#AC_PROG_CC
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
AC_C_CONST
AC_C_INLINE
AC_STRUCT_TM

# Checks for library functions.
AC_FUNC_MALLOC
AC_CHECK_FUNCS([memset pow])

AC_CONFIG_FILES([Makefile
                 src/Makefile])
AC_OUTPUT
