dnl
dnl Embedded PTY terminal widget (mcterm) support.
dnl
AC_DEFUN([mc_MCTERM], [

    AC_ARG_ENABLE([mcterm],
        AS_HELP_STRING([--enable-mcterm],
            [embedded PTY terminal widget @<:@auto@:>@]),
        [enable_mcterm=$enableval],
        [enable_mcterm=auto])

    if test "x$enable_mcterm" != xno; then
        AC_CHECK_HEADERS([pty.h libutil.h util.h])
        have_openpty=no
        AC_CHECK_FUNCS([openpty], [have_openpty=yes],
            [AC_CHECK_LIB([util], [openpty],
                [AC_DEFINE([HAVE_OPENPTY], [1], [Define if openpty() is available])
                 LIBS="$LIBS -lutil"
                 have_openpty=yes])])

        if test "x$have_openpty" = xno; then
            if test "x$enable_mcterm" = xyes; then
                AC_MSG_ERROR([openpty() is required for --enable-mcterm but was not found.
Install the appropriate development package (e.g. libutil-dev, libbsd-dev).])
            fi
            enable_mcterm=no
        else
            enable_mcterm=yes
        fi
    fi

    AC_MSG_CHECKING([for mcterm terminal support])
    AC_MSG_RESULT([$enable_mcterm])

    if test "x$enable_mcterm" = xyes; then
        AC_DEFINE([ENABLE_MCTERM], [1], [Define to enable embedded PTY terminal widget])
    fi

    AM_CONDITIONAL([ENABLE_MCTERM], [test "x$enable_mcterm" = xyes])
    mcterm=$enable_mcterm
])
