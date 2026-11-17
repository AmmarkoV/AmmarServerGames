#ifndef FRISK_HTTP_H
#define FRISK_HTTP_H

#include <stddef.h>

#include "../../AmmServerlib/AmmServerlib.h"

#define FRISK_JSON_MAX_DEPTH 8

/* Streaming, bounded JSON writer over rqst->content.  Handlers build one
 * object, nest arrays/objects up to FRISK_JSON_MAX_DEPTH and call
 * frisk_json_end().  On overflow the response is replaced with
 * {"ok":0,"err":"overflow"} and the status becomes 500. */
struct FriskJson {
    struct AmmServer_DynamicRequest *rqst;
    size_t used;
    int overflow;
    int depth;
    int first[FRISK_JSON_MAX_DEPTH];
    char kind[FRISK_JSON_MAX_DEPTH];
};

void frisk_json_begin(struct FriskJson *json,
                      struct AmmServer_DynamicRequest *rqst);
int frisk_json_end(struct FriskJson *json);

void frisk_json_key(struct FriskJson *json, const char *key);
void frisk_json_key_int(struct FriskJson *json, const char *key, long long value);
void frisk_json_key_str(struct FriskJson *json, const char *key,
                        const char *value);
void frisk_json_key_bool(struct FriskJson *json, const char *key, int value);
void frisk_json_key_raw(struct FriskJson *json, const char *key,
                        const char *raw);
void frisk_json_key_array(struct FriskJson *json, const char *key);
void frisk_json_key_object(struct FriskJson *json, const char *key);

void frisk_json_array_next(struct FriskJson *json);
void frisk_json_array_int(struct FriskJson *json, long long value);
void frisk_json_array_str(struct FriskJson *json, const char *value);
void frisk_json_array_object(struct FriskJson *json);
void frisk_json_array_end(struct FriskJson *json);
void frisk_json_object_end(struct FriskJson *json);

void frisk_json_value_int(struct FriskJson *json, long long value);
void frisk_json_value_str(struct FriskJson *json, const char *value);
void frisk_json_value_bool(struct FriskJson *json, int value);
void frisk_json_value_raw(struct FriskJson *json, const char *raw);

/* Complete {"ok":0,"err":code} response with an HTTP status. */
void frisk_json_error(struct AmmServer_DynamicRequest *rqst,
                      unsigned int status, const char *code);

/* These helpers assume the caller holds friskLock (see frisk.h). */
int frisk_require_user(struct AmmServer_DynamicRequest *rqst,
                       char *username, size_t username_size);
int frisk_require_csrf(struct AmmServer_DynamicRequest *rqst);
int frisk_csrf_token(struct AmmServer_DynamicRequest *rqst,
                     char *out, size_t out_size);

int frisk_is_post(struct AmmServer_DynamicRequest *rqst);
int frisk_param_uint(struct AmmServer_DynamicRequest *rqst, const char *name,
                     unsigned int minimum, unsigned int maximum,
                     unsigned int *out);
int frisk_param_str(struct AmmServer_DynamicRequest *rqst, const char *name,
                    char *out, size_t out_size);
int frisk_post_str(struct AmmServer_DynamicRequest *rqst, const char *name,
                   char *out, size_t out_size);
int frisk_post_uint(struct AmmServer_DynamicRequest *rqst, const char *name,
                    unsigned int minimum, unsigned int maximum,
                    unsigned int *out);

#endif
