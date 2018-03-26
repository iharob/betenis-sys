#ifndef __BT_STRING_BUILDER_H__
#define __BT_STRING_BUILDER_H__

/** @file
 */

typedef struct bt_string_builder bt_string_builder;
/**
 * @brief Crear un constructor de cadenas nuevo
 * @return Un apuntador a una estructura para construir cadenas eficientemente
 */
bt_string_builder *bt_string_builder_new(void);
/**
 * @brief Libera un constructor de cadenas
 * @param builder El constructor de cadenas para liberar
 */
void bt_string_builder_free(bt_string_builder *builder);
/**
 * @brief Extrae la cadena y toma posesión de la misma
 * @param builder El constructor de cadenas objetivo
 * @return Devuelve el apuntador original a la cadena construida, evita que
 * el constructor pueda liberar esta memoria. Esto permite obtener la cadena
 * sin copiarla.
 */
char *bt_string_builder_take_string(bt_string_builder *builder);
/**
 * @brief Agregar un pedazo a la cadena construída por el constructor
 * @param builder El constructor de cadenas objetivo
 * @param chunk El pedazo que queremos agregar
 * @param length La longitud del pedazo para agregar
 */
void bt_string_builder_append(bt_string_builder *builder, const char *const chunk, size_t length);
/**
 * @brief Agregar una cadena a la cadena construída por el constructor
 * @param builder El constructor de cadena objetivo
 * @param string La cadena que queremos agregar
 */
void bt_string_builder_append_string(bt_string_builder *builder, const char *const string);
/**
 * @brief Usar el formato de la famila *`printf()` para agregar contendio a la
 * cadena en construcción.
 * @param builder El constructor de cadenas objetivo
 * @param format La cadena de formato, idéntica a la que usa `sprintf()`
 * @param list La lista de argumentos
 */
void bt_string_builder_vprintf(bt_string_builder *builder, const char *const format, va_list list);
/**
 * @brief Usar el formato de la famila *`printf()` para agregar contendio a la
 * cadena en construcción.
 * @param builder El constructor de cadenas objetivo
 * @param format La cadena de formato, idéntica a la que usa `sprintf()`
 *
 * La lista variable de argumentos, debe satisfacer las mismas condiciones que
 * para `sprintf()`
 */
void bt_string_builder_printf(bt_string_builder *builder, const char *const format, ...) __attribute__((format(printf,2,3)));
/**
 * @brief Obtener una referencia constante a la cadena construida
 * @param builder El constructor de cadenas objetivo
 * @return Una referencia constante a la cadena. En principio el usuario puede
 * modificar esta cadena, pero recomiendo usar `bt_string_builder_take_string()`
 * para evitar tragedias.
 */
const char *bt_string_builder_string(const bt_string_builder *const builder);
/**
 * @brief bt_string_builder_reset Reiniciar el constructor de cadena
 * @param builder Apuntador al objeto que se reiniciará
 */
void bt_string_builder_reset(bt_string_builder *builder);
#endif // __BT_STRING_BUILDER_H__
