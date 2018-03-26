#include <string.h>
#include <libxml/xpath.h>
#include <bt-mbet-xml.h>

char *
bt_mbet_get_node_conent_string(xmlNode *node, const char *const xpath_expression)
{
    xmlNodeSet *nodes;
    xmlXPathContext *context;
    xmlXPathObject *xpath;
    char *result;
    // Just in case
    result = NULL;
    if (node == NULL)
        return NULL;
    // Make a new context for the xpath query
    context = xmlXPathNewContext(node->doc);
    if (context == NULL)
        return NULL;
    // Set the root node
    xmlXPathSetContextNode(node, context);
    // Execute the xpath query
    xpath = xmlXPathEvalExpression((const xmlChar *) xpath_expression, context);
    if (xpath == NULL)
        goto error;
    // Make a pointer to the set of matching nodes
    nodes = xpath->nodesetval;
    if (nodes->nodeNr == 1) {
        // If exactly one node matched, we got it
        result = (char *) xmlNodeGetContent(nodes->nodeTab[0]);
    }
    // Release resources
    xmlXPathFreeObject(xpath);
error:
    xmlXPathFreeContext(context);
    return result;
}

int
bt_mbet_get_node_content_integer(xmlNode *node, const char *const xpath_expression)
{
    char *endptr;
    char *string;
    int value;
    // First make a string
    string = bt_mbet_get_node_conent_string(node, xpath_expression);
    if (string == NULL)
        return -1;
    // And now convert it to a number
    value = (int) strtol(string, &endptr, 10);
    if (*endptr != '\0')
        value = -1;
    // Free the temporary string
    xmlFree(string);
    return value;
}

int
bt_mbet_get_integer_property(xmlNode *node, const char *const name)
{
    char *endptr;
    xmlChar *property;
    int value;
    // Get the property as text
    property = xmlGetProp(node, (xmlChar *) name);
    if (property == NULL)
        return -1;
    // Convert it to int
    value = (int) strtol((const char *) property, &endptr, 10);
    if (*endptr != '\0')
        value = -1;
    // Free the temporary property
    xmlFree(property);
    return value;
}

long int
bt_mbet_get_long_property(xmlNode *node, const char *const name)
{
    char *endptr;
    xmlChar *property;
    long int value;
    // Get the property as text
    property = xmlGetProp(node, (xmlChar *) name);
    if (property == NULL)
        return -1;
    // Convert it to long int
    value = strtol((const char *) property, &endptr, 10);
    if (*endptr != '\0')
        value = -1;
    // Free the temporary property
    xmlFree(property);
    return value;
}

float
bt_mbet_get_float_property(xmlNode *node, const char *const name)
{
    xmlChar *property;
    char *endptr;
    float value;
    // Get the property as text
    property = xmlGetProp(node, (xmlChar *) name);
    if (property == NULL)
        return strtod("NaN", NULL);
    // Convert it to float
    value = (float) strtod((const char *) property, &endptr);
    if (*endptr != '\0')
        value = strtod("NaN", NULL);
    // Free the temporary property
    xmlFree(property);
    return value;
}

bool
bt_mbet_get_boolean_property(xmlNode *node, const char *const name)
{
    xmlChar *property;
    bool value;
    // Get the property as text
    property = xmlGetProp(node, (xmlChar *) name);
    if (property == NULL)
        return false;
    // Check if it's "true" otherwise it's false
    value = (xmlStrcmp(property, (const xmlChar *) "true") == 0);
    // Free the temporary property
    xmlFree(property);
    return value;
}

void
bt_mbet_get_date_property(xmlNode *node, const char *const name, struct tm *tm)
{
    xmlChar *property;
    // Get the property as text
    property = xmlGetProp(node, (xmlChar *) name);
    if (property == NULL)
        return;
    memset(tm, 0, sizeof(*tm));
    // Convert it to a tm structure
    strptime((char *) property, "%FT%T%Z", tm);
    // Free the temporary property
    xmlFree(property);
}

char *
bt_mbet_get_string_property(xmlNode *node, const char *const name)
{
    // Simple wrapper to avoid casting (this could be a macro)
    return (char *) xmlGetProp(node, (xmlChar *) name);
}
