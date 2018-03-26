#ifndef __BETENIS_CONTEXT_H__
#define __BETENIS_CONTEXT_H__

/** @file
 */
#include <stdbool.h>
#include <pthread.h>

typedef struct bt_context bt_context;
typedef struct bt_event bt_event;
typedef struct bt_topic bt_topic;
typedef struct bt_event_list bt_event_list;
typedef struct bt_topic_list bt_topic_list;
struct httpio;

/**
 * @brief Encontrar un evento con id `match`
 * @param context El contexto de ejecución
 * @param match El partido para ser encontrado
 * @return Un objetoc `bt_event` que tiene como `id` `match`
 */
bt_event *bt_find_bt_william_hill_event(const bt_context *const context, int match);
/**
 * @brief Crear el contexto de ejecución accesible desde varios hilos a la vez
 * @return Un objeto `bt_context` recién alojado que debe ser liberado con
 * `bt_context_free()`
 */
bt_context *bt_create_context(void);
/**
 * @brief Liberar un contexto de ejecución
 * @param context El contexto para liberar
 */
void bt_context_free(bt_context *const context);
/**
 * @brief Transferir los eventos del proveedor al consumidor de eventos
 * de William Hill
 * @param context El contexto de ejecución
 * @param events Los eventos para transferir
 */
void bt_transfer_new_bt_william_hill_events(bt_context *const context, bt_event_list *const events);
/**
 * @brief Obtener los eventos en la lista interna del contexto de ejecución
 * @param context El contexto de ejecución
 * @return Una lista de los eventos actualmente alamacenados en el contexto
 */
bt_event_list *bt_context_get_events(const bt_context *const context);
/**
 * @brief Encontrar un topic en el contexto de ejecución
 * @param context El contexto de ejecución
 * @param alias El alias del topic
 * @return El topic si es encontrado en la lista
 */
const bt_topic *bt_william_hill_find_topic(const bt_context *const context, const char *const alias);
/**
 * @brief Anexar un topic a la lista del contexto
 * @param context El contexto de ejecución
 * @param topic El topic para agregar
 * @return Si se ha logrado anexar el item
 */
bool bt_context_append_topic(bt_context *const context, bt_topic *topic);
/**
 * @brief Eliminar un topic del contexto de ejecución
 * @param context El contexto de ejecución
 * @param topic El topic a ser eliminado
 */
void bt_context_remove_bt_william_hill_topic(bt_context *const context, const bt_topic *const topic);
/**
 * @brief Detener el programa, esta función termina el programa inmediatamente
 * @param context El contexto de ejecución
 */
void bt_context_stop(bt_context *const context);
/**
 * @brief Marcar los eventos como no-suscritos al websocket
 * @param context El contexto de ejecución
 */
void bt_unsubscribe_events(bt_context *const context);
/**
 * @brief Buscar un topic para un evento particular y de un tipo determinado
 * @param context El contexto de ejecución
 * @param match El id del evento
 * @param type El tipo de topic
 * @return
 */
bt_topic *bt_find_topic_for_event(const bt_context *const context, int match, int type);
/**
 * @brief Ordenar los topics por alias alfabéticamente
 * @param context El contexto de ejecución
 */
void bt_context_sort_topics(bt_context *const context);
/**
 * @brief Verificar que el programa no ha sido terminado
 * @param context El contexto de ejecución
 * @return Si el programa sigue corriendo y no ha terminado
 */
bool bt_isrunning(const bt_context *const context);

void bt_global_lock();
void bt_global_unlock();

#endif /* __BETENIS_CONTEXT_H__ */
