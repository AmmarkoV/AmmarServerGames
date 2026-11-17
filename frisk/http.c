#include "http.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "../../UserAccounts/userAccountsWeb.h"
#include "accounts.h"

static void json_putc(struct FriskJson *json, char c)
{
    if (json->overflow || json->rqst == NULL) {
        return;
    }
    if (json->used + 2 > json->rqst->MAXcontentSize) {
        json->overflow = 1;
        return;
    }
    json->rqst->content[json->used++] = c;
}

static void json_puts(struct FriskJson *json, const char *text)
{
    while (text != NULL && *text != '\0' && !json->overflow) {
        json_putc(json, *text++);
    }
}

static void json_string(struct FriskJson *json, const char *text)
{
    const unsigned char *p = (const unsigned char *)
                             (text != NULL ? text : "");
    static const char hex[] = "0123456789abcdef";

    json_putc(json, '"');
    while (*p != '\0' && !json->overflow) {
        unsigned char c = *p++;
        switch (c) {
        case '"': json_puts(json, "\\\""); break;
        case '\\': json_puts(json, "\\\\"); break;
        case '\b': json_puts(json, "\\b"); break;
        case '\f': json_puts(json, "\\f"); break;
        case '\n': json_puts(json, "\\n"); break;
        case '\r': json_puts(json, "\\r"); break;
        case '\t': json_puts(json, "\\t"); break;
        default:
            if (c < 0x20) {
                char escape[7] = { '\\', 'u', '0', '0',
                                   hex[c >> 4], hex[c & 0xf], '\0' };
                json_puts(json, escape);
            } else {
                json_putc(json, (char)c);
            }
            break;
        }
    }
    json_putc(json, '"');
}

void frisk_json_begin(struct FriskJson *json,
                      struct AmmServer_DynamicRequest *rqst)
{
    memset(json, 0, sizeof(*json));
    json->rqst = rqst;
    json->depth = 1;
    json->kind[0] = 'o';
    json->first[0] = 1;
    if (rqst == NULL || rqst->content == NULL ||
        rqst->MAXcontentSize < 4) {
        json->overflow = 1;
        return;
    }
    rqst->content[0] = '{';
    json->used = 1;
    rqst->contentSize = 0;
}

static void json_push(struct FriskJson *json, char kind)
{
    if (json->depth >= FRISK_JSON_MAX_DEPTH) {
        json->overflow = 1;
        return;
    }
    json->kind[json->depth] = kind;
    json->first[json->depth] = 1;
    json->depth++;
}

void frisk_json_key(struct FriskJson *json, const char *key)
{
    if (json->depth <= 0 || json->kind[json->depth - 1] != 'o') {
        json->overflow = 1;
        return;
    }
    if (!json->first[json->depth - 1]) {
        json_putc(json, ',');
    }
    json->first[json->depth - 1] = 0;
    json_string(json, key);
    json_putc(json, ':');
}

void frisk_json_value_int(struct FriskJson *json, long long value)
{
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%lld", value);
    json_puts(json, buffer);
}

void frisk_json_value_str(struct FriskJson *json, const char *value)
{
    json_string(json, value);
}

void frisk_json_value_bool(struct FriskJson *json, int value)
{
    json_puts(json, value ? "true" : "false");
}

void frisk_json_value_raw(struct FriskJson *json, const char *raw)
{
    json_puts(json, raw);
}

void frisk_json_key_int(struct FriskJson *json, const char *key,
                        long long value)
{
    frisk_json_key(json, key);
    frisk_json_value_int(json, value);
}

void frisk_json_key_str(struct FriskJson *json, const char *key,
                        const char *value)
{
    frisk_json_key(json, key);
    frisk_json_value_str(json, value);
}

void frisk_json_key_bool(struct FriskJson *json, const char *key, int value)
{
    frisk_json_key(json, key);
    frisk_json_value_bool(json, value);
}

void frisk_json_key_raw(struct FriskJson *json, const char *key,
                        const char *raw)
{
    frisk_json_key(json, key);
    frisk_json_value_raw(json, raw);
}

void frisk_json_key_array(struct FriskJson *json, const char *key)
{
    frisk_json_key(json, key);
    json_putc(json, '[');
    json_push(json, 'a');
}

void frisk_json_key_object(struct FriskJson *json, const char *key)
{
    frisk_json_key(json, key);
    json_putc(json, '{');
    json_push(json, 'o');
}

void frisk_json_array_next(struct FriskJson *json)
{
    if (json->depth <= 0 || json->kind[json->depth - 1] != 'a') {
        json->overflow = 1;
        return;
    }
    if (!json->first[json->depth - 1]) {
        json_putc(json, ',');
    }
    json->first[json->depth - 1] = 0;
}

void frisk_json_array_int(struct FriskJson *json, long long value)
{
    frisk_json_array_next(json);
    frisk_json_value_int(json, value);
}

void frisk_json_array_str(struct FriskJson *json, const char *value)
{
    frisk_json_array_next(json);
    frisk_json_value_str(json, value);
}

void frisk_json_array_object(struct FriskJson *json)
{
    frisk_json_array_next(json);
    json_putc(json, '{');
    json_push(json, 'o');
}

void frisk_json_array_end(struct FriskJson *json)
{
    if (json->depth <= 1 || json->kind[json->depth - 1] != 'a') {
        json->overflow = 1;
        return;
    }
    json->depth--;
    json_putc(json, ']');
}

void frisk_json_object_end(struct FriskJson *json)
{
    if (json->depth <= 1 || json->kind[json->depth - 1] != 'o') {
        json->overflow = 1;
        return;
    }
    json->depth--;
    json_putc(json, '}');
}

int frisk_json_end(struct FriskJson *json)
{
    if (json == NULL || json->rqst == NULL) {
        return 0;
    }
    while (json->depth > 1 && !json->overflow) {
        json->depth--;
        json_putc(json, json->kind[json->depth] == 'a' ? ']' : '}');
    }
    if (!json->overflow && json->used + 1 <= json->rqst->MAXcontentSize) {
        json_putc(json, '}');
    }
    if (json->overflow) {
        static const char fallback[] =
            "{\"ok\":0,\"err\":\"overflow\"}";
        snprintf(json->rqst->content, json->rqst->MAXcontentSize, "%s",
                 fallback);
        json->rqst->contentSize =
            (unsigned long)strlen(json->rqst->content);
        json->rqst->headerResponse = 500;
        return 0;
    }
    json->rqst->content[json->used] = '\0';
    json->rqst->contentSize = json->used;
    return 1;
}

void frisk_json_error(struct AmmServer_DynamicRequest *rqst,
                      unsigned int status, const char *code)
{
    struct FriskJson json;

    frisk_json_begin(&json, rqst);
    frisk_json_key_int(&json, "ok", 0);
    frisk_json_key_str(&json, "err", code);
    frisk_json_end(&json);
    if (rqst != NULL) {
        rqst->headerResponse = status;
    }
}

int frisk_require_user(struct AmmServer_DynamicRequest *rqst,
                       char *username, size_t username_size)
{
    if (AmmServer_CurrentUsername(rqst, username,
                                  (unsigned int)username_size)) {
        accounts_touch(username);
        return 1;
    }
    if (accounts_remember_login(rqst, username, username_size)) {
        accounts_touch(username);
        return 1;
    }
    frisk_json_error(rqst, 401, "login");
    return 0;
}

static int copy_value(const char *value, unsigned int length,
                      char *out, size_t out_size)
{
    if (value == NULL || out == NULL || out_size == 0 ||
        (size_t)length >= out_size) {
        return 0;
    }
    if (length > 0) {
        memcpy(out, value, length);
    }
    out[length] = '\0';
    return 1;
}

static int parse_uint_value(const char *text, unsigned int length,
                            unsigned int minimum, unsigned int maximum,
                            unsigned int *out)
{
    unsigned long long value = 0;
    unsigned int i;

    if (text == NULL || out == NULL || length == 0 || length > 10) {
        return 0;
    }
    for (i = 0; i < length; i++) {
        if (!isdigit((unsigned char)text[i])) {
            return 0;
        }
        value = value * 10ULL + (unsigned long long)(text[i] - '0');
    }
    if (value < minimum || value > maximum) {
        return 0;
    }
    *out = (unsigned int)value;
    return 1;
}

int frisk_require_csrf(struct AmmServer_DynamicRequest *rqst)
{
    char token[64];

    token[0] = '\0';
    if (!frisk_post_str(rqst, "csrf", token, sizeof(token)) ||
        !AmmServer_ValidateCSRFToken(rqst, token)) {
        frisk_json_error(rqst, 403, "csrf");
        return 0;
    }
    return 1;
}

int frisk_csrf_token(struct AmmServer_DynamicRequest *rqst,
                     char *out, size_t out_size)
{
    return AmmServer_GenerateCSRFToken(rqst, out, (unsigned int)out_size);
}

int frisk_is_post(struct AmmServer_DynamicRequest *rqst)
{
    return _POSTnum(rqst) > 0;
}

int frisk_param_uint(struct AmmServer_DynamicRequest *rqst, const char *name,
                     unsigned int minimum, unsigned int maximum,
                     unsigned int *out)
{
    const char *value;
    unsigned int length = 0;

    if (out == NULL || name == NULL) {
        return 0;
    }
    if (_GETexists(rqst, name)) {
        value = _GET(rqst, name, &length);
    } else if (frisk_is_post(rqst) && _POSTexists(rqst, name)) {
        value = _POST(rqst, name, &length);
    } else {
        return 0;
    }
    return parse_uint_value(value, length, minimum, maximum, out);
}

int frisk_param_str(struct AmmServer_DynamicRequest *rqst, const char *name,
                    char *out, size_t out_size)
{
    const char *value;
    unsigned int length = 0;

    if (out == NULL || out_size == 0 || name == NULL) {
        return 0;
    }
    out[0] = '\0';
    if (_GETexists(rqst, name)) {
        value = _GET(rqst, name, &length);
    } else if (frisk_is_post(rqst) && _POSTexists(rqst, name)) {
        value = _POST(rqst, name, &length);
    } else {
        return 0;
    }
    return copy_value(value, length, out, out_size);
}

int frisk_post_str(struct AmmServer_DynamicRequest *rqst, const char *name,
                   char *out, size_t out_size)
{
    const char *value;
    unsigned int length = 0;

    if (out == NULL || out_size == 0 || name == NULL) {
        return 0;
    }
    out[0] = '\0';
    if (!_POSTexists(rqst, name)) {
        return 0;
    }
    value = _POST(rqst, name, &length);
    return copy_value(value, length, out, out_size);
}

int frisk_post_uint(struct AmmServer_DynamicRequest *rqst, const char *name,
                    unsigned int minimum, unsigned int maximum,
                    unsigned int *out)
{
    const char *value;
    unsigned int length = 0;

    if (out == NULL || name == NULL || !_POSTexists(rqst, name)) {
        return 0;
    }
    value = _POST(rqst, name, &length);
    return parse_uint_value(value, length, minimum, maximum, out);
}
