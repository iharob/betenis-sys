#ifndef __BETENIS_DATABASE_H__
#define __BETENIS_DATABASE_H__

/** @file
 */

#include <stdint.h>
#include <mysql.h>
#include <stdbool.h>
#include <json.h>

typedef struct bt_mysql_transaction bt_mysql_transaction;
typedef struct bt_player bt_player;
typedef enum bt_tennis_category bt_tennis_category;
typedef struct bt_event bt_event;
/**
 * @brief Ejecutar una transacción MySQL interna
 * @param transaction Objeto de transacción de MySQL que admite
 * varias operaciones
 */
void bt_database_transaction_run(bt_mysql_transaction *transaction);
/**
 * @brief Crear conexión a la base de datos
 */
void bt_database_initialize(void);
/**
 * @brief Liberar memoria usada por la conexión a la base de datos
 */
void bt_database_finalize(void);
/**
 * @brief Obtener la versión local de la base de datos de <a href="www.oncourt.org">oncourt</a>
 * @return La versión de la base de datos
 */
int bt_database_oncourt_local_version(void);
/**
 * @brief Ejecutar un query sencillo, sin parametros
 * @param query The SQL query to execute
 * @return 0 si fue ejecutada correctamente, -1 de lo contrario
 */
int bt_mysql_execute_query(const char *const query);
/**
 * @brief Crear una sentencia preparada MySQL
 * @return Un apuntador a un objeto `MYSQL_STMT`
 */
MYSQL_STMT *bt_database_new_stmt(void);
/**
 * @brief Verificar sel el jugador con id `id` jugará dobles pronto
 * @param category Categoría de tenis del jugador
 * @param id El id del jugador según base de datos <a href="www.oncourt.org">oncourt</a>
 * @return Si el jugador jugará dobles
 */
bool bt_next_doubles_for_player(const char *const category, int id);
/**
 * @brief Obtener el id de <a href="www.oncourt.org">oncourt</a> de un jugador
 * cuyo nombre según la web <a href="sports.williamhill.com">William Hill</a>
 * es `name` y pertence a la categoría `category`. Si el valor de `category` es
 * distinto de `InvalidCategory` entonces la función intentará adivinarla y la
 * almacenará en dicha variable
 * @param name El nombre del jugador
 * @param[out] category La categoría que es una de `bt_tennis_category`
 * @return El id del jugador si es encontrado en la base de datos
 */
int bt_get_player_from_bt_william_hill(bt_tennis_category *category, const char *const name);
/**
 * @brief Obtener el id de <a href="www.oncourt.org">oncourt</a> de un jugador
 * cuyo nombre según la web de <a href="sports.williamhill.com">William Hill</a>
 * es `name` y pertence a la categoría `category` que es una representación
 * textual en lugar de un enumerador
 * @param name El nombre del jugador
 * @param category La representación textual de la categoría, `"atp"` o `"wta"`
 * @return El id del jugador si es encontrado en la base de datos
 */
int bt_get_player_from_bt_william_hill_sg(const char *const category, const char *const name);
/**
 * @brief Obtener el id del torneo de <a href="www.oncourt.org">oncourt</a>
 * dados los jugadores con ids `id1` e `id2`. El orden de `id1` e `id2` es
 * irrelevante.
 * @param category La categorí del torneo
 * @param id1 El id de uno de los jugadores
 * @param id2 El id de uno de los jugadores
 * @param[out] tour El id del torneo
 * @param[out] round El id de la ronda actual del tornel
 * @param[out] rank El rank o clasificación del torneo
 * @return
 */
int bt_get_tournament_id_from_players(bt_tennis_category category, int id1, int id2, int *tour, int *round, int *rank);
/**
 * @brief Comprobar que un valor de MySQL es `NULL`
 * @param value El valor de MySQL que se quiere comprobar
 * @return Si es o no `NULL` el valor
 */
bool bt_mysql_is_null(MYSQL_BIND *value);
/**
 * @brief Almacenar el `id` del mensaje de telegram del MTO
 * @param message El `id` del mensaje
 * @param match El partido en cuestión
 * @param channel El canal de telegram
 * @return `0` si hay éxito y `1` en caso contrario
 */
int bt_database_save_mto_message_id(int message, int match, const char *const channel);
/**
 * @brief Obtener el número de veces que sale el MTO, equivale al número de
 * minutos que el jugador a pedido MTO
 * @param match El partido en cuestión
 * @param player El jugador que pide MTO
 * @return El número de veces que se ha pedido MTO
 */
int bt_database_count_mto(int match, const char *const player);
/**
 * @brief Obtener el `id` del mensaje de telegram de un MTO
 * @param match El partido en cuestión
 * @param channel El canal de telegram
 * @return El `id` del mensaje de telegram, sirve para editar
 * el mensaje y actualizar el estatus.
 */
int bt_database_mto_message_id(int match, const char *const channel);
/**
 * @brief Iniciar transacción MySQL
 */
void bt_database_begin(void);
/**
 * @brief Deshacer transacción MySQL
 */
void bt_database_rollback(void);
/**
 * @brief Aceptar transacción MySQL
 */
void bt_database_commit(void);
/**
 * @brief Obtener el nombre de la categoría
 * @param category Id de la categoría
 * @return Cadena read-only con el nombre de la categoría
 */
const char *bt_get_category_name(bt_tennis_category category);

bool bt_database_get_tournament_name(bt_tennis_category category, int id, char **out_name, char **out_flag, char **out_court);
#endif // __BETENIS_DATABASE_H__

