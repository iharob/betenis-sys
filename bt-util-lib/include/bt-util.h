#ifndef __BT_UTIL_H__
#define __BT_UTIL_H__

#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>

#include <mysql.h>
#include <json.h>

#include <inttypes.h>
#include <stdarg.h>

typedef struct bt_mysql_transaction bt_mysql_transaction;
typedef struct bt_mysql_operation bt_mysql_operation;
typedef struct bt_context bt_context;
typedef struct bt_event bt_event;
typedef struct bt_http bt_http;
typedef struct bt_http_url bt_http_url;
typedef struct bt_http_headers bt_http_headers;

/** @file
 */

#define BT_USER_AGENT "Mozilla/5.0 (X11; Linux x86_64; rv:43.0) Gecko/20100101 Firefox/43.0"
typedef struct bt_string_builder bt_string_builder;

/**
 * @brief The bt_tennis_category enum
 */
typedef enum bt_tennis_category {
    NoCategory = 0x000, /**< Ningun torneo habilitado (usado principalmente por MTOs) */
    DoublesMask = 0x01, /**< Mascara para dobles **/
    CategoryATP = 0x002, /**< Torneos Masculinos */
    CategoryChallenger = 0x004, /**< Torneos Masculinos Challenger */
    CategoryITF = 0x008, /**< Futures/Satélites */
    CategoryWTA = 0x010, /**< Torneos Femeninos */

    CategoryDoublesATP = CategoryATP | DoublesMask, /**< Dobles Masculinos */
    CategoryDoublesChallenger = CategoryChallenger | DoublesMask, /**< Dobles Challenger **/
    CategoryDoublesITF = CategoryITF | DoublesMask, /**< Dobles Futures/Satélites */
    CategoryDoublesWTA = CategoryWTA | DoublesMask, /**< Dobles Femeninos */
    AllCategories = CategoryATP | CategoryWTA /**< Todos los torneos (usado principalmente por MTOs) */
} bt_tennis_category;

/**
 * @brief The bt_tennis_subcategory enum
 */
enum bt_tennis_subcategory {
    Singles, /**< Sencillos */
    Doubles  /**< Dobles */
};

#define countof(list) sizeof list / sizeof *list

/* String */
/**
 * @brief Copiar una cadena quitando los espacios alrededor
 * @param string La cadena origen
 * @param length La longitud de la cadena origen, que es modificada por la
 * función para reflejar el posible cambio en longitud.
 * @return La nueva cadena, que es una copia y no tiene los espacios
 * en los bordes. Debe llamar `free()` para liberar memoria.
 */
char *bt_stripdup(const char *string, size_t *length);
/**
 * @brief Clon de la función POSIX `strdup()` pero usando el alojador de bt.
 * @param string La cadena para copiar
 * @return Una nueva cadena que es copia fiel de `string`, debe ser pasada a
 * `bt_free`
 */
char *bt_strdup(const char *const string);
/**
 * @brief Reemplazar múltiples ocurrencias de `needle` en `string`. Esta función
 * aloja memoria nueva siempre, liberando la que ha pasado el que la llama.
 * @param string Un apuntador al apuntador a la cadena origen/destino
 * @param needle Lo que se quiere reemplazar
 * @param replacement Por lo que lo vamos a reemplazar
 * @return Devuelve la nueva longitud de la cadena modificada.
 */
size_t bt_strreplace_all(char **string, const char *const needle, const char *const replacement);

/* String lists */
/**
 * @brief Liberar lista de cadenas
 * @param list La lista para liberar
 */
void bt_string_list_free(char **list);
/**
 * @brief Separar una cadena en sub-cadenas, en cada
 * ocurrencia del delimitador.
 * @param string La cadena que vamos a separar.
 * @param delimiter El delimitador que vamos a usar
 * @return Un apuntador, a una lista de apuntadores a los pedazos
 * resultado de separar la cadena `string`
 */
char **bt_util_string_splitstr(const char *const string, const char *const delimiter);
/**
 * @brief Lo mismo que `bt_string_splitstr` pero toma
 * un `char` individual
 * @param string La cadena que vamos a separar.
 * @param delimiter El delimitador que vamos a usar
 * @return Un apuntador, a una lista de apuntadores a los pedazos
 * resultado de separar la cadena `string`
 */
char **bt_string_splitchr(const char *const string, char delimiter);

/* MySQL Parameters */
/**
 * @brief Construir una cadena para generar un query
 * con `n` parámetros y `m` grupos de parámetros
 *
 * Algo así
 *
 *     (?,?,?,...,?),(?,?,?,...,?),(?,?,?,...,?),...,(?,?,?,...,?)
 *
 * donde habría `n` `?` por grupo, y cada grupo es uno de `(?,?,?,...,?)`.
 *
 * @param n Número de parámetros por "grupo"
 * @param m Número de grupos de parámetros
 * @return La cadena con los "placeholders" de los parámetros
 */
char *bt_mysql_parameters_sql(size_t n, size_t m);
/**
 * @brief Pegar parámetros de MySQL a un objeto `MYSQL_BIND`
 * @param bind El objetivo, donde se van a "pegar" los parámetros
 * @param index Posición dentro del el array `bind`
 * @param type El tipo del parámetro
 * @param value El valor del parámetro
 */
void bt_mysql_parameters_append(MYSQL_BIND *bind, size_t index, int type, void *value);
/* MySQL Transactions */
/**
 * @brief Ejecutar una transacción previamente almacenada
 * @param transaction El objeto con toda la información necesaria
 * ejecutar la transacción
 */
void bt_mysql_transaction_execute(bt_mysql_transaction *transaction);
/**
 * @brief Crear una nueva transacción con `n` operaciones
 * @param n El número de transacciones
 *
 * La lista variable de argumentos debe estar compuesta de el número de
 * parámetros que toma el query, seguido del código SQL del query mismo.
 *
 * @return Un objeto `bt_mysql_transaction` recién alojado que debe
 * ser pasado a `bt_mysql_transaction_free()`.
 */
bt_mysql_transaction *bt_mysql_transaction_new(size_t n, ...);
/**
 * @brief Liberar un objeto `bt_mysql_transaction`
 * @param transaction El objeto para liberar
 */
void bt_mysql_transaction_free(bt_mysql_transaction *transaction);
/* MySQL Operations */
/**
 * @brief Agregar un grupo de parámetros usando una cadena de formato como la
 * de la familia *`printf()` de funciones estándar.
 * @param operation Operación objetivo para agregar los parámetros
 * @param format La cadena de formato para procesar los parámetros. Pasar
 * tipos que no concuerdan con los del formato especificado, causará
 * comportamiento indefinido.
 */
void bt_mysql_operation_put(bt_mysql_operation *operation, const char *const format, ...) __attribute__((format(printf, 2, 3)));
/**
 * @brief Obtiene `n`-ésima operanción de la transacción
 * @param transaction Transacción de la cual se desea obtener la operación
 * @param n La posición de la operación (`0` .. `N - 1`)
 * @return
 */
bt_mysql_operation *bt_transaction_get_operation(bt_mysql_transaction *transaction, size_t n);

void bt_debug_mysql(const char *const file, const char *const function, int line, int status, MYSQL_STMT *stmt);

/**
 * @brief Ejecutar una sentencia MySQL sin llenar las molestias `MYSQL_BIND`
 * manualmente.
 * @param query La sentencia SQL
 * @param format Una cadena de formato que admite parámetros y resultados
 * separados por un `|`. La cadena es similar a la de la familia *`scanf()`
 * y para cada tipo de parámetro se espero un apuntador a un tipo equivalente.
 *
 * Los especificadores que van del lado izquierdo del separador, son
 * interpretados como los parámetros de la sentencia. Los de la derecha
 * serán los resultados.
 * @return
 */
MYSQL_STMT *bt_mysql_easy_query(const char *const query, const char *const format, ...);

/* String Builder */
/**
 * @brief Emula `sprintf()` pero copiando la cadena en una nueva
 * @param format La cadena de formato para especificar los tipos de las variables
 * pasadas posteriormente.
 * @return Una nueva cadena resultado de aplicar `sprintf()` luego de preparar
 * todo para hacerlo
 */
char *bt_strdup_printf(const char *const format, ...);

/* System Utilities */
/**
 * @brief Dormir con precisión de nanosecond
 * @param seconds Cantidad de segundos, puede ser tan puequeño como $1.0^{-9}$
 */
void bt_sleep(double seconds);
/**
 * @brief Cargar una sentencia SQL por nombre
 * @param name El nombre de la sentencia SQL
 *
 * La lista variable de parámetros consiste de pares de *llaves*, *valor*. Las
 * *llave*s serán sustituidos por sus correspondientes *valor*es
 * @return
 */
char *bt_load_query(const char *const name, ...) __attribute__((sentinel));
/**
 * @brief Realizar una solicitud GET a través de HTTP
 * @param url Url al que realizar la solicitud
 * @param tor Si usamos un el proxy Socks5 de
 * @param http Un objeto `bt_http` que sirve para realizar varias
 * solicitudes con la misma conexión
 * <a href="https://www.torproject.org/">Tor</a>
 * @return
 */
char *bt_http_get(const char *const url, bool tor, bt_http *http, const bt_http_headers *const headers);
/**
 * @brief Conectarse a un servidor HTTP
 * @param url El url del servidor
 * @param tor Si usar <a href="https://www.torproject.org/">Tor</a>
 * @param headers Cabeceras adicionales
 * @return
 */
bt_http *bt_http_connect(const char *const url, bool tor);
/**
 * @brief Cerrar una conexión http y liberar recursos
 * @param http La conexión http para cerrar
 */
void bt_http_disconnect(bt_http *http);

// Undocumented internal functions
void bt_sort_queries(void);
size_t bt_curl_data_function(char *data, size_t size, size_t nmemb, void *sb);

#define bt_notify_thread_end() log("%s/0x%08lx: exiting\n", __FUNCTION__, pthread_self());

#endif // __BT_UTIL_H__
