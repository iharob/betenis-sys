bin_PROGRAMS = bt-ls

bt_ls_SOURCES = src/bt-ls.c
bt_ls_LDADD = -lm	                     \
    $(top_builddir)/bt-mbet-lib/libbt-mbet.a \
    $(top_builddir)/bt-util-lib/libbt-util.a \
    $(LIBXML_2_LIBS)	                     \
    $(HTTP_IO_LIBS)	                     \
    $(MYSQL_LIBS)                            \
    $(MONGOC_LIBS)                           \
    $(JSON_C_LIBS)

bt_ls_CFLAGS =		              \
  -I$(top_srcdir)/bt-mbet-lib/include \
  -I$(top_srcdir)/bt/include          \
  -I$(top_srcdir)/bt-util-lib/include \
  -I$(srcdir)/include                 \
    $(MYSQL_CFLAGS)                   \
    $(MONGOC_CFLAGS)                  \
    $(JSON_C_CFLAGS)

clang-analyze: $(bt_ls_SOURCES:.c=.clang-analyze)
	@true

cppcheck: $(bt_ls_SOURCES:.c=.cppcheck)
	@true

.c.cppcheck:
	@cppcheck $(bt_ls_CFLAGS) $(CPPCHECK_OPTIONS) $(CPPCHECK_LIBRARIES) $<

.c.clang-analyze:
	@echo Analyzing $<
	@clang --analyze $(bt_ls_CFLAGS) $<
