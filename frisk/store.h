#ifndef FRISK_STORE_H
#define FRISK_STORE_H

#include <stddef.h>

int store_ensure_dir(const char *path, char *err, size_t err_size);
int store_ensure_parent(const char *file_path, char *err, size_t err_size);
int store_read_file(const char *path, char **out, size_t *out_size,
                    char *err, size_t err_size);
int store_write_atomic(const char *path, const char *data, size_t length,
                       char *err, size_t err_size);

/* Creates <data_dir>/{games,chat,clips}. */
int store_init(const char *data_dir, char *err, size_t err_size);

#endif
