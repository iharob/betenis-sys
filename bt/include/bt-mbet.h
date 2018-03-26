#ifndef __MBET_MARKET_DROPS_H__
#define __MBET_MARKET_DROPS_H__
/** @file
 */

#include <bt-util.h>
typedef struct bt_mbet_feed bt_mbet_feed;
/**
 * @brief bt_mbet_feed_prematch Función principal para el hilo del feed PRE
 * de mbet
 * @param context Contexto de ejecución
 * @return
 */
void *bt_mbet_feed_prematch(void *context);
/**
 * @brief bt_mbet_feed_live Función principal para el hilo del feed LIVE
 * de mbet
 * @param context Contexto de ejecución
 * @return
 */
void *bt_mbet_feed_live(void *context);
#endif // __MBET_MARKET_DROPS_H__
