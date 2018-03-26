#ifndef __bt_william_hill_EVENTS_H__
#define __bt_william_hill_EVENTS_H__

/** @file
 */

#include <bt-william-hill.h>

#include <json.h>
#include <mysql.h>

struct httpio;
typedef struct bt_context bt_context;

typedef void (*bt_event_list_applier)(size_t, bt_event *,void *);
/**
 * @brief Liberar un objeto evento
 * @param event El objeto para liberar
 */
void bt_william_hill_event_free(bt_event *event);
/**
 * @brief Alojar espacio para una nueva lista de eventos
 * @return La lista recién alojada, debe ser pasada a
 * `bt_william_hill_event_list_free()`
 */
bt_event_list *bt_william_hill_event_list_new();
/**
 * @brief Obtener la lista de eventos disponibles desde
 * la web de oncourt
 * @return Una lista de eventos disponibles para ser observados
 */
bt_event_list *bt_william_hill_events_list_fetch();
/**
 * @brief Encontrar un evento dado el ID interno de William Hill.
 * @param list La lista de eventos en la cual se encuentra el posible
 * evento que queremos buscar
 * @param id El id del evento
 * @return
 */
bt_event *bt_william_hill_event_list_find(bt_event_list *list, int id);
/**
 * @brief Anexar un evento a la lista `list`
 * @param list La lista en la que vamos a anexar el evento
 * @param event El evento que se quiere anexar, previamente inicializado
 */
void bt_william_hill_event_list_append(bt_event_list *list, bt_event *event);
/**
 * @brief Liberar una lista de eventos
 * @param list La lista para liberar
 */
void bt_william_hill_event_list_free(bt_event_list *list);
/**
 * @brief Limpia la lista de eventos pero no libera la lista misma, sirve
 * para volver a usar la lista y anexarle nuevos eventos, sin necesidad de
 * volver a crearla.
 * @param list La lista que queremos limpiar
 */
void bt_william_hill_event_list_clear(bt_event_list *list);
/**
 * @brief Obtiene un evento de la lista, y lo saca de la misma quedando la
 * memoria asociada bajo la responsabilidad del que recibe el evento
 * @param list Lista de la cual extraer el evento
 * @param index Poisición del evento en la lista `[0..N-1]`
 * @return El evento que ha sido retirado por completo de la lista `list`
 */
bt_event *bt_william_hill_event_list_take(bt_event_list *list, int index);
/**
 * @brief Quitar un evento de una lista y liberar la memoria que usa
 * @param list La lista de la que se quiere, retirar el evento
 * @param event El evento que queremos retirar
 */
void bt_william_hill_event_list_remove(bt_event_list *const list, const bt_event *const event);
/**
 * @brief Mezcla dos listas de eventos, colocando los eventos de `source` que
 * no están presentes en `target`, dentro de la lista `target`
 * @param target La lista objetivo que tendrá los eventos de ambas listas,
 * salvando duplicados
 * @param source La lista origen desde la que se extraen todos los eventos
 * que no estén presentes en `target`
 */
void bt_william_hill_event_list_merge(bt_event_list *target, bt_event_list *source);
/**
 * @brief Ordenar los eventos por `id`, esta función tiene una importancia
 * más bien interna, y tal vez debería ser retirada de la API.
 * @param list La lista que se desea ordenar.
 */
void bt_william_hill_event_list_sort(bt_event_list *list);
/**
 * @brief Obtener el número de eventso en la lista
 * @param list Lista de interés
 * @return Número de eventso en la lista
 */
size_t bt_william_hill_event_list_get_count(const bt_event_list *const list);
/**
 * @brief Obtener un elemento de la lista
 * @param list Lista de interés
 * @param n Posición del elemento
 * @return El elemento de la lista
 */
bt_event *bt_william_hill_event_list_get_item(const bt_event_list *const list, size_t n);
/**
 * @brief Obtener la categoría de un evento
 * @param event El evento
 * @return La categoria del evento una de `bt_tennis_category`
 */
bt_tennis_category bt_william_hill_event_get_category(const bt_event *const event);
/**
 * @brief Obtener el n-ésimo jugador en el evento (sólo habrán 2)
 * @param event El evento de interés
 * @param n La posición del jugador `0` o `1`
 * @return
 */
bt_player *bt_william_hill_event_get_player(const bt_event *const event, size_t n);
/**
 * @brief Obtener el id del evento
 * @param event El evento de interés
 * @return El id del evento según la web de William Hill
 */
int bt_william_hill_event_get_id(const bt_event *const event);
/**
 * @brief Obtener el id del torneo del evento
 * @param event Evento de interés
 * @return El id del torneo según la base de datos de oncourt
 */
const char *bt_william_hill_event_get_tour(const bt_event *const event);
/**
 * @brief Marcar todos los eventos en la lista como NO suscritos al WebSocket
 * de William Hill
 * @param list Lista de eventos para marcar
 */
void bt_william_hill_event_list_unsubscribe_all(bt_event_list *list);
/**
 * @brief Suscribir todos los eventos en el WebSocket de William Hill
 * para escuchar y monitorear la actividad
 * @param websocket La conexión al WebSocket
 * @param list La lista de eventos para registrar
 */
int bt_william_hill_event_list_subscribe_all(const bt_websocket_connection *const websocket, bt_event_list *list);
/**
 * @brief bt_william_hill_event_swap_players Alternar los jugadores para coincidir
 * con el orden de los otros topics
 * @param event El evento de interés
 */
void bt_william_hill_event_swap_players(bt_event *const event);
/**
 * @brief bt_william_hill_event_set_current_set Actualizar el número de set en el
 * evento
 * @param event El evento de interés
 * @param set El número de set
 */
void bt_william_hill_event_set_current_set(bt_event *const event, int set);
/**
 * @brief bt_william_fill_event_current_set Obtener el número de set actual
 * @param event El evento de interés
 * @return El número de set actual
 */
int bt_william_hill_event_get_current_set(const bt_event *const event);

bool bt_william_hill_event_is_ready_for_incidents(bt_event *const event);
const char *bt_william_hill_event_get_date(const bt_event *const event);
#endif /* __bt_william_hill_EVENTS_H__ */
