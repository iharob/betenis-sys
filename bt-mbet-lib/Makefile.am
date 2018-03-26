noinst_LIBRARIES = libbt-mbet.a

libbt_mbet_a_SOURCES =         \
        src/bt-mbet-feed.c     \
        src/bt-mbet-xml.c      \
        src/bt-mbet-score.c    \
        include/bt-mbet-feed.h \
        include/bt-mbet-xml.h  \
        include/bt-mbet-score.h

libbt_mbet_a_CFLAGS =                 \
  -I$(srcdir)/include                 \
  -I$(top_srcdir)/bt/include          \
  -I$(top_srcdir)/bt-util-lib/include \
    $(LIBXML_2_CFLAGS)                \
    $(MYSQL_CFLAGS)                   \
    $(JSON_C_CFLAGS)                  \
    $(HTTP_IO_CFLAGS)

clang-analyze: $(libbt_mbet_a_SOURCES:.c=.clang-analyze)
	@true

cppcheck: $(libbt_mbet_a_SOURCES:.c=.cppcheck)
	@true

.c.cppcheck:
	@cppcheck $(libbt_mbet_a_CFLAGS) $(CPPCHECK_OPTIONS) $(CPPCHECK_LIBRARIES) $<

.c.clang-analyze:
	@echo Analyzing $<
	@clang --analyze $(libbt_mbet_a_CFLAGS) $<
