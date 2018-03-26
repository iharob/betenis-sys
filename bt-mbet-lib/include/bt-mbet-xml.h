#ifndef __MBET_XML_H__
#define __MBET_XML_H__

#include <libxml/parser.h>
#include <libxml/xpath.h>

#include <stdbool.h>
#include <time.h>
char *bt_mbet_get_node_conent_string(xmlNode *node, const char *const xpath_expression);
int bt_mbet_get_node_content_integer(xmlNode *node, const char *const xpath_expression);
int bt_mbet_get_integer_property(xmlNode *node, const char *const name);
long int bt_mbet_get_long_property(xmlNode *node, const char *const name);
float bt_mbet_get_float_property(xmlNode *node, const char *const name);
char *bt_mbet_get_string_property(xmlNode *node, const char *const name);
void bt_mbet_get_date_property(xmlNode *node, const char *const name, struct tm *tm);
bool bt_mbet_get_boolean_property(xmlNode *node, const char *const name);
#if LIBXML_VERSION < 20901
void xmlXPathSetContextNode(xmlNodePtr node, xmlXPathContextPtr ctx);
#endif
#endif // __MBET_XML_H__
