#ifndef __BT_TIMER_H__
#define __BT_TIMER_H__

#include <signal.h>
#include <time.h>

/** @file
 */

/** Función de notificación para un temporizador */
typedef void (*bt_timer_notifier)(void);

/**
 * @brief Establecer un reloj alarma, para realizar una acción. El temporizador
 * dispara una vez al día a la hora especificada en los parámetros.
 * @param when El tiempo, es una cadena que representa la hora a la que se
 * quiere iniciar el temporizador.
 * @param notifier La acción que se desea ejectutar. Es una función con la
 * signatura de `bt_timer_notifier`
 * @return Devulve un objeto timer que debe ser liberado usando `timer_delete()`
 */
timer_t bt_setup_timer(const char *const when, bt_timer_notifier notifier);
#endif // __BT_TIMER_H__

