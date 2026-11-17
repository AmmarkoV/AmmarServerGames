/* Minimal JSON tree reader/writer used by the FRISK map loader.
 *
 * Supported values: objects, arrays, strings, integers and booleans.  Floats,
 * null and \u escapes are rejected on purpose: every FRISK data file is kept
 * inside this subset so the parser can stay small and strict.
 */
#ifndef FRISK_JSON_H
#define FRISK_JSON_H

#include <stddef.h>

typedef enum {
    JSON_NULL = 0,
    JSON_BOOL,
    JSON_INT,
    JSON_STRING,
    JSON_ARRAY,
    JSON_OBJECT
} JsonType;

typedef struct JsonValue JsonValue;

struct JsonValue {
    JsonType type;
    union {
        int boolean;
        long long integer;
        char *string;
        struct {
            JsonValue **items;
            size_t count;
        } array;
        struct {
            char **keys;
            JsonValue **values;
            size_t count;
        } object;
    } u;
};

/* Parse `text` (NUL terminated).  Returns NULL on error and writes a
 * human-readable message including line/column to `err`. */
JsonValue *json_parse(const char *text, char *err, size_t err_size);

void json_free(JsonValue *value);

/* Object lookup; returns NULL when the key is absent or `object` is not an
 * object. */
const JsonValue *json_get(const JsonValue *object, const char *key);
const JsonValue *json_at(const JsonValue *array, size_t index);
size_t json_count(const JsonValue *array);

int json_get_int(const JsonValue *value, long long *out);
int json_get_bool(const JsonValue *value, int *out);
const char *json_get_string(const JsonValue *value);

/* Serialise a value compactly (no whitespace).  Returns 0 on success, -1 if
 * the output buffer is too small.  Always NUL terminates on success. */
int json_write_compact(const JsonValue *value, char *out, size_t out_size);

#endif
