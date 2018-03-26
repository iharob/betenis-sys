#ifndef __bt_william_hill_TOPICS_H__
#define __bt_william_hill_TOPICS_H__

/** @file
 */

#include <stdlib.h>
#include <mysql.h>

#include <http-websockets.h>

typedef struct bt_websocket_connection bt_websocket_connection;
enum bt_player_idx {
    Home = 0,
    Away = 1
};

/**
 * @brief Tipos de "tópicos" observables
 */
enum bt_topic_type
{
    PhaseTopic /**< Fase de un partido */
  , TotalSetsTopic /**< Número total de sets */
  , CurrentSetTopic /**< Set actual */
  , TeamANameTopic /**< Nombre del equipo A */
  , TeamBNameTopic /**< Nombre del equipo B */
  , TeamServingTopic /**< Equipo con servicio */
  , IncidentsTopic /**< Incidentes (aquí es donde aparece MTO) */
  , AnimationTopic /**< Animaciones (cada cosa que ocurre en el partido) */
  , PreviousSet1GamesWonA /**< Puntos ganados por el equipo A en el primer set */
  , PreviousSet2GamesWonA /**< Puntos ganados por el equipo A en el segundo set */
  , PreviousSet3GamesWonA /**< Puntos ganados por el equipo A en el tercer set */
  , PreviousSet4GamesWonA /**< Puntos ganados por el equipo A en el cuarto set */
  , PreviousSet5GamesWonA /**< Puntos ganados por el equipo A en el quinto set */
  , PreviousSet1TiebreakPointsWonA /**< Puntos tie break primer set equipo A */
  , PreviousSet2TiebreakPointsWonA /**< Puntos tie break segundo set equipo A */
  , PreviousSet3TiebreakPointsWonA /**< Puntos tie break tercer set equipo A */
  , PreviousSet4TiebreakPointsWonA /**< Puntos tie break cuarto set equipo A */
  , PreviousSet5TiebreakPointsWonA /**< Puntos tie break quinto set equipo A */
  , PreviousSet1GamesWonB /**< Puntos ganados por el equipo B en el primer set */
  , PreviousSet2GamesWonB /**< Puntos ganados por el equipo B en el segundo set */
  , PreviousSet3GamesWonB /**< Puntos ganados por el equipo B en el tercer set */
  , PreviousSet4GamesWonB /**< Puntos ganados por el equipo B en el cuarto set */
  , PreviousSet5GamesWonB /**< Puntos ganados por el equipo B en el quinto set */
  , PreviousSet1TiebreakPointsWonB /**< Puntos tie break primer set equipo B */
  , PreviousSet2TiebreakPointsWonB /**< Puntos tie break segundo set equipo B */
  , PreviousSet3TiebreakPointsWonB /**< Puntos tie break tercer set equipo B */
  , PreviousSet4TiebreakPointsWonB /**< Puntos tie break cuarto set equipo B */
  , PreviousSet5TiebreakPointsWonB /**< Puntos tie break quinto set equipo B */
  , CurrentSetGamesWonA /**< Juegos ganados en set actual equipo A */
  , CurrentSetGamesWonB /**< Juegos ganados en set actual equipo B */
  , CurrentGamePointsWonA /**< Puntos ganados en juego actual equipo A */
  , CurrentGamePointsWonB /**< Puntos ganados en juego actual equipo B */
  , TopicCount /**< Contador de "tópicos" */
  , InvalidTopic = -1 /**< Sirve para marcar como inválido un valor */
};


typedef struct bt_context bt_context;
typedef struct bt_topic_list bt_topic_list;
typedef struct bt_event bt_event;
typedef struct bt_topic bt_topic;
typedef struct bt_topic_descriptor bt_topic_descriptor;

/**
 * @brief Crea un nuevo objeto `bt_topic`
 * @param alias El alias con el cual se identifcará cuando el WebSocket envía
 * mensajes para este `bt_topic`
 * @param event El evento asociado. Para un evento existen varios posibles
 * `bt_topic`s, pero este campo es simplemente una forma rápida de acceder
 * al objeto evento asociado a este `bt_topic`
 * @param type El tipo del evento `enum bt_topic_type`
 * @return Un objeto `bt_topic` recién alojado, este debe ser pasado a
 * `bt_william_hill_topic_free()`.
 */
bt_topic *bt_william_hill_topic_new(char *alias, bt_event *event, enum bt_topic_type type);
/**
 * @brief Libera un objeto `bt_topic`
 * @param topic El objeto que se va a liberar
 */
void bt_william_hill_topic_free(bt_topic *const topic);
/**
 * @brief Obtener el evento asociado al `bt_topic`
 * @param topic El objeto del cual obtener el evento
 * @return
 */
bt_event *bt_william_hill_topic_get_event(const bt_topic *const topic);
/**
 * @brief Obtener el tipo de `bt_topic`
 * @param topic El objeto del cual obtener el tipo
 * @return
 */
enum bt_topic_type bt_william_hill_topic_get_type(const bt_topic *const topic);
/**
 * @brief Obtener el alias de `bt_topic`
 * @param topic El objeto del cual obtener el alias
 * @return
 */
const char *bt_william_hill_topic_get_alias(const bt_topic *const topic);
/**
 * @brief Reestablecer el alias del `bt_topic`. Cuando se reinicia la conexión
 * no se liberan inmediatamente los eventos para no tener que
 *
 *   1. Esperar a que se vuelvan a generar por el proveedor, cosa que tiene un
 *      margen de 1 minuto de posble espera y en el transcurso de ese tiempo
 *      podría ocurrir algo relevante. Si bien, de todas maneras se capturará
 *      la idea básica es que eso ocurra en "tiempo real".
 *
 *   2. No alojar memoria nuevamente para los topics de eventos que aún están
 *      corriendo.
 *
 * Esta función toma posesión de la memoria de `alias`, de manera que si
 * esa memoria es liberada después del llamado a esta función el comportamiento
 * es indefinido
 *
 * @param topic El topic cuyo alias se reiniciará
 * @param alias El nuevo alias
 */
void bt_william_hill_topic_reset_alias(bt_topic *topic, char *alias);
/**
 * @brief Crear una nueva lista de `bt_topic`s
 * @return La nueva lista que deberá pasar a `bt_william_hill_topic_list_free()`
 */
bt_topic_list *bt_william_hill_topics_list_create(void);
/**
 * @brief Obtener el número de items en la lista de `bt_topic`s
 * @param list La lista objetivo
 * @return El número de items de la lista
 */
size_t bt_william_hill_topic_list_get_count(const bt_topic_list *const list);
/**
 * @brief Buscar on objeto `bt_topic` cuyo alias es `alias`
 * @param list La lista en la que se desea buscar
 * @param alias El alias que debe tener el objeto buscado
 * @return
 */
const bt_topic *bt_william_hill_topic_list_find(const bt_topic_list *const list, const char *const alias);
/**
 * @brief Obtener el n-ésimo item de la lista de `bt_topic`s
 * @param list La lista objetivo de la que se desea obtener el `bt_topic`
 * @param n La posición del elemento que se desea obtener
 * @return
 */
bt_topic *bt_william_hill_topic_list_get_item(const bt_topic_list *const list, size_t n);
/**
 * @brief Anexar en item a la lista
 * @param list La lista objetivo
 * @param topic El item que se desea anexar
 * @return
 */
bool bt_william_hill_topic_list_append(bt_topic_list *list, bt_topic *topic);
/**
 * @brief Ordenar la lista en orden alfabético por `alias`
 * @param list La lista objetivo
 */
void bt_william_hill_topic_list_sort(bt_topic_list *list);
/**
 * @brief Quitar los items de la lista, para el evento `event`. Esta función
 * libera la memoria asociada al `bt_topic` pero no toca el `event`. La razón
 * es que el `event` está presente en esta estructura para este tipo de cosas
 * pero no está bajo el control de esta estructura.
 * @param list La lista objetivo
 * @param event El evento cuyos `bt_topic`s deseamos quitar
 */
void bt_william_hill_topic_list_remove(bt_topic_list *const list, const bt_event *const event);
/**
 * @brief Liberar los recursos utilizados por una lista de `bt_topic`s
 * @param list La lista cuyos recursos se desea liberar
 */
void bt_william_hill_topic_list_free(bt_topic_list *const list);
/**
 * @brief Obtener el tipo de topic, a partir del nombre del mismo. Básicamente
 * permite asociar el topic al evento pero también es útil si se desea saber
 * qué tipo de topic es el que tiene este nombre en general
 * @param name El nombre del topic cuyo tipo se desea determinar
 * @return El tipo de topic o `InvalidTopic` si no hay coincidencias
 */
enum bt_topic_type bt_william_hill_topic_get_type_from_description_name(const char *const name);
/**
 * @brief Suscribir un evento con todos los "topics" habilitados al WebSocket.
 * Después de hacer esto, a través del WebSocket llegará toda la información
 * requerida en esta operación, justo cuando el servidor lo decida.
 * @param event El evento para suscribir
 * @param websocket La conexión de websocket a la cual suscribir el evento
 * al WebSocket qué hacer.
 * @return Si tuvo o no éxito la operación
 */
bool bt_william_hill_topics_subscribe_event(const bt_event *event, const bt_websocket_connection *const ws);
/**
 * @brief bt_william_hill_topic_subscribe_previous_sets
 * @param event
 * @param set
 */
void bt_william_hill_topic_subscribe_incidents(struct httpio *const ws, bt_event * const event);
void bt_william_hill_topic_subscribe_current_set_gameswon(struct httpio *const ws, const bt_event *const event, enum bt_player_idx idx);
void bt_william_hill_topic_subscribe_previous_set(struct httpio *const ws, const bt_event *const event, int set, enum bt_player_idx idx);
#endif /* __bt_william_hill_TOPICS_H__ */
