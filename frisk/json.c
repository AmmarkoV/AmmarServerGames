#include "json.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define JSON_MAX_DEPTH 64

struct Parser {
    const char *s;
    size_t len;
    size_t pos;
    char *err;
    size_t errsz;
    int failed;
};

static void parser_error(struct Parser *p, const char *fmt, ...)
{
    va_list ap;
    size_t i, line = 1, column = 1;
    int n;

    if (p->failed) {
        return;
    }
    p->failed = 1;
    if (p->err == NULL || p->errsz == 0) {
        return;
    }
    for (i = 0; i < p->pos && i < p->len; i++) {
        if (p->s[i] == '\n') {
            line++;
            column = 1;
        } else {
            column++;
        }
    }
    n = snprintf(p->err, p->errsz, "line %zu, column %zu: ", line, column);
    if (n < 0 || (size_t)n >= p->errsz) {
        return;
    }
    va_start(ap, fmt);
    vsnprintf(p->err + n, p->errsz - (size_t)n, fmt, ap);
    va_end(ap);
}

static void skip_ws(struct Parser *p)
{
    while (p->pos < p->len) {
        char c = p->s[p->pos];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            p->pos++;
        } else {
            break;
        }
    }
}

static JsonValue *new_value(JsonType type)
{
    JsonValue *v = (JsonValue *)calloc(1, sizeof(*v));
    if (v != NULL) {
        v->type = type;
    }
    return v;
}

void json_free(JsonValue *value)
{
    size_t i;

    if (value == NULL) {
        return;
    }
    switch (value->type) {
    case JSON_STRING:
        free(value->u.string);
        break;
    case JSON_ARRAY:
        for (i = 0; i < value->u.array.count; i++) {
            json_free(value->u.array.items[i]);
        }
        free(value->u.array.items);
        break;
    case JSON_OBJECT:
        for (i = 0; i < value->u.object.count; i++) {
            free(value->u.object.keys[i]);
            json_free(value->u.object.values[i]);
        }
        free(value->u.object.keys);
        free(value->u.object.values);
        break;
    default:
        break;
    }
    free(value);
}

static char *parse_string_raw(struct Parser *p)
{
    char *out;
    size_t capacity, used = 0;

    if (p->pos >= p->len || p->s[p->pos] != '"') {
        parser_error(p, "expected a string");
        return NULL;
    }
    p->pos++;
    capacity = p->len - p->pos + 1;
    out = (char *)malloc(capacity);
    if (out == NULL) {
        parser_error(p, "out of memory");
        return NULL;
    }
    while (p->pos < p->len) {
        unsigned char c = (unsigned char)p->s[p->pos++];
        if (c == '"') {
            out[used] = '\0';
            return out;
        }
        if (c < 0x20) {
            parser_error(p, "control character in string");
            free(out);
            return NULL;
        }
        if (c == '\\') {
            char e;
            if (p->pos >= p->len) {
                parser_error(p, "unterminated escape sequence");
                free(out);
                return NULL;
            }
            e = p->s[p->pos++];
            switch (e) {
            case '"': out[used++] = '"'; break;
            case '\\': out[used++] = '\\'; break;
            case '/': out[used++] = '/'; break;
            case 'b': out[used++] = '\b'; break;
            case 'f': out[used++] = '\f'; break;
            case 'n': out[used++] = '\n'; break;
            case 'r': out[used++] = '\r'; break;
            case 't': out[used++] = '\t'; break;
            default:
                parser_error(p, "unsupported escape sequence \\%c", e);
                free(out);
                return NULL;
            }
        } else {
            out[used++] = (char)c;
        }
    }
    parser_error(p, "unterminated string");
    free(out);
    return NULL;
}

static JsonValue *parse_object(struct Parser *p, int depth);
static JsonValue *parse_array(struct Parser *p, int depth);

static JsonValue *parse_number(struct Parser *p)
{
    size_t start = p->pos;
    size_t digits_start;
    char buffer[64];
    size_t length;
    char *end = NULL;
    long long value;
    JsonValue *v;

    if (p->pos < p->len && p->s[p->pos] == '-') {
        p->pos++;
    }
    digits_start = p->pos;
    if (p->pos >= p->len || !isdigit((unsigned char)p->s[p->pos])) {
        parser_error(p, "expected a number");
        return NULL;
    }
    if (p->s[p->pos] == '0' && p->pos + 1 < p->len &&
        isdigit((unsigned char)p->s[p->pos + 1])) {
        parser_error(p, "leading zero in number");
        return NULL;
    }
    while (p->pos < p->len && isdigit((unsigned char)p->s[p->pos])) {
        p->pos++;
    }
    if (p->pos < p->len &&
        (p->s[p->pos] == '.' || p->s[p->pos] == 'e' || p->s[p->pos] == 'E')) {
        parser_error(p, "floating point numbers are not supported");
        return NULL;
    }
    if (p->pos == digits_start) {
        parser_error(p, "expected a number");
        return NULL;
    }
    length = p->pos - start;
    if (length >= sizeof(buffer)) {
        parser_error(p, "number is too long");
        return NULL;
    }
    memcpy(buffer, p->s + start, length);
    buffer[length] = '\0';
    errno = 0;
    value = strtoll(buffer, &end, 10);
    if (errno == ERANGE || end == buffer || *end != '\0') {
        parser_error(p, "integer out of range");
        return NULL;
    }
    v = new_value(JSON_INT);
    if (v == NULL) {
        parser_error(p, "out of memory");
        return NULL;
    }
    v->u.integer = value;
    return v;
}

static JsonValue *parse_value(struct Parser *p, int depth)
{
    JsonValue *v;

    if (depth > JSON_MAX_DEPTH) {
        parser_error(p, "maximum nesting depth exceeded");
        return NULL;
    }
    skip_ws(p);
    if (p->pos >= p->len) {
        parser_error(p, "unexpected end of input");
        return NULL;
    }
    switch (p->s[p->pos]) {
    case '{':
        return parse_object(p, depth);
    case '[':
        return parse_array(p, depth);
    case '"': {
        char *s = parse_string_raw(p);
        if (s == NULL) {
            return NULL;
        }
        v = new_value(JSON_STRING);
        if (v == NULL) {
            free(s);
            parser_error(p, "out of memory");
            return NULL;
        }
        v->u.string = s;
        return v;
    }
    case 't':
        if (p->len - p->pos >= 4 && memcmp(p->s + p->pos, "true", 4) == 0) {
            p->pos += 4;
            v = new_value(JSON_BOOL);
            if (v == NULL) {
                parser_error(p, "out of memory");
                return NULL;
            }
            v->u.boolean = 1;
            return v;
        }
        parser_error(p, "invalid literal");
        return NULL;
    case 'f':
        if (p->len - p->pos >= 5 && memcmp(p->s + p->pos, "false", 5) == 0) {
            p->pos += 5;
            v = new_value(JSON_BOOL);
            if (v == NULL) {
                parser_error(p, "out of memory");
                return NULL;
            }
            v->u.boolean = 0;
            return v;
        }
        parser_error(p, "invalid literal");
        return NULL;
    case 'n':
        parser_error(p, "null is not supported");
        return NULL;
    default:
        if (p->s[p->pos] == '-' || isdigit((unsigned char)p->s[p->pos])) {
            return parse_number(p);
        }
        parser_error(p, "unexpected character '%c'", p->s[p->pos]);
        return NULL;
    }
}

static int object_has_key(const JsonValue *object, const char *key)
{
    size_t i;
    for (i = 0; i < object->u.object.count; i++) {
        if (strcmp(object->u.object.keys[i], key) == 0) {
            return 1;
        }
    }
    return 0;
}

static int object_add(struct Parser *p, JsonValue *object,
                      char *key, JsonValue *value)
{
    size_t count = object->u.object.count;
    char **new_keys;
    JsonValue **new_values;

    if (object_has_key(object, key)) {
        parser_error(p, "duplicate object key \"%s\"", key);
        return -1;
    }
    new_keys = (char **)malloc((count + 1) * sizeof(*new_keys));
    new_values = (JsonValue **)malloc((count + 1) * sizeof(*new_values));
    if (new_keys == NULL || new_values == NULL) {
        free(new_keys);
        free(new_values);
        parser_error(p, "out of memory");
        return -1;
    }
    if (count > 0) {
        memcpy(new_keys, object->u.object.keys, count * sizeof(*new_keys));
        memcpy(new_values, object->u.object.values,
               count * sizeof(*new_values));
    }
    new_keys[count] = key;
    new_values[count] = value;
    free(object->u.object.keys);
    free(object->u.object.values);
    object->u.object.keys = new_keys;
    object->u.object.values = new_values;
    object->u.object.count = count + 1;
    return 0;
}

static JsonValue *parse_object(struct Parser *p, int depth)
{
    JsonValue *object = new_value(JSON_OBJECT);

    if (object == NULL) {
        parser_error(p, "out of memory");
        return NULL;
    }
    p->pos++; /* '{' */
    skip_ws(p);
    if (p->pos < p->len && p->s[p->pos] == '}') {
        p->pos++;
        return object;
    }
    for (;;) {
        char *key;
        JsonValue *value;

        skip_ws(p);
        key = parse_string_raw(p);
        if (key == NULL) {
            json_free(object);
            return NULL;
        }
        skip_ws(p);
        if (p->pos >= p->len || p->s[p->pos] != ':') {
            parser_error(p, "expected ':' after object key");
            free(key);
            json_free(object);
            return NULL;
        }
        p->pos++;
        value = parse_value(p, depth + 1);
        if (value == NULL) {
            free(key);
            json_free(object);
            return NULL;
        }
        if (object_add(p, object, key, value) != 0) {
            free(key);
            json_free(value);
            json_free(object);
            return NULL;
        }
        skip_ws(p);
        if (p->pos < p->len && p->s[p->pos] == '}') {
            p->pos++;
            return object;
        }
        if (p->pos >= p->len || p->s[p->pos] != ',') {
            parser_error(p, "expected ',' or '}' in object");
            json_free(object);
            return NULL;
        }
        p->pos++;
    }
}

static int array_add(struct Parser *p, JsonValue *array, JsonValue *value)
{
    size_t count = array->u.array.count;
    JsonValue **new_items = (JsonValue **)malloc(
        (count + 1) * sizeof(*new_items));

    if (new_items == NULL) {
        parser_error(p, "out of memory");
        return -1;
    }
    if (count > 0) {
        memcpy(new_items, array->u.array.items,
               count * sizeof(*new_items));
    }
    new_items[count] = value;
    free(array->u.array.items);
    array->u.array.items = new_items;
    array->u.array.count = count + 1;
    return 0;
}

static JsonValue *parse_array(struct Parser *p, int depth)
{
    JsonValue *array = new_value(JSON_ARRAY);

    if (array == NULL) {
        parser_error(p, "out of memory");
        return NULL;
    }
    p->pos++; /* '[' */
    skip_ws(p);
    if (p->pos < p->len && p->s[p->pos] == ']') {
        p->pos++;
        return array;
    }
    for (;;) {
        JsonValue *value = parse_value(p, depth + 1);
        if (value == NULL) {
            json_free(array);
            return NULL;
        }
        if (array_add(p, array, value) != 0) {
            json_free(value);
            json_free(array);
            return NULL;
        }
        skip_ws(p);
        if (p->pos < p->len && p->s[p->pos] == ']') {
            p->pos++;
            return array;
        }
        if (p->pos >= p->len || p->s[p->pos] != ',') {
            parser_error(p, "expected ',' or ']' in array");
            json_free(array);
            return NULL;
        }
        p->pos++;
    }
}

JsonValue *json_parse(const char *text, char *err, size_t err_size)
{
    struct Parser p;
    JsonValue *value;

    if (err != NULL && err_size > 0) {
        err[0] = '\0';
    }
    if (text == NULL) {
        if (err != NULL && err_size > 0) {
            snprintf(err, err_size, "no input");
        }
        return NULL;
    }
    memset(&p, 0, sizeof(p));
    p.s = text;
    p.len = strlen(text);
    p.err = err;
    p.errsz = err_size;

    value = parse_value(&p, 0);
    if (value == NULL) {
        if (err != NULL && err_size > 0 && err[0] == '\0') {
            snprintf(err, err_size, "invalid JSON");
        }
        return NULL;
    }
    skip_ws(&p);
    if (p.pos != p.len) {
        parser_error(&p, "trailing data after JSON value");
        json_free(value);
        return NULL;
    }
    return value;
}

const JsonValue *json_get(const JsonValue *object, const char *key)
{
    size_t i;

    if (object == NULL || object->type != JSON_OBJECT || key == NULL) {
        return NULL;
    }
    for (i = 0; i < object->u.object.count; i++) {
        if (strcmp(object->u.object.keys[i], key) == 0) {
            return object->u.object.values[i];
        }
    }
    return NULL;
}

const JsonValue *json_at(const JsonValue *array, size_t index)
{
    if (array == NULL || array->type != JSON_ARRAY ||
        index >= array->u.array.count) {
        return NULL;
    }
    return array->u.array.items[index];
}

size_t json_count(const JsonValue *array)
{
    if (array == NULL || array->type != JSON_ARRAY) {
        return 0;
    }
    return array->u.array.count;
}

int json_get_int(const JsonValue *value, long long *out)
{
    if (value == NULL || value->type != JSON_INT || out == NULL) {
        return -1;
    }
    *out = value->u.integer;
    return 0;
}

int json_get_bool(const JsonValue *value, int *out)
{
    if (value == NULL || value->type != JSON_BOOL || out == NULL) {
        return -1;
    }
    *out = value->u.boolean;
    return 0;
}

const char *json_get_string(const JsonValue *value)
{
    if (value == NULL || value->type != JSON_STRING) {
        return NULL;
    }
    return value->u.string;
}

struct Writer {
    char *out;
    size_t capacity;
    size_t used;
    int overflow;
};

static void writer_char(struct Writer *w, char c)
{
    if (w->used + 2 > w->capacity) {
        w->overflow = 1;
        return;
    }
    w->out[w->used++] = c;
}

static void writer_text(struct Writer *w, const char *text)
{
    while (text != NULL && *text != '\0') {
        writer_char(w, *text++);
    }
}

static void writer_string(struct Writer *w, const char *text)
{
    const unsigned char *p = (const unsigned char *)text;
    static const char hex[] = "0123456789abcdef";

    writer_char(w, '"');
    while (p != NULL && *p != '\0') {
        unsigned char c = *p++;
        switch (c) {
        case '"': writer_text(w, "\\\""); break;
        case '\\': writer_text(w, "\\\\"); break;
        case '\b': writer_text(w, "\\b"); break;
        case '\f': writer_text(w, "\\f"); break;
        case '\n': writer_text(w, "\\n"); break;
        case '\r': writer_text(w, "\\r"); break;
        case '\t': writer_text(w, "\\t"); break;
        default:
            if (c < 0x20) {
                char escape[7] = { '\\', 'u', '0', '0',
                                   hex[c >> 4], hex[c & 0xf], '\0' };
                writer_text(w, escape);
            } else {
                writer_char(w, (char)c);
            }
            break;
        }
    }
    writer_char(w, '"');
}

static void writer_value(struct Writer *w, const JsonValue *value)
{
    size_t i;
    char number[32];

    if (value == NULL || w->overflow) {
        return;
    }
    switch (value->type) {
    case JSON_NULL:
        writer_text(w, "null");
        break;
    case JSON_BOOL:
        writer_text(w, value->u.boolean ? "true" : "false");
        break;
    case JSON_INT:
        snprintf(number, sizeof(number), "%lld", value->u.integer);
        writer_text(w, number);
        break;
    case JSON_STRING:
        writer_string(w, value->u.string);
        break;
    case JSON_ARRAY:
        writer_char(w, '[');
        for (i = 0; i < value->u.array.count && !w->overflow; i++) {
            if (i > 0) {
                writer_char(w, ',');
            }
            writer_value(w, value->u.array.items[i]);
        }
        writer_char(w, ']');
        break;
    case JSON_OBJECT:
        writer_char(w, '{');
        for (i = 0; i < value->u.object.count && !w->overflow; i++) {
            if (i > 0) {
                writer_char(w, ',');
            }
            writer_string(w, value->u.object.keys[i]);
            writer_char(w, ':');
            writer_value(w, value->u.object.values[i]);
        }
        writer_char(w, '}');
        break;
    default:
        writer_text(w, "null");
        break;
    }
}

int json_write_compact(const JsonValue *value, char *out, size_t out_size)
{
    struct Writer w;

    if (out == NULL || out_size == 0) {
        return -1;
    }
    w.out = out;
    w.capacity = out_size;
    w.used = 0;
    w.overflow = 0;
    writer_value(&w, value);
    if (w.overflow) {
        out[out_size - 1] = '\0';
        return -1;
    }
    out[w.used] = '\0';
    return 0;
}
