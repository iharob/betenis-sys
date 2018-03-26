#ifndef __MBET_SCORE_H__
#define __MBET_SCORE_H__
typedef struct bt_mbet_score bt_mbet_score;
typedef struct _xmlNode xmlNode;
/** @file
 */
/**
 * @brief Analizar y extraer una cadena con el resultado de un partido
 * a una estructura `bt_mbet_live_result`
 * @param node El node XML que contiene el resultado en forma de texto
 * @return
 */
bt_mbet_score *bt_score_parse_oncourt(const char * const);
bt_mbet_score *bt_score_parse_mbet(xmlNode *node);
void bt_mbet_score_free(bt_mbet_score *);
#endif // __MBET_SCORE_H__
