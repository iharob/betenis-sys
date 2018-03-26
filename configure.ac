AC_INIT([bt], [0.0.0], [iharob@gmail.com])
AM_INIT_AUTOMAKE([subdir-objects])

AC_PROG_RANLIB
AC_PROG_CC
AC_CHECK_PROG([MYSQL_CONFIG], [mysql_config], [yes], [no])
AC_CHECK_PROG([DOXYGEN], [doxygen], [yes], [no])

AM_EXTRA_RECURSIVE_TARGETS([cppcheck])
AM_EXTRA_RECURSIVE_TARGETS([clang-analyze])
AS_IF([test "x$MYSQL_CONFIG" == "xno"],
    AC_MSG_FAILURE([
        No pude encontrar 'mysql_config' necesario
        para construir BeTenis Daemon
    ])
)

PKG_CHECK_MODULES([MONGOC], [libmongoc-1.0 >= 1.0])
PKG_CHECK_MODULES([HTTP_IO], [libhttpio >= 1.0.0])
PKG_CHECK_MODULES([JSON_C], [json-c >= 0.11])
PKG_CHECK_MODULES([LIBXML_2], [libxml-2.0 >= 2.7])
PKG_CHECK_MODULES([PCRE], [libpcre >= 7.8])

AC_SUBST([CFLAGS], "${CFLAGS} -std=c99")
AC_SUBST([CFLAGS], "${CFLAGS} -pthread")

AC_DEFINE([_GNU_SOURCE])
AC_ARG_ENABLE(
    [debug],
    AS_HELP_STRING([--enable-debug], [habilitar depuración en los binarios]),
    [
        AC_SUBST([LDFLAGS], "${CFLAGS} -O0 -g3 -rdynamic")
        AC_SUBST([CFLAGS], "${CFLAGS} -O0 -g3 -Wall -Werror -D_DEBUG")
    ],
    [
        AC_SUBST([LDFLAGS], "${CFLAGS} -O2 -g0")
        AC_SUBST([CFLAGS], "${CFLAGS} -O2 -g0")
    ]
)

AC_ARG_WITH(
    [user],
    AS_HELP_STRING([--with-user], [usuario de la base de datos]),
    [
        AC_SUBST([user], ["$with_user"])
    ],[
        AC_SUBST([user], ["root"])
    ]
)

AC_ARG_WITH(
    [password],
    AS_HELP_STRING([--with-password], [password de la base de datos]),
    [
        AC_SUBST([password], ["$with_password"])
    ],[
        AC_SUBST([password], ["AMVYJ5O345"])
    ]
)

AC_ARG_WITH(
    [database],
    AS_HELP_STRING([--with-database], [nombre de la base de datos]),
    [
        AC_SUBST([database], ["$with_database"])
    ],[
        AC_SUBST([database], ["bt"])
    ]
)

AC_ARG_WITH(
    [host],
    AS_HELP_STRING([--with-host], [máquina en la que corre MySQL]),
    [
        AC_SUBST([host], ["$with_host"])
    ],[
        AC_SUBST([host], ["127.0.0.1"])
    ]
)

MYSQL_LIBS=$(mysql_config --libs)
MYSQL_CFLAGS=$(mysql_config --cflags)

AC_SUBST(CFLAGS)
AC_SUBST(LDFLAGS)
AC_SUBST(MYSQL_CFLAGS)
AC_SUBST(MYSQL_LIBS)

AC_SUBST([CPPCHECK_OPTIONS], ["-D_DEBUG --template='{file}:{line}: {severity}: {message}' --suppress=varFuncNullUB --check-library --language=c --enable=warning,style,performance"])
result=""; 
for x in `dirname $0`/quality/*.cfg;
do 
    top_srcdir='$(top_srcdir)'
    file=`basename $x`
    result="$result --library=$top_srcdir/quality/$x";
done;
AC_SUBST([CPPCHECK_LIBRARIES], [$result])

AS_IF([test "x$DOXYGEN" == "xyes"], [
    AC_CONFIG_FILES([doc/Doxyfile], [doxygen doc/Doxyfile])
])

AC_CONFIG_FILES([
    Makefile
    data/Makefile
    players.data/Makefile
    data/bt.conf
    bt-mbet-lib/Makefile
    bt-util-lib/Makefile
    bt-ls/Makefile
    bt/Makefile
])

AC_OUTPUT
