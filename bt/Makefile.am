bin_PROGRAMS = bt
bt_SOURCES =                             \
        src/bt-channel-settings.c        \
        src/bt-context.c                 \
        src/bt-timer.c                   \
        src/bt-drops.c                   \
        src/bt-oncourt-database.c        \
        src/bt-oncourt-dogs.c            \
        src/bt-oncourt-retires.c         \
        src/bt-telegram-channel.c        \
        src/bt-string-builder.c          \
        src/bt-william-hill.c            \
        src/bt-william-hill-main.c       \
        src/bt-william-hill-events.c     \
        src/bt-william-hill-topics.c     \
        src/bt-mbet.c                    \
        src/bt-pinnacle.c                \
        include/bt-context.h             \
        include/bt-channel-settings.h    \
        include/bt-timer.h               \
        include/bt-drops.h               \
        include/bt-oncourt-database.h    \
        include/bt-oncourt-dogs.h        \
        include/bt-oncourt-retires.h     \
        include/bt-telegram-channel.h    \
        include/bt-string-builder.h      \
        include/bt-william-hill.h        \
        include/bt-william-hill-main.h   \
        include/bt-william-hill-events.h \
        include/bt-william-hill-topics.h \
        include/bt-mbet.h                \
        include/bt-pinnacle.h            \
        src/bt-main.c
 
bt_CFLAGS =            \
    $(MYSQL_CFLAGS)    \
    $(JSON_C_CFLAGS)   \
    $(LIBXML_2_CFLAGS) \
    $(PCRE_CFLAGS)     \
    $(CURL_CFLAGS)     \
    $(HTTP_IO_CFLAGS)  \
  -I$(top_srcdir)/bt-mbet-lib/include \
  -I$(top_srcdir)/bt-util-lib/include \
  -I$(srcdir)/include

bt_LDADD = -lrt              \
    $(top_builddir)/bt-mbet-lib/libbt-mbet.a \
    $(top_builddir)/bt-util-lib/libbt-util.a \
    $(MYSQL_LIBS)            \
    $(JSON_C_LIBS)           \
    $(LIBXML_2_LIBS)         \
    $(PCRE_LIBS)             \
    $(CURL_LIBS)             \
    $(HTTP_IO_LIBS)

clang-analyze: $(bt_SOURCES:.c=.clang-analyze)
	@true

cppcheck: $(bt_SOURCES:.c=.cppcheck)
	@true

.c.cppcheck:
	@cppcheck $(bt_CFLAGS) $(CPPCHECK_OPTIONS) $(CPPCHECK_LIBRARIES) $<
	
.c.clang-analyze:
	@echo Analyzing $<
	@clang --analyze $(bt_CFLAGS) $<
