#include <bt-private.h>
#include <bt-mbet-xml.h>
#include <bt-mbet-score.h>
#include <bt-memory.h>

#include <string.h>

static int
bt_mbet_get_integer_from_score(char *string)
{
    char *endptr;
    int value;
    // If the first character is `_' we skip it
    // it might mean something but for now we
    // don't know
    //
    // TODO: Find what what is the meaning of this
    //       character
    if (*string == '_')
        string += 1;
    // Check it's an advantage score. For the first player
    // string[1] == ':' and for the other, it's the '\0'
    if ((string[0] == 'A') && ((string[1] == '\0') || (string[1] == ':')))
        return -2;
    // Convert it to a number if it's not advantage
    value = strtol(string, &endptr, 10);
    // Check if the conversion was succesful
    if ((*endptr == '\0') || (*endptr == ':') || (*endptr == '_'))
        return value;
    // This means "Unknow?"
    return -1;
}

static void
bt_mbet_extract_values(char *string, int8_t *first, int8_t *second)
{
    char *separator;
    // Initialize these in case of an early return
    *second = -1;
    *first = -1;
    // Find the score separator used by `mbet' ':'
    separator = strchr(string, ':');
    if (separator == NULL)
        return;
    // Extract the values using the helper function
    *first = bt_mbet_get_integer_from_score(string);
    *second = bt_mbet_get_integer_from_score(separator + 1);

}

static void
bt_mbet_extract_score(char *string, bt_mbet_score_item *score, uint8_t *active)
{
    char *strip;
    // Assign invalid values in case of failure
    score->home = -1;
    score->away = -1;
    // WTF?
    if (string == NULL)
        return;
    // Find the closing parenthesis
    strip = strchr(string, ')');
    if (strip != NULL)
        *strip = '\0';
    // Find this to determine who is serving
    strip = strchr(string, '*');
    if (strip != NULL) {
        int value;
        // TODO: perhaps no one is serving if the match is
        //       stopped or hasn't started yet. We haven't
        //       checked this case and it would be great to
        //       do something about it.
        // This means that the second player is
        // serving, because the '*' was found
        // after the ')'
        if (strip > string) {
            *strip = '\0';
            value = 2;
        } else { // Otherwise the other one is serving
            string += 1;
            value = 1;
        }
        // If we're pointing somewhere, we have
        // a player with the service
        if (active != NULL)
            *active = value;
    }
    // Extract the score from the string
    // and save it in the right structures
    bt_mbet_extract_values(string, &score->home, &score->away);
}

static bt_mbet_score_item *
bt_mbet_extract_sets(char *const string, int8_t *count)
{
    bt_mbet_score_item *score;
    char **sets;
    char *closing;
    // Initialize this in case of early return
    *count = 0;
    // WTF?
    if (string == NULL)
        return NULL;
    // This is a poitner to the closing parenthesis
    closing = strchr(string, ')');
    if (closing != NULL)
        *closing = '\0';
    // Break the string at the ',' character
    // each item woule be the score of a set
    sets = bt_util_string_splitstr(string, ", ");
    while (sets[*count] != NULL)
        ++(*count);
    // Initialize this to be safe
    score = NULL;
    // There is no score, please exit this function
    if (*count == 0)
        goto failed;
    // Allocate space for the score object
    // FIXME: Perhaps it would be better to
    //        have an array of 5 items to
    //        avoid `malloc`ing. After all
    //        there is no possibility for a
    //        tennis match with more than 5
    //        sets
    score = bt_malloc(*count * sizeof(*score));
    if (score == NULL)
        goto failed;
    // Initialize all the values because some of them
    // might not be initialized
    memset(score, 0, *count * sizeof(*score));
    for (size_t i = 0; i < *count; ++i) {
        // Extract the i-th set score
        bt_mbet_extract_score(sets[i], score + i, NULL);
    }
failed:
    // Release resources
    bt_string_list_free(sets);
    return score;
}

bt_mbet_score *
bt_score_parse_mbet(xmlNode *node)
{
    bt_mbet_score *result;
    char *content;
    char **parts;
    // Get the contents of the `liveresult' tag
    content = bt_mbet_get_node_conent_string(node, "liveresult");
    if (content == NULL)
        return NULL;
    // Allocate space forthe result object
    // TODO: Since this is a simple structure
    //       perhaps we can avoid to malloc
    //       it.
    result = bt_malloc(sizeof(*result));
    if (result == NULL)
        goto failure;
    // Initiialize all the values to 0 since there is
    // no guarantee that they will be initialized.
    memset(&result->score, 0, sizeof(result->score));
    memset(&result->game, 0, sizeof(result->game));
    // Initialize some other values
    result->service = 0;
    result->nsets = 0;
    result->sets = NULL;
    // Break the string to parse each interesting
    // part
    parts = bt_util_string_splitstr(content, " (");
    if ((parts[0] != NULL) && (parts[1] != NULL)) {
        if (parts[2] == NULL) {
            // Get the first set, it's always before any '('
            // character becuase it's not parenthesized
            result->sets = bt_mbet_extract_sets(parts[0], &result->nsets);
            bt_mbet_extract_score(parts[1], &result->game, &result->service);
        } else {
            // Get the remaining sets, they always go
            // after the openning parenthesis
            bt_mbet_extract_score(parts[0], &result->score, NULL);
            result->sets = bt_mbet_extract_sets(parts[1], &result->nsets);
            bt_mbet_extract_score(parts[2], &result->game, &result->service);
        }
    }
    // Release used resources
    bt_string_list_free(parts);
failure:
    bt_free(content);
    return result;
}

bt_mbet_score *
bt_score_parse_oncourt(const char *const source)
{
    bt_mbet_score_item *item;
    bt_mbet_score *result;
    char **sets;
    if ((source == NULL) || (*source == '\0'))
        return NULL;
    result = bt_malloc(sizeof(*result));
    if (result == NULL)
        return NULL;
    sets = bt_string_splitchr(source, ' ');
    for (result->nsets = 0; sets[result->nsets] != NULL; ++result->nsets)
;
    result->sets = bt_malloc(result->nsets * sizeof(*result->sets));
    if (result->sets == NULL)
        goto error;
    item = &result->game;

    item->home = -1;
    item->away = -1;

    memset(&result->service, 0, sizeof(result->game));
    memset(&result->score, 0, sizeof(result->game));
    for (int8_t idx = 0; idx < result->nsets; ++idx) {
        bt_mbet_score_item *set;
        char *next;
        set = &result->sets[idx];
        item = &result->score;

        set->home = strtol(sets[idx], &next, 10);
        set->away = strtol(next + 1, &next, 10);

        if (set->home > set->away) {
            item->home += 1;
        } else {
            item->away += 1;
        }
    }
    bt_string_list_free(sets);
    return result;
error:
    bt_string_list_free(sets);
    bt_free(result);
    return NULL;
}

void
bt_mbet_score_free(bt_mbet_score *score)
{
    if (score == NULL)
        return;
    bt_free(score->sets);
    bt_free(score);
}
