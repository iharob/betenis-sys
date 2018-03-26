#ifndef __ONCOURT_DATABASE_H__
#define __ONCOURT_DATABASE_H__

#include <mysql.h>
/** @file
 */
/**
 * @brief Observar cambios en versión de base de datos <a href="www.oncourt.org">oncourt</a>
 * @param context El contexto de ejecución, el único objetivo es estar
 * informados de si el programa sigue vivo
 * @return
 */
void *bt_watch_oncourt_database(void *context);
/**
 * @brief Actualizar la base de datos usando el archivo de comandos
 * de <a href="www.oncourt.org">oncourt</a>
 * @param data El texto que es interpretado por este módulo y convertido
 * en sentencias SQL
 * @return
 */
int bt_oncourt_database_parse_update(const char *const data);
#endif /* __ONCOURT_DATABASE_H__ */
