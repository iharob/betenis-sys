noinst_LIBRARIES = libbt-util.a
libbt_util_a_SOURCES =               \
    src/bt-daemon.c                  \
    src/bt-util.c                    \
    src/bt-http-headers.c            \
    src/bt-database.c                \
    src/bt-oncourt-players-map.c     \
    src/bt-mysql-easy.c              \
    src/bt-memory.c                  \
    include/bt-daemon.h              \
    include/bt-util.h                \
    include/bt-http-headers.h        \
    include/bt-database.h            \
    include/bt-oncourt-players-map.h \
    include/bt-mysql-easy.h          \
    include/bt-memory.h
libbt_util_a_CFLAGS =                 \
  -I$(srcdir)/include                 \
  -I$(top_srcdir)/bt/include          \
  -I$(top_srcdir)/bt-mbet-lib/include \
    $(MYSQL_CFLAGS)                   \
    $(LIBXML_2_CFLAGS)                \
    $(HTTP_IO_CFLAGS)                 \
    $(JSON_C_CFLAGS)


clang-analyze: $(libbt_util_a_SOURCES:.c=.clang-analyze)
	@true

cppcheck: $(libbt_util_a_SOURCES:.c=.cppcheck)
	@true

.c.cppcheck:
	@cppcheck $(libbt_util_a_CFLAGS) $(CPPCHECK_OPTIONS) $(CPPCHECK_LIBRARIES) $<

.c.clang-analyze:
	@echo Analyzing $<
	@clang --analyze $(libbt_util_a_CFLAGS) $<
