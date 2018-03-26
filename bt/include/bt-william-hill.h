#ifndef __bt_william_hill_H__
#define __bt_william_hill_H__

/** @file
 */

#include <stdlib.h>
#include <stdbool.h>

#include <bt-william-hill-topics.h>
#include <bt-util.h>

typedef struct bt_event_list bt_event_list;
typedef struct bt_event bt_event;
typedef struct bt_websocket_connection bt_websocket_connection;
typedef struct bt_player bt_player;
/**
 * @brief Conectarse al websocket
 * @return Un objeto `httpio` que permite leer y escribir en
 * el socket directamente.
 */
struct httpio *bt_william_hill_websocket_connect(void);
/**
 * @brief Realizar el saludo inicial para establecer la conexión al
 * WebSocket de forma segura <a href="https://tools.ietf.org/html/rfc6455">RFC 6455</a>
 * @param websocket La conexión de websocket para realizar el saludo
 * @return Si el saludo fue un éxito o no
 */
bool bt_william_hill_websocket_handshake(struct httpio *websocket);
/**
 * @brief Manejar un mensaje en el formato entendido por los WebSockets
 * según <a href="https://tools.ietf.org/html/rfc6455">RFC 6455</a>
 * @param websocket La conexión de websocket
 * @param context Un objeto con toda la información del contexto de
 * ejecución
 * @return `0` cuando ha habido éxito y `-1` en caso de error
 */
int bt_william_hill_handle_websocket_frame(struct httpio *websocket, bt_context *const context);
/**
 * @brief Envolvente de la función para suscribir los eventos al WebSocket de
 * forma segura en cuanto a multi hilos.
 * @param websocket El objeto al cual suscribirse
 * @param context Un objeto con toda la información del contexto de
 * ejecución
 */
int bt_william_hill_subscribe_events(const bt_websocket_connection *const websocket, bt_context *const context);
/**
 * @brief bt_william_hill_score_to_str Generar una cadena con el resultado del partido
 * @param event El evento en cuestión
 * @param home El jugador de casa
 * @param away El jugador visitante
 * @return Una cadena alojada con `bt_malloc` que debe ser desalojada con `bt_free`.
 */
char *bt_william_hill_score_to_str(const bt_event *const event, bt_player *const victim, bt_player *const oponent);

bool bt_william_hill_use_tor(void);
#endif /* __bt_william_hill_H__ */
