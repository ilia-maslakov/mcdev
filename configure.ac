dnl
dnl Configure.in file for the Midnight Commander
dnl

dnl 2.64 is required at least for m4_esyscmd_s()
AC_PREREQ([2.64])

dnl The name is what PACKAGE_NAME becomes, and that lands in the middle of
dnl sentences such as "Type 'exit' to return to the %s", so it stays a short
dnl noun phrase. The full title of the fork belongs where it reads as a title.
dnl The fourth argument is PACKAGE and must remain mc: the gettext domain and
dnl the data paths are built from it.
AC_INIT([Midnight Commander], m4_esyscmd_s([./version.sh .]),
        [https://github.com/ilia-maslakov/mcdev/issues], [mc],
        [https://github.com/ilia-maslakov/mcdev/wiki])

m4_pattern_forbid(MC_)
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_AUX_DIR(config)
AC_CONFIG_SRCDIR(src/main.c)
AC_CONFIG_HEADERS(config.h)

dnl Apply "no-define" to avoid defining the VERSION macro in config.h
AM_INIT_AUTOMAKE([no-define subdir-objects])
dnl PACKAGE macro isn't defined if "no-define" is applied
AC_DEFINE([PACKAGE], ["mc"], [Name of package])

dnl Enable silent rules by default (if yes)
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

mc_VERSION

AM_MAINTAINER_MODE

AC_CANONICAL_HOST

AC_USE_SYSTEM_EXTENSIONS


dnl ############################################################################
dnl Check for compiler
dnl ############################################################################

dnl This should be checked before toolchain macros, otherwise they will remember
dnl that ar cannot be found and linking via libtool will fail at a later stage
AC_CHECK_TOOLS([AR], [ar gar])

AC_PROG_CC

# AC_PROG_CC doesn't try enabling C99 in autoconf 2.69 and below, but
# AC_PROG_CC_C99 is deprecated in newer ones. In autoconf 2.70+ both
# will try enabling features up to C11.
m4_version_prereq([2.70], [AC_PROG_CC], [AC_PROG_CC_C99])

mc_CHECK_CFLAGS

CFLAGS_OPTS=""
if test "x$CFLAGS" = "x"; then
    CFLAGS_OPTS=" -O2 "
fi
if test x$USE_MAINTAINER_MODE = xyes; then
    CFLAGS_OPTS="-g3 -Og -ggdb"
    AC_DEFINE(USE_MAINTAINER_MODE, 1, [Use maintainer mode])
fi

AC_ARG_ENABLE([werror],
    AS_HELP_STRING([--enable-werror], [Handle all compiler warnings as errors]))
if test "x$enable_werror" = xyes; then
    AX_APPEND_COMPILE_FLAGS([-Werror], [mc_configured_cflags])
fi

LT_INIT


dnl ############################################################################
dnl Check for programs
dnl ############################################################################

AC_PROG_SED
PKG_PROG_PKG_CONFIG
AC_PROG_INSTALL
AC_PROG_LN_S
dnl See also the "OS specific stuff" section below.

dnl Check nroff and the options it supports
AC_CHECK_PROG(HAVE_nroff, nroff, true, false)
dnl Default values
MANDOC=-man
MAN_FLAGS=
if $HAVE_nroff; then
    AC_MSG_CHECKING([for manual formatting macros])
    AC_CACHE_VAL(mc_cv_mandoc, [
    nroff -mandoc < /dev/null > /dev/null 2>&1
    if test $? = 0; then
        mc_cv_mandoc=-mandoc
    else
        mc_cv_mandoc=-man
    fi
    ])
    MANDOC=$mc_cv_mandoc
    AC_MSG_RESULT([$MANDOC])

    AC_MSG_CHECKING([for option to disable ANSI color in manuals])
    AC_CACHE_VAL(mc_cv_man_nocolor, [
    nroff -c < /dev/null > /dev/null 2>&1
    if test $? = 0; then
        mc_cv_man_nocolor=-c
    else
        mc_cv_man_nocolor=
    fi
    ])
    MAN_FLAGS=$mc_cv_man_nocolor
    AC_MSG_RESULT([${MAN_NOCOLOR-none}])

    AC_MSG_CHECKING([if nroff accepts -Tlatin1 or -Tascii])
    AC_CACHE_VAL(mc_cv_nroff_tascii, [
    mc_cv_nroff_tascii=
    nroff -Tlatin1 < /dev/null > /dev/null 2>&1 /dev/null
    if test $? = 0; then
        mc_cv_nroff_tascii=-Tlatin1
    else
        nroff -Tascii < /dev/null > /dev/null 2>&1 /dev/null
        if test $? = 0; then
            mc_cv_nroff_tascii=-Tascii
        fi
    fi
    ])
    AC_MSG_RESULT([${mc_cv_nroff_tascii-no}])
    MAN_FLAGS="$MAN_FLAGS $mc_cv_nroff_tascii"
fi

AC_SUBST(MANDOC)
AC_SUBST(MAN_FLAGS)

dnl Check for -z, -b, -L, and -S options to file
AC_CHECK_PROG(HAVE_FILECMD, file, true, false)
if $HAVE_FILECMD; then
    dnl Don't use the file command if it doesn't accept the -z option
    AC_MSG_CHECKING([for -z option to file command])
    AC_CACHE_VAL(mc_cv_file_z, [
        file -z . > /dev/null 2>&1
        if test $? = 0; then
            mc_cv_file_z=yes
        else
            mc_cv_file_z=no
        fi
    ])
    AC_MSG_RESULT([$mc_cv_file_z])

    if test x$mc_cv_file_z = xyes; then
        AC_DEFINE(USE_FILE_CMD, 1, [Define if the file command accepts the -z option])
    else
        AC_MSG_WARN([The file command doesn't accept the -z option and will not be used])
    fi

    if test x$mc_cv_file_z = xyes; then
        dnl file is used; check -b, -L and -S options

        AC_MSG_CHECKING([for -b option to file command])
        AC_CACHE_VAL(mc_cv_file_b, [
            file -b . > /dev/null 2>&1
            if test $? = 0; then
                mc_cv_file_b=yes
            else
                mc_cv_file_b=no
            fi
        ])
        AC_MSG_RESULT([$mc_cv_file_b])

        if test x$mc_cv_file_b = xyes; then
            AC_DEFINE(FILE_B, "-b ", [Define if the file command accepts the -b option])
        fi

        AC_MSG_CHECKING([for -L option to file command])
        AC_CACHE_VAL(mc_cv_file_L, [
            file -L . > /dev/null 2>&1
            if test $? = 0; then
                mc_cv_file_L=yes
            else
                mc_cv_file_L=no
            fi
        ])
        AC_MSG_RESULT([$mc_cv_file_L])

        if test x$mc_cv_file_L = xyes; then
            AC_DEFINE(FILE_L, "-L ", [Define if the file command accepts the -L option])
        else
            AC_DEFINE(FILE_L, "", [Define if the file command accepts the -L option])
        fi

        dnl The file command accepts the -S option since 5.33
        AC_MSG_CHECKING([for -S option to file command])
        AC_CACHE_VAL(mc_cv_file_S, [
            file -S . > /dev/null 2>&1
            if test $? = 0; then
                mc_cv_file_S=yes
            else
                mc_cv_file_S=no
            fi
        ])
        AC_MSG_RESULT([$mc_cv_file_S])

        if test x$mc_cv_file_S = xyes; then
            AC_DEFINE(FILE_S, "-S ", [Define if file command accepts the -S option])
        else
            AC_DEFINE(FILE_S, "", [Define if file command accepts the -S option])
        fi
    fi
fi


dnl ############################################################################
dnl Check for other tools
dnl ############################################################################

mc_UNIT_TESTS


dnl ############################################################################
dnl Check for main libraries
dnl ############################################################################

mc_CHECK_GLIB
mc_G_MODULE_SUPPORTED
mc_WITH_SCREEN
dnl X11 support. Used to read keyboard modifiers when running under X11.
mc_WITH_X


dnl ############################################################################
dnl Check for header files
dnl ############################################################################

AC_CHECK_HEADERS([string.h memory.h limits.h malloc.h \
    utime.h sys/statfs.h sys/vfs.h \
    sys/select.h sys/ioctl.h stropts.h arpa/inet.h \
    sys/socket.h])
dnl This macro is redefined in m4.include/gnulib/sys_types_h.m4
dnl   to work around a buggy version in autoconf <= 2.69.
AC_HEADER_MAJOR

mc_CHECK_HEADER_STDCKDINT


dnl ############################################################################
dnl Check for types
dnl ############################################################################

dnl Check largefile before type sizeof checks
AC_SYS_LARGEFILE

AC_CHECK_SIZEOF(long)
AC_TYPE_UINTMAX_T
AC_CHECK_SIZEOF(uintmax_t)
AC_TYPE_OFF_T
AC_CHECK_SIZEOF(off_t)
AC_TYPE_MODE_T
gl_PROMOTED_TYPE_MODE_T
AC_TYPE_PID_T
AC_TYPE_UID_T

dnl Taken from GNU/Linux, and should be good enough on platforms lacking these types.
AC_CHECK_TYPE([dev_t], [unsigned long long int])
AC_CHECK_TYPE([ino_t], [unsigned long long int])

dnl Taken from GNU/Linux, and should be good enough on platforms lacking these types.
AC_CHECK_TYPE([major_t], [unsigned int])
AC_CHECK_TYPE([minor_t], [unsigned int])

AC_STRUCT_ST_BLOCKS
AC_CHECK_MEMBERS([struct stat.st_blksize, struct stat.st_rdev, struct stat.st_mtim, struct stat.st_mtimespec, struct stat.st_mtimensec])
gl_STAT_SIZE

AC_STRUCT_DIRENT_D_TYPE

AH_TEMPLATE([sig_atomic_t],
            [/* Define to `int' if <signal.h> doesn't define.])
AH_TEMPLATE([SIG_ATOMIC_VOLATILE_T],
            [Some systems declare sig_atomic_t as volatile, some others -- no.
             This define will have value `sig_atomic_t' or
             `volatile sig_atomic_t' accordingly.])

AC_MSG_CHECKING(for sig_atomic_t in signal.h)
AC_EGREP_HEADER(sig_atomic_t,signal.h,
  [
    ac_cv_type_sig_atomic_t=yes;
    AC_EGREP_HEADER(volatile.*sig_atomic_t,
                    signal.h,
                    [
                        is_sig_atomic_t_volatile=yes;
                        AC_MSG_RESULT([yes, volatile])
                    ],
                    [
                        is_sig_atomic_t_volatile=no;
                        AC_MSG_RESULT([yes, non volatile])
                    ])
  ],
  [
    AC_MSG_RESULT(no)
    AC_CHECK_TYPE(sig_atomic_t, int)
    is_sig_atomic_t_volatile=no
  ])
if test $is_sig_atomic_t_volatile = 'yes'
then
    AC_DEFINE(SIG_ATOMIC_VOLATILE_T, sig_atomic_t)
else
    AC_DEFINE(SIG_ATOMIC_VOLATILE_T, [volatile sig_atomic_t])
fi


dnl ############################################################################
dnl Check for functions
dnl ############################################################################

AC_CHECK_FUNCS([\
    strverscmp \
    strncasecmp \
    realpath
])

dnl getpt is a GNU Extension (glibc 2.1.x)
AC_CHECK_FUNCS(posix_openpt, , [AC_CHECK_FUNCS(getpt)])
AC_CHECK_FUNCS(grantpt, , [AC_CHECK_LIB(pt, grantpt)])

dnl replacing lstat with statlstat on sco makes it more portable between
dnl sco clones
AC_CHECK_FUNCS(statlstat)

mc_GET_FS_INFO


dnl ############################################################################
dnl Internationalization
dnl ############################################################################

AC_CHECK_FUNCS([setlocale])

AM_GNU_GETTEXT([external], [need-ngettext])
AM_GNU_GETTEXT_VERSION([0.18.2])

mc_ENCODING

dnl ############################################################################
dnl OS specific stuff
dnl ############################################################################

AC_CHECK_DECLS([environ], , ,
  [ #include <unistd.h>
    #include <stdlib.h> ])

AC_PATH_PROG([PERL], [perl], [/usr/bin/env perl], [$PATH$PATH_SEPARATORQOpenSys/pkgs/bin])
AC_PATH_PROG([PERL_FOR_BUILD], [perl], [/usr/bin/env perl], [$PATH$PATH_SEPARATORQOpenSys/pkgs/bin])
AC_PATH_PROG([PYTHON], [python3], [/usr/bin/env python3], [$PATH$PATH_SEPARATORQOpenSys/pkgs/bin])
AC_PATH_PROG([RUBY], [ruby], [/usr/bin/env ruby], [$PATH$PATH_SEPARATORQOpenSys/pkgs/bin])

case $host_os in
aux*)
    # A/UX
    LIBS="$LIBS -lposix"
    AC_DEFINE(_POSIX_SOURCE)
    ;;
esac

dnl If running under AIX, AC_USE_SYSTEM_EXTENSIONS does not tell us that
AC_MSG_CHECKING([for AIX defines])
AC_EGREP_CPP([yes],
    [
        #if defined(AIX) || defined(_AIX) || defined(__aix__) || defined(aix)
        yes
        #endif
    ],
    [
        AC_DEFINE(IS_AIX, 1, [Define if compiling for AIX])
        AC_MSG_RESULT(yes)
    ],
    [
        AC_MSG_RESULT(no)
    ])

dnl utimensat is supported since glibc 2.6 and specified in POSIX.1-2008
AC_CHECK_FUNCS([utimensat])

case $host_os in
*os400)
    AC_DEFINE([PTY_ZEROREAD], [1], [read(1) can return 0 for a non-closed fd])
esac

dnl Check linux/fs.h for FICLONE to support BTRFS's file clone operation
case $host_os in
linux*)
    AC_CHECK_HEADERS([linux/fs.h])
esac

dnl Check if the OS is supported by the console saver.
cons_saver=""
case $host_os in
linux*)
    cons_saver=yes
esac

dnl Check for gpm mouse support (Linux only)
mouse_lib="xterm only"
AC_ARG_WITH([gpm-mouse],
    AS_HELP_STRING([--with-gpm-mouse], [Compile with gpm mouse support (Linux only) @<:@yes if found@:>@]))

case $host_os in
linux*)
    if test x$with_gpm_mouse != xno; then
        AC_CHECK_LIB(gpm, Gpm_Repeat,
            [AC_DEFINE(HAVE_LIBGPM, 1, [Define to enable gpm mouse support on Linux])
            mouse_lib="gpm and xterm"
            MCLIBS="$MCLIBS -lgpm"],
            if test "x$with_gpm_mouse" = "xyes"; then
                [AC_MSG_ERROR([libgpm is missing or older than 0.18])]
            else
                [AC_MSG_WARN([libgpm is missing or older than 0.18])]
            fi
        )
    fi
    ;;
esac


dnl ############################################################################
dnl libmc
dnl ############################################################################

LIBMC_VERSION="0.0.1"
LIBMC_RELEASE="1"
AC_SUBST(LIBMC_VERSION)
AC_SUBST(LIBMC_RELEASE)

AC_ARG_ENABLE([mclib],
        [AS_HELP_STRING([--enable-mclib], [Compile shared library libmc.so @<:@no@:>@])],
        [
            if test "x$enableval" = "xno" ; then
                enable_mclib=no
            else
                if test "x$enable_shared" = "xno" ; then
                    AC_MSG_WARN([Build of shared library is disabled. Specify --enable-shared first])
                    enable_mclib=no
                else
                    enable_mclib=yes
                fi
            fi
        ],
        [enable_mclib=no])

AM_CONDITIONAL([ENABLE_MCLIB], [test x$enable_mclib = xyes])


dnl ############################################################################
dnl MC options
dnl ############################################################################

mc_ASSERT

mc_WITH_INTERNAL_EDIT

dnl Diff viewer support.
AC_ARG_WITH([diff_viewer],
    AS_HELP_STRING([--with-diff-viewer], [Compile with diff viewer @<:@yes@:>@]))

if test x$with_diff_viewer != xno; then
        AC_DEFINE(USE_DIFF_VIEW, 1, [Define to enable diff viewer])
        use_diff=yes
        diff_msg="yes"
        AC_MSG_NOTICE([using diff viewer])
else
        diff_msg="no"
fi

mc_BACKGROUND

mc_MCTERM
dnl libssh2 is looked for once; the sftp and shell link plugins share the result
m4_include([m4.include/plugins/mc-libssh2.m4])
mc_LIBSSH2

mc_EXT2FS_ATTR
mc_VFS_CHECKS

AC_ARG_WITH([panel-plugins-dir],
    AS_HELP_STRING([--with-panel-plugins-dir=DIR],
        [Directory for dynamic panel plugins [LIBDIR/mc/panel-plugins]]),
    [panel_plugins_dir="$withval"],
    [panel_plugins_dir='${libdir}/mc/panel-plugins'])
AC_SUBST([panel_plugins_dir])

AC_ARG_WITH([editor-plugins-dir],
    AS_HELP_STRING([--with-editor-plugins-dir=DIR],
        [Directory for dynamic editor plugins [LIBDIR/mc/editor-plugins]]),
    [editor_plugins_dir="$withval"],
    [editor_plugins_dir='${libdir}/mc/editor-plugins'])
AC_SUBST([editor_plugins_dir])

AC_ARG_WITH([runtime-plugins-dir],
    AS_HELP_STRING([--with-runtime-plugins-dir=DIR],
        [Directory for dynamic runtime plugins [LIBDIR/mc/runtime-plugins]]),
    [runtime_plugins_dir="$withval"],
    [runtime_plugins_dir='${libdir}/mc/runtime-plugins'])
AC_SUBST([runtime_plugins_dir])

AC_ARG_ENABLE([lua-plugin],
    AS_HELP_STRING([--enable-lua-plugin@<:@=auto/yes/no@:>@],
        [Build the Lua runtime extension (default: auto)]),
    [enable_lua_plugin="$enableval"],
    [enable_lua_plugin=auto])

case "$enable_lua_plugin" in
    auto|yes|no) ;;
    *) AC_MSG_ERROR([--enable-lua-plugin expects auto, yes, or no]) ;;
esac

AC_ARG_WITH([lua],
    AS_HELP_STRING([--with-lua=PREFIX],
        [Lua installation prefix used while locating Lua 5.3+]),
    [with_lua="$withval"],
    [with_lua=""])

case "$with_lua" in
    yes|no) AC_MSG_ERROR([--with-lua requires an installation prefix]) ;;
esac

AC_ARG_WITH([lua-scripts-dir],
    AS_HELP_STRING([--with-lua-scripts-dir=DIR],
        [Directory for system Lua scripts [DATADIR/mc/lua/scripts]]),
    [lua_scripts_dir="$withval"],
    [lua_scripts_dir='${datadir}/mc/lua/scripts'])
AC_SUBST([lua_scripts_dir])

AC_ARG_WITH([lua-modules-dir],
    AS_HELP_STRING([--with-lua-modules-dir=DIR],
        [Directory for system shared Lua modules [DATADIR/mc/lua/lib]]),
    [lua_modules_dir="$withval"],
    [lua_modules_dir='${datadir}/mc/lua/lib'])
AC_SUBST([lua_modules_dir])

have_lua_plugin=no
lua_msg="no"
LUA_CFLAGS=""
LUA_LIBS=""

if test x"$enable_lua_plugin" != xno; then
    if test -z "$g_module_supported"; then
        if test x"$enable_lua_plugin" = xyes; then
            AC_MSG_ERROR([--enable-lua-plugin=yes requires GModule support])
        fi
        lua_msg="no (GModule unavailable)"
    else
        mc_saved_pkg_config_path="$PKG_CONFIG_PATH"
        if test -n "$with_lua"; then
            PKG_CONFIG_PATH="$with_lua/lib/pkgconfig:$with_lua/lib64/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
            export PKG_CONFIG_PATH
        fi

        AC_MSG_CHECKING([for Lua 5.3 or newer])
        for lua_pkg in lua5.4 lua-5.4 lua54 lua5.3 lua-5.3 lua53 lua; do
            if $PKG_CONFIG --exists "$lua_pkg >= 5.3"; then
                LUA_CFLAGS=`$PKG_CONFIG --cflags "$lua_pkg"`
                LUA_LIBS=`$PKG_CONFIG --libs "$lua_pkg"`
                have_lua_plugin=yes
                lua_msg=`$PKG_CONFIG --modversion "$lua_pkg"`
                break
            fi
        done
        AC_MSG_RESULT([$lua_msg])

        PKG_CONFIG_PATH="$mc_saved_pkg_config_path"
        export PKG_CONFIG_PATH

        if test x"$have_lua_plugin" = xno && test x"$enable_lua_plugin" = xyes; then
            AC_MSG_ERROR([Lua 5.3 or newer was not found])
        fi
    fi
fi

AC_SUBST([LUA_CFLAGS])
AC_SUBST([LUA_LIBS])
AM_CONDITIONAL([ENABLE_LUA_PLUGIN], [test x"$have_lua_plugin" = xyes])
if test x"$have_lua_plugin" = xyes; then
    AC_DEFINE([ENABLE_LUA_PLUGIN], [1], [Define to build the Lua runtime extension])
fi

dnl ############################################################################
dnl Directories
dnl ############################################################################

dnl ${prefix} and ${exec_prefix} are undefined here if --prefix is not used in command line
dnl Let define ${prefix} and ${exec_prefix}
test "x$prefix" = "xNONE" && prefix="$ac_default_prefix"
test "x$exec_prefix" = "xNONE" && exec_prefix="${prefix}"

if test x${libexecdir} = x'${exec_prefix}/libexec'; then
    EXTHELPERSDIR=${prefix}/libexec/${PACKAGE}/ext.d
    EXTFSHELPERSDIR=${prefix}/libexec/${PACKAGE}/extfs.d
elif test x${libexecdir} = x'${exec_prefix}/lib'; then
    EXTHELPERSDIR=${prefix}/lib/${PACKAGE}/ext.d
    EXTFSHELPERSDIR=${prefix}/lib/${PACKAGE}/extfs.d
else
    EXTHELPERSDIR=${libexecdir}/${PACKAGE}/ext.d
    EXTFSHELPERSDIR=${libexecdir}/${PACKAGE}/extfs.d
fi
AC_SUBST(EXTHELPERSDIR)
AC_SUBST(EXTFSHELPERSDIR)


dnl ############################################################################
dnl Documentation
dnl ############################################################################

MAN_DATE="$(LC_ALL=C date "+%B %Y")"
AC_SUBST(MAN_DATE)

dnl Determine which help translations we want to install.
ALL_DOC_LINGUAS="es hu it pl ru sr"

DOC_LINGUAS=
if test "x$USE_NLS" = xyes; then
    if test -z "$LINGUAS"; then
        langs="`grep -v '^#' $srcdir/po/LINGUAS`"
    else
        langs="$LINGUAS"
    fi
else
    langs=
fi

for h_lang in $ALL_DOC_LINGUAS; do
    for lang in $langs; do
        if test "$lang" = "$h_lang"; then
            DOC_LINGUAS="$DOC_LINGUAS $lang"
            break
        fi
    done
done
AC_SUBST(DOC_LINGUAS)

dnl ############################################################################
dnl Configure results
dnl ############################################################################

CFLAGS="$mc_configured_cflags $CFLAGS_OPTS $CFLAGS"

AC_SUBST(MCLIBS)
AC_SUBST(CFLAGS)
AC_SUBST(CPPFLAGS)
AC_SUBST(LDFLAGS)
AC_SUBST(LIBS)

AM_CONDITIONAL(USE_NLS, [test x"$USE_NLS" = xyes])
AM_CONDITIONAL(USE_MAINTAINER_MODE, [test x"$USE_MAINTAINER_MODE" = xyes])
AM_CONDITIONAL(USE_SCREEN_SLANG, [test x"$with_screen" = xslang])
AM_CONDITIONAL(USE_INTERNAL_EDIT, [test x"$use_internal_edit" = xyes ])
AM_CONDITIONAL(USE_DIFF, [test -n "$use_diff"])
AM_CONDITIONAL(CONS_SAVER, [test -n "$cons_saver"])
dnl Enable GModule when available (used for X11, aspell, and dynamic VFS plugins)
AM_CONDITIONAL([HAVE_GMODULE], [test -n "$g_module_supported"])

dnl Optional libmagic for mctree content detection
have_mctree_magic=no

AC_ARG_WITH([magic-file],
    AS_HELP_STRING([--with-magic-file=PATH],
                   [Use PATH as the libmagic database (default: libmagic default)]),
    [case "$withval" in
        yes|no|'') AC_MSG_ERROR([--with-magic-file requires an absolute path]) ;;
        /*) AC_DEFINE_UNQUOTED([MC_LIBMAGIC_FILE], ["$withval"],
                              [Path to the libmagic database, or NULL for its default]) ;;
        *) AC_MSG_ERROR([--with-magic-file requires an absolute path]) ;;
     esac],
    [AC_DEFINE([MC_LIBMAGIC_FILE], [NULL],
               [Path to the libmagic database, or NULL for its default])])

AC_ARG_ENABLE([mctree-magic],
    AS_HELP_STRING([--enable-mctree-magic@<:@=auto/yes/no@:>@],
                   [Use libmagic for mctree content detection (default: auto)]),
    [enable_mctree_magic="$enableval"],
    [enable_mctree_magic=auto])

AS_CASE([$enable_mctree_magic],
    [yes|no|auto], [],
    [AC_MSG_ERROR([bad value '$enable_mctree_magic' for --enable-mctree-magic (use auto, yes, or no)])])

if test "x$enable_mctree_magic" != xno; then
    PKG_CHECK_MODULES([MCTREE_MAGIC], [libmagic],
        [have_mctree_magic=yes
         AC_DEFINE([HAVE_MCTREE_MAGIC], [1], [Define if libmagic is available for mctree])],
        [:])
fi

if test "x$enable_mctree_magic" = xyes && test "x$have_mctree_magic" != xyes; then
    AC_MSG_ERROR([mctree libmagic support requested but libmagic was not found.
Install libmagic development package (for example: libmagic-dev) or use --enable-mctree-magic=auto/no.])
fi

if test "x$enable_mctree_magic" = xauto && test "x$have_mctree_magic" != xyes; then
    AC_MSG_WARN([mctree libmagic support disabled: libmagic was not found.])
fi

dnl Optional libsmbclient for Samba panel plugin
SMBCLIENT_CFLAGS=""
SMBCLIENT_LIBS=""
dnl sftp -- SFTP browser panel plugin (libssh2)
have_panel_plugin_sftp=no

AC_ARG_ENABLE([panel-plugin-sftp],
    AS_HELP_STRING([--enable-panel-plugin-sftp@<:@=auto/yes/no@:>@],
                   [Build SFTP panel plugin (default: auto)]),
    [enable_panel_plugin_sftp="$enableval"],
    [enable_panel_plugin_sftp=auto])

AS_CASE([$enable_panel_plugin_sftp],
    [yes|no|auto], [],
    [AC_MSG_ERROR([bad value '$enable_panel_plugin_sftp' for --enable-panel-plugin-sftp (use auto, yes, or no)])])

if test "x$enable_panel_plugin_sftp" != xno && test x"$have_libssh2" = xyes; then
    have_panel_plugin_sftp=yes
fi

if test "x$enable_panel_plugin_sftp" = xyes && test "x$have_panel_plugin_sftp" != xyes; then
    AC_MSG_ERROR([SFTP panel plugin requested but libssh2 >= 1.2.8 was not found.
Install the libssh2 development package (for example: libssh2-1-dev) or use
--enable-panel-plugin-sftp=auto/no.])
fi

if test "x$enable_panel_plugin_sftp" = xauto && test "x$have_panel_plugin_sftp" != xyes; then
    AC_MSG_WARN([SFTP panel plugin disabled: libssh2 >= 1.2.8 was not found.])
fi

AM_CONDITIONAL([ENABLE_PANEL_PLUGIN_SFTP], [test x"$have_panel_plugin_sftp" = xyes])

have_panel_plugin_samba=no

AC_ARG_ENABLE([panel-plugin-samba],
    AS_HELP_STRING([--enable-panel-plugin-samba@<:@=auto/yes/no@:>@],
                   [Build Samba panel plugin (default: auto)]),
    [enable_panel_plugin_samba="$enableval"],
    [enable_panel_plugin_samba=auto])

AS_CASE([$enable_panel_plugin_samba],
    [yes|no|auto], [],
    [AC_MSG_ERROR([bad value '$enable_panel_plugin_samba' for --enable-panel-plugin-samba (use auto, yes, or no)])])

if test "x$enable_panel_plugin_samba" != xno; then
    PKG_CHECK_MODULES([SMBCLIENT], [smbclient], [have_panel_plugin_samba=yes], [:])
fi

if test "x$enable_panel_plugin_samba" = xyes && test "x$have_panel_plugin_samba" != xyes; then
    AC_MSG_ERROR([Samba panel plugin requested but libsmbclient was not found.
Install libsmbclient development package (for example: libsmbclient-dev) or use --enable-panel-plugin-samba=auto/no.])
fi

if test "x$enable_panel_plugin_samba" = xauto && test "x$have_panel_plugin_samba" != xyes; then
    AC_MSG_WARN([Samba panel plugin disabled: libsmbclient was not found.])
fi

AM_CONDITIONAL([ENABLE_PANEL_PLUGIN_SAMBA], [test x"$have_panel_plugin_samba" = xyes])

dnl Shell link panel plugin (sh:// connection manager)
AC_ARG_ENABLE([panel-plugin-shell-link],
    AS_HELP_STRING([--enable-panel-plugin-shell-link@<:@=auto/yes/no@:>@],
                   [Build Shell link panel plugin (default: auto)]),
    [enable_panel_plugin_shell_link="$enableval"],
    [enable_panel_plugin_shell_link=auto])

AS_CASE([$enable_panel_plugin_shell_link],
    [yes|no|auto], [],
    [AC_MSG_ERROR([bad value '$enable_panel_plugin_shell_link' for --enable-panel-plugin-shell-link (use auto, yes, or no)])])

have_panel_plugin_shell_link=no
if test "x$enable_panel_plugin_shell_link" != xno; then
    have_panel_plugin_shell_link=yes
fi

AM_CONDITIONAL([ENABLE_PANEL_PLUGIN_SHELL_LINK],
               [test x"$have_panel_plugin_shell_link" = xyes])

dnl libssh2 transport for the shell link plugin. Optional: without it the
dnl plugin talks to an external ssh, which cannot do password authentication
dnl but handles keys, the agent and host key confirmation on the terminal.
have_shell_ssh2=no
AC_ARG_ENABLE([shell-ssh2],
    AS_HELP_STRING([--enable-shell-ssh2@<:@=auto/yes/no@:>@],
                   [Use libssh2 as Shell link transport (default: auto)]),
    [enable_shell_ssh2="$enableval"],
    [enable_shell_ssh2=auto])

AS_CASE([$enable_shell_ssh2],
    [yes|no|auto], [],
    [AC_MSG_ERROR([bad value '$enable_shell_ssh2' for --enable-shell-ssh2 (use auto, yes, or no)])])

if test x"$have_panel_plugin_shell_link" = xyes -a x"$enable_shell_ssh2" != xno; then
    if test x"$have_libssh2" = xyes; then
        AC_DEFINE([ENABLE_SHELL_SSH2], [1], [Use libssh2 as Shell link transport])
        have_shell_ssh2=yes
    elif test x"$enable_shell_ssh2" = xyes; then
        AC_MSG_ERROR([Shell link libssh2 transport requested but libssh2 >= 1.2.8 was not found.
Install the libssh2 development package (for example: libssh2-1-dev) or use --enable-shell-ssh2=auto/no.])
    fi
fi

AM_CONDITIONAL([ENABLE_SHELL_SSH2], [test x"$have_shell_ssh2" = xyes])

dnl Optional libcurl for FTP/FTPS panel plugin
LIBCURL_CFLAGS=""
LIBCURL_LIBS=""
have_panel_plugin_ftp=no

AC_ARG_ENABLE([panel-plugin-ftp],
    AS_HELP_STRING([--enable-panel-plugin-ftp@<:@=auto/yes/no@:>@],
                   [Build FTP/FTPS panel plugin (default: auto)]),
    [enable_panel_plugin_ftp="$enableval"],
    [enable_panel_plugin_ftp=auto])

AS_CASE([$enable_panel_plugin_ftp],
    [yes|no|auto], [],
    [AC_MSG_ERROR([bad value '$enable_panel_plugin_ftp' for --enable-panel-plugin-ftp (use auto, yes, or no)])])

if test "x$enable_panel_plugin_ftp" != xno; then
    PKG_CHECK_MODULES([LIBCURL], [libcurl >= 7.20.0], [
        save_CPPFLAGS="$CPPFLAGS"
        save_LIBS="$LIBS"
        CPPFLAGS="$CPPFLAGS $LIBCURL_CFLAGS"
        LIBS="$LIBCURL_LIBS $LIBS"
        AC_LINK_IFELSE(
            [AC_LANG_PROGRAM([[#include <curl/curl.h>]], [[curl_easy_init ();]])],
            [have_panel_plugin_ftp=yes],
            [have_panel_plugin_ftp=no]
        )
        CPPFLAGS="$save_CPPFLAGS"
        LIBS="$save_LIBS"
    ], [:])
fi

if test "x$enable_panel_plugin_ftp" = xyes && test "x$have_panel_plugin_ftp" != xyes; then
    AC_MSG_ERROR([FTP panel plugin requested but usable libcurl headers/libs were not found.
Install libcurl development package (for example: libcurl4-openssl-dev) or use --enable-panel-plugin-ftp=auto/no.])
fi

if test "x$enable_panel_plugin_ftp" = xauto && test "x$have_panel_plugin_ftp" != xyes; then
    AC_MSG_WARN([FTP panel plugin disabled: usable libcurl headers/libs were not found.])
fi

AM_CONDITIONAL([ENABLE_PANEL_PLUGIN_FTP], [test x"$have_panel_plugin_ftp" = xyes])

dnl arcmc — archive browser panel plugin (libarchive)

AC_ARG_ENABLE([panel-plugin-arcmc],
    AS_HELP_STRING([--enable-panel-plugin-arcmc@<:@=auto/yes/no@:>@],
                   [Build archive browser panel plugin (default: auto)]),
    [enable_panel_plugin_arcmc="$enableval"],
    [enable_panel_plugin_arcmc=auto])

AS_CASE([$enable_panel_plugin_arcmc],
    [yes|no|auto], [],
    [AC_MSG_ERROR([bad value '$enable_panel_plugin_arcmc' for --enable-panel-plugin-arcmc (use auto, yes, or no)])])

if test "x$enable_panel_plugin_arcmc" != xno; then
    PKG_CHECK_MODULES([LIBARCHIVE], [libarchive >= 3.0], [
        have_panel_plugin_arcmc=yes
    ], [
        dnl FreeBSD carries libarchive in the base system and ships no
        dnl pkg-config file for it. archive_read_free arrived in 3.0.
        AC_CHECK_HEADER([archive.h], [
            AC_CHECK_LIB([archive], [archive_read_free], [
                LIBARCHIVE_CFLAGS=""
                LIBARCHIVE_LIBS="-larchive"
                have_panel_plugin_arcmc=yes
            ])
        ])
    ])
fi

dnl Optional libmagic support for arcmc content-based archive detection
if test "x$have_panel_plugin_arcmc" = xyes; then
    AC_CHECK_HEADERS([magic.h], [
        dnl Not AC_CHECK_LIB: it declares the function itself and without a
        dnl prototype, so -Werror=strict-prototypes fails the test before the
        dnl link is even tried and --enable-werror builds lose libmagic. The
        dnl header declares it properly, so ask it.
        mc_saved_LIBS="$LIBS"
        LIBS="$LIBS -lmagic"
        AC_MSG_CHECKING([for magic_open in -lmagic])
        AC_LINK_IFELSE([AC_LANG_PROGRAM([[#include <magic.h>]],
                                        [[magic_t cookie = magic_open (0);
                                          magic_close (cookie);]])],
            [AC_MSG_RESULT([yes])
             AC_DEFINE([HAVE_LIBMAGIC], [1], [Define if libmagic is available])
             LIBMAGIC_LIBS="-lmagic"
             LIBMAGIC_CFLAGS=""],
            [AC_MSG_RESULT([no])])
        LIBS="$mc_saved_LIBS"
    ])
    AC_SUBST([LIBMAGIC_LIBS])
    AC_SUBST([LIBMAGIC_CFLAGS])
fi

if test "x$enable_panel_plugin_arcmc" = xyes && test "x$have_panel_plugin_arcmc" != xyes; then
    AC_MSG_ERROR([Archive browser panel plugin requested but libarchive >= 3.0 was not found.
Install libarchive development package (for example: libarchive-dev) or use --enable-panel-plugin-arcmc=auto/no.])
fi

if test "x$enable_panel_plugin_arcmc" = xauto && test "x$have_panel_plugin_arcmc" != xyes; then
    AC_MSG_WARN([Archive browser panel plugin disabled: libarchive >= 3.0 was not found.])
fi

AM_CONDITIONAL([ENABLE_PANEL_PLUGIN_ARCMC], [test x"$have_panel_plugin_arcmc" = xyes])

MAGIC_ARCMC_RULES=""
if test "x$have_panel_plugin_arcmc" = xyes; then
    MAGIC_ARCMC_RULES='[[arcmc.archive]]
Regex=\\.(tar|tar\\.gz|tgz|tar\\.bz2|tbz2|tar\\.xz|txz|tar\\.zst|tzst|zip|jar|war|ear|7z|cpio)$
RegexIgnoreCase=true
Open=%plugin{arcmc:open}
View=%plugin{arcmc:view}'
fi
AC_SUBST([MAGIC_ARCMC_RULES])
dnl Several lines: it belongs in magic.ini, and a Makefile cannot hold it.
AM_SUBST_NOTMAKE([MAGIC_ARCMC_RULES])

dnl Optional libcurl >= 7.75.0 for S3 panel plugin (needs CURLOPT_AWS_SIGV4)
LIBCURL_S3_CFLAGS=""
LIBCURL_S3_LIBS=""
have_panel_plugin_s3=no

AC_ARG_ENABLE([panel-plugin-s3],
    AS_HELP_STRING([--enable-panel-plugin-s3@<:@=auto/yes/no@:>@],
                   [Build S3 object storage panel plugin (default: auto)]),
    [enable_panel_plugin_s3="$enableval"],
    [enable_panel_plugin_s3=auto])

AS_CASE([$enable_panel_plugin_s3],
    [yes|no|auto], [],
    [AC_MSG_ERROR([bad value '$enable_panel_plugin_s3' for --enable-panel-plugin-s3 (use auto, yes, or no)])])

if test "x$enable_panel_plugin_s3" != xno; then
    PKG_CHECK_MODULES([LIBCURL_S3], [libcurl >= 7.75.0], [
        save_CPPFLAGS="$CPPFLAGS"
        save_LIBS="$LIBS"
        CPPFLAGS="$CPPFLAGS $LIBCURL_S3_CFLAGS"
        LIBS="$LIBCURL_S3_LIBS $LIBS"
        AC_LINK_IFELSE(
            [AC_LANG_PROGRAM([[#include <curl/curl.h>]], [[curl_easy_init ();]])],
            [have_panel_plugin_s3=yes],
            [have_panel_plugin_s3=no]
        )
        CPPFLAGS="$save_CPPFLAGS"
        LIBS="$save_LIBS"
    ], [:])
fi

if test "x$enable_panel_plugin_s3" = xyes && test "x$have_panel_plugin_s3" != xyes; then
    AC_MSG_ERROR([S3 panel plugin requested but libcurl >= 7.75.0 was not found.
Install libcurl development package (for example: libcurl4-openssl-dev) or use --enable-panel-plugin-s3=auto/no.])
fi

if test "x$enable_panel_plugin_s3" = xauto && test "x$have_panel_plugin_s3" != xyes; then
    AC_MSG_WARN([S3 panel plugin disabled: libcurl >= 7.75.0 was not found.])
fi

AM_CONDITIONAL([ENABLE_PANEL_PLUGIN_S3], [test x"$have_panel_plugin_s3" = xyes])

dnl mongo -- MongoDB browser panel plugin (libmongoc + libbson)
have_panel_plugin_mongo=no

AC_ARG_ENABLE([panel-plugin-mongo],
    AS_HELP_STRING([--enable-panel-plugin-mongo@<:@=auto/yes/no@:>@],
                   [Build MongoDB panel plugin (default: auto)]),
    [enable_panel_plugin_mongo="$enableval"],
    [enable_panel_plugin_mongo=auto])

AS_CASE([$enable_panel_plugin_mongo],
    [yes|no|auto], [],
    [AC_MSG_ERROR([bad value '$enable_panel_plugin_mongo' for --enable-panel-plugin-mongo (use auto, yes, or no)])])

if test "x$enable_panel_plugin_mongo" != xno; then
    dnl Driver 2 renamed its pkg-config modules to mongoc2 and bson2, and ships
    dnl them as one package on some distributions. The plugin calls nothing that
    dnl 2 dropped, so either generation will do.
    PKG_CHECK_MODULES([MONGOC],
        [libmongoc-1.0 >= 1.21 libbson-1.0 >= 1.21],
        [have_panel_plugin_mongo=yes],
        [PKG_CHECK_MODULES([MONGOC2],
            [mongoc2 >= 2.0 bson2 >= 2.0],
            [have_panel_plugin_mongo=yes
             MONGOC_CFLAGS="$MONGOC2_CFLAGS"
             MONGOC_LIBS="$MONGOC2_LIBS"],
            [:])])
fi

if test "x$enable_panel_plugin_mongo" = xyes && test "x$have_panel_plugin_mongo" != xyes; then
    AC_MSG_ERROR([MongoDB panel plugin requested but no MongoDB C driver was found:
neither libmongoc-1.0 >= 1.21 with libbson-1.0 >= 1.21, nor mongoc2 >= 2.0 with bson2 >= 2.0.
Install the MongoDB C driver development package (for example: libmongoc-dev and
libbson-dev, or mongo-c-driver) or use --enable-panel-plugin-mongo=auto/no.])
fi

if test "x$enable_panel_plugin_mongo" = xauto && test "x$have_panel_plugin_mongo" != xyes; then
    AC_MSG_WARN([MongoDB panel plugin disabled: no MongoDB C driver found (libmongoc-1.0 or mongoc2).])
fi

AM_CONDITIONAL([ENABLE_PANEL_PLUGIN_MONGO], [test x"$have_panel_plugin_mongo" = xyes])

dnl sqlite -- read-only SQLite database browser panel plugin
have_panel_plugin_sqlite=no

AC_ARG_ENABLE([panel-plugin-sqlite],
    AS_HELP_STRING([--enable-panel-plugin-sqlite@<:@=auto/yes/no@:>@],
                   [Build SQLite database browser panel plugin (default: auto)]),
    [enable_panel_plugin_sqlite="$enableval"],
    [enable_panel_plugin_sqlite=auto])

AS_CASE([$enable_panel_plugin_sqlite],
    [yes|no|auto], [],
    [AC_MSG_ERROR([bad value '$enable_panel_plugin_sqlite' for --enable-panel-plugin-sqlite (use auto, yes, or no)])])

if test "x$enable_panel_plugin_sqlite" != xno; then
    PKG_CHECK_MODULES([SQLITE3], [sqlite3 >= 3.26], [have_panel_plugin_sqlite=yes], [:])
fi

if test "x$enable_panel_plugin_sqlite" = xyes && test "x$have_panel_plugin_sqlite" != xyes; then
    AC_MSG_ERROR([SQLite panel plugin requested but sqlite3 >= 3.26 development files were not found.
Install the SQLite development package (for example: libsqlite3-dev) or use
--enable-panel-plugin-sqlite=auto/no.])
fi

if test "x$enable_panel_plugin_sqlite" = xauto && test "x$have_panel_plugin_sqlite" != xyes; then
    AC_MSG_WARN([SQLite panel plugin disabled: sqlite3 >= 3.26 development files were not found.])
fi

AM_CONDITIONAL([ENABLE_PANEL_PLUGIN_SQLITE], [test x"$have_panel_plugin_sqlite" = xyes])

MAGIC_SQLITE_RULES=""
if test "x$have_panel_plugin_sqlite" = xyes; then
    MAGIC_SQLITE_RULES='[[sqlite.database]]
Type=^SQLite 3\\.x database
Open=%plugin{sqlite:open}
View=%plugin{sqlite:view}

[[sqlite.database-name]]
Regex=\\.(sqlite|sqlite3|sqlitedb)$
RegexIgnoreCase=true
Open=%plugin{sqlite:open}
View=%plugin{sqlite:view}'
fi
AC_SUBST([MAGIC_SQLITE_RULES])
AM_SUBST_NOTMAKE([MAGIC_SQLITE_RULES])

AC_ARG_ENABLE([configure-args],
    AS_HELP_STRING([--enable-configure-args], [Embed ./configure arguments into binaries]))
if test "x$enable_configure_args" != xno; then
    AC_DEFINE([ENABLE_CONFIGURE_ARGS], 1, [Define to enable showing configure arguments in help])
    AC_DEFINE_UNQUOTED([MC_CONFIGURE_ARGS], ["$ac_configure_args"], [MC configure arguments])
fi


AC_CONFIG_FILES(
[
src/man2hlp/man2hlp
],
[
chmod +x src/man2hlp/man2hlp
])

AC_CONFIG_FILES([
Makefile

contrib/Makefile

misc/Makefile
misc/mc.charsets
misc/mc.menu
misc/mcedit.menu
misc/skins/Makefile
misc/ext.d/Makefile
misc/ext.d/misc.sh
misc/ext.d/text.sh
misc/macros.d/Makefile
misc/mc.ext.ini
misc/magic.ini

src/Makefile
src/consaver/Makefile
src/editor/Makefile
src/lua/Makefile
src/man2hlp/Makefile
src/mctree/Makefile
src/viewer/Makefile
src/mcterm/Makefile
src/diffviewer/Makefile
src/filemanager/Makefile

src/panel-plugins/Makefile
src/panel-plugins/hello/Makefile
src/panel-plugins/panelize/Makefile
src/panel-plugins/samba/Makefile
src/panel-plugins/ftp/Makefile
src/panel-plugins/systemd/Makefile
src/panel-plugins/sftp/Makefile
src/panel-plugins/docker/Makefile
src/panel-plugins/git/Makefile
src/panel-plugins/k8s/Makefile
src/panel-plugins/shell-link/Makefile
src/panel-plugins/shell-link/helpers/Makefile
src/panel-plugins/arcmc/Makefile
src/panel-plugins/s3/Makefile
src/panel-plugins/mongo/Makefile
src/panel-plugins/sqlite/Makefile

src/vfs/Makefile

src/vfs/extfs/Makefile
src/vfs/extfs/helpers/Makefile
src/vfs/extfs/helpers/a+
src/vfs/extfs/helpers/apt+
src/vfs/extfs/helpers/audio
src/vfs/extfs/helpers/brotli
src/vfs/extfs/helpers/deb
src/vfs/extfs/helpers/deba
src/vfs/extfs/helpers/debd
src/vfs/extfs/helpers/dpkg+
src/vfs/extfs/helpers/iso9660
src/vfs/extfs/helpers/hp48+
src/vfs/extfs/helpers/lslR
src/vfs/extfs/helpers/mailfs
src/vfs/extfs/helpers/patchfs
src/vfs/extfs/helpers/rpms+
src/vfs/extfs/helpers/s3+
src/vfs/extfs/helpers/torrent
src/vfs/extfs/helpers/uace
src/vfs/extfs/helpers/ualz
src/vfs/extfs/helpers/uar
src/vfs/extfs/helpers/uarc
src/vfs/extfs/helpers/uarj
src/vfs/extfs/helpers/uc1541
src/vfs/extfs/helpers/ucab
src/vfs/extfs/helpers/uha
src/vfs/extfs/helpers/ulha
src/vfs/extfs/helpers/ulib
src/vfs/extfs/helpers/unar
src/vfs/extfs/helpers/urar
src/vfs/extfs/helpers/usqfs
src/vfs/extfs/helpers/uwim
src/vfs/extfs/helpers/uzip
src/vfs/extfs/helpers/uzoo


src/vfs/local/Makefile

src/vfs/sfs/Makefile

lib/Makefile
lib/event/Makefile
lib/filehighlight/Makefile
lib/mcconfig/Makefile
lib/search/Makefile
lib/skin/Makefile
lib/strutil/Makefile
lib/tty/Makefile

lib/vfs/Makefile

lib/widget/Makefile

misc/syntax/Makefile

doc/Makefile

doc/hints/Makefile
doc/hints/l10n/Makefile

doc/man/Makefile
doc/man/es/Makefile
doc/man/hu/Makefile
doc/man/it/Makefile
doc/man/pl/Makefile
doc/man/ru/Makefile
doc/man/sr/Makefile

doc/hlp/Makefile
doc/hlp/es/Makefile
doc/hlp/hu/Makefile
doc/hlp/it/Makefile
doc/hlp/pl/Makefile
doc/hlp/ru/Makefile
doc/hlp/sr/Makefile

po/Makefile.in
])

dnl https://stackoverflow.com/questions/30897170/ac-subst-does-not-expand-variable/30932102#30932102
AC_CONFIG_FILES(
[misc/syntax/Syntax], [${SED-sed} -e "s%\${prefix}%$PREFIX%" misc/syntax/Syntax > misc/syntax/Syntax.tmp && \
  mv -f misc/syntax/Syntax.tmp misc/syntax/Syntax], [export PREFIX=$prefix]
)

AC_CONFIG_FILES([
tests/Makefile
tests/lib/Makefile
tests/lib/mcconfig/Makefile
tests/lib/search/Makefile
tests/lib/strutil/Makefile
tests/lib/vfs/Makefile
tests/lib/vfs/mc.charsets
tests/lib/widget/Makefile
tests/src/Makefile
tests/src/filemanager/Makefile
tests/src/viewer/Makefile
tests/src/editor/Makefile
tests/src/editor/edit_complete_word_cmd_test_data.txt
tests/src/vfs/Makefile
tests/src/vfs/extfs/Makefile
tests/src/vfs/extfs/helpers-list/Makefile
tests/src/vfs/extfs/helpers-list/data/config.sh
tests/src/vfs/extfs/helpers-list/misc/Makefile
tests/src/panel-plugins/Makefile
tests/src/panel-plugins/ftp/Makefile
tests/src/panel-plugins/arcmc/Makefile
tests/src/panel-plugins/samba/Makefile
tests/src/panel-plugins/docker/Makefile
tests/src/panel-plugins/mongo/Makefile
tests/src/panel-plugins/sqlite/Makefile
tests/src/panel-plugins/pp_common/Makefile
tests/misc/Makefile
tests/misc/ext.d/Makefile
tests/misc/ext.d/markdown/Makefile
])

AC_OUTPUT

AC_MSG_NOTICE([

Configuration:

  Source code location:           ${srcdir}
  Compiler:                       ${CC}
  Compiler flags:                 ${CFLAGS}
  Assertions:                     ${enable_assert}
  Unit tests:                     ${tests_msg}
  File system:                    ${vfs_type}
                                  ${vfs_flags}
  Screen library:                 ${screen_msg}
  Mouse support:                  ${mouse_lib}
  X11 events support:             ${textmode_x11_support}
  With background operations:     ${enable_background}
  With ext2fs attributes support: ${ext2fs_attr_msg}
  Internal editor:                ${edit_msg}
  Diff viewer:                    ${diff_msg}
  Lua runtime extension:          ${lua_msg}
])

dnl option checking is disable by default due to AC_CONFIG_SUBDIRS
dnl we enable it back for top-level ./configure
if test -n "$ac_unrecognized_opts"; then
  case $enable_option_checking in
    fatal) AC_MSG_ERROR([unrecognized options: $ac_unrecognized_opts]) ;;
    *)     AC_MSG_WARN( [unrecognized options: $ac_unrecognized_opts]) ;;
  esac
fi
