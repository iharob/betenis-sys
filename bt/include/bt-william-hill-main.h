#ifndef __bt_william_hill_MAIN_H__
#define __bt_william_hill_MAIN_H__

/** @file
 */

/**
 * @brief Proveedor de eventos de William Hill.
 *
 * Esta función revisa periódicamente el web site de William Hill y
 * suministra los "eventos" en curso al WebSocket para poder observar
 * todo lo que ocurre en dichos eventos y capturar los MTOs
 * @param data Este parámetro es de tipo `void *` porque esta función
 * es la función de inicio de un hilo `pthread`. Y se usa para pasar
 * una estructura con información que se comparte entre el proveedor
 * y el WebSocket.
 * @return El valor de devolución es irrelvante, y siempre devuelve `NULL`
 */
void *bt_william_hill_events_provider(void *data);
/**
 * @brief Esta función se conecta al WebSocket, recoje los eventos
 * dejados por `bt_william_hill_events_provider()` y envía los
 * incidentes relevantes a otras funciones que notifican y almacenan
 * la información.
 * @param data Este parámetro es de tipo `void *` porque esta función
 * es la función de inicio de un hilo `pthread`. Y se usa para pasar
 * una estructura con información que se comparte entre el proveedor
 * y el WebSocket.
 * @return El valor de devolución es irrelvante, y siempre devuelve `NULL`
 */
void *bt_william_hill_events_listener(void *data);

#endif // __bt_william_hill_MAIN_H__
