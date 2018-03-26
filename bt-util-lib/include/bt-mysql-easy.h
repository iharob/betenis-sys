#ifndef __BT_MYSQL_HELPERS_H__
#define __BT_MYSQL_HELPERS_H__

#include <stdarg.h>
#include <mysql.h>

MYSQL_STMT *htisql_mysql_easy_query(MYSQL *mysql, const char *const query, const char *const format, ...);
MYSQL_STMT *htisql_mysql_easy_vquery(MYSQL *mysql, const char *const query, const char *const format, va_list args);

#endif // __BT_MYSQL_HELPERS_H__
