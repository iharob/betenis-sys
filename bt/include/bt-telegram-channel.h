#ifndef __TELEGRAM_CHANNEL_H__
#define __TELEGRAM_CHANNEL_H__

/** @file
 */

#include <json.h>

#define MBET_URL "https://www.mbet.com/es/popular/Tennis/?menu=false"
#define LIVE "\xF0\x9F\x8E\xBE <b>LIVE</b> — "
#define RETIRED_LIST "<b>Retirados de hoy</b>\n<i>El tenis es muy duro</i>\n\n%s"
#define RETIRED_MESSAGE "<b>%s</b>\nJugando vs. <i>%s</i>\n%s%s\n\n"
#define DOG_MESSAGE "<b>%s</b>  <i>%.2f</i>\nLe ha ganado a %s %.2f\nResultado: %s\n<a href=\"%s\">%s</a>\n\n"
#define DOGS_TITLE "\xF0\x9F\x90\xB6 Tenistas <b>NO</b> favoritos que ganaron\n\n%s"
#define MEDICAL_TIMEOUT "\xF0\x9F\xA4\x95 <b>LIVE</b> — <b>%s</b>\n\nHa solicitado tratamiento m&#233;dico, jugando vs. <i>%s</i> en <b>%s</b>"
#define MEDICAL_TIMEOUT_NO_PLAYER "\xF0\x9F\xA4\x95 <b>LIVE</b> — Han solicitado tratamiento m&#233;dico %s vs %s en <b>%s</b>"
#define MEDICAL_TIMEOUT_MULTI "\xF0\x9F\xA4\x95 <b>LIVE</b> — <b>%s</b>\n\nHa solicitado tratamiento médico <b>%d minutos</b>, jugando vs. <i>%s</i> en <b>%s</b>"
#define MEDICAL_TIMEOUT_MULTI_NO_PLAYER "\xF0\x9F\xA4\x95 <b>LIVE</b> — Han solicitado tratamiento médico <b>%d minutos</b> %s vs %s en <b>%s</b>"
#define PLAYS_DOUBLES "\n\n<b>Nota</b>: jugar&#225; dobles"

int bt_telegram_send_message(const char *const channel, const char *const format, ...);
int bt_telegram_edit_message(int id, const char *const channel, const char *const format, ...);

#endif /* __TELEGRAM_CHANNEL_H__ */
