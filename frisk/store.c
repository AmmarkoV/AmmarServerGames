#include "store.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define STORE_PATH_MAX 1024
#define STORE_MAX_READ (32u * 1024u * 1024u)

static void set_error(char *err, size_t err_size, const char *what,
                      const char *path)
{
    if (err == NULL || err_size == 0) {
        return;
    }
    snprintf(err, err_size, "%s %s: %s", what, path != NULL ? path : "",
             strerror(errno));
}

int store_ensure_dir(const char *path, char *err, size_t err_size)
{
    char copy[STORE_PATH_MAX];
    char *cursor;
    size_t length;

    if (path == NULL || path[0] == '\0') {
        if (err != NULL && err_size > 0) {
            snprintf(err, err_size, "empty directory path");
        }
        return -1;
    }
    length = strlen(path);
    if (length >= sizeof(copy)) {
        if (err != NULL && err_size > 0) {
            snprintf(err, err_size, "directory path is too long");
        }
        return -1;
    }
    memcpy(copy, path, length + 1);
    cursor = copy;
    while (*cursor != '\0') {
        if (*cursor == '/') {
            *cursor = '\0';
            if (copy[0] != '\0' && mkdir(copy, 0755) != 0 &&
                errno != EEXIST) {
                set_error(err, err_size, "cannot create directory", copy);
                *cursor = '/';
                return -1;
            }
            *cursor = '/';
        }
        cursor++;
    }
    if (mkdir(copy, 0755) != 0 && errno != EEXIST) {
        set_error(err, err_size, "cannot create directory", copy);
        return -1;
    }
    return 0;
}

int store_ensure_parent(const char *file_path, char *err, size_t err_size)
{
    char copy[STORE_PATH_MAX];
    char *slash;
    size_t length;

    if (file_path == NULL) {
        return -1;
    }
    length = strlen(file_path);
    if (length >= sizeof(copy)) {
        if (err != NULL && err_size > 0) {
            snprintf(err, err_size, "file path is too long");
        }
        return -1;
    }
    memcpy(copy, file_path, length + 1);
    slash = strrchr(copy, '/');
    if (slash == NULL) {
        return 0;
    }
    *slash = '\0';
    if (copy[0] == '\0') {
        return 0;
    }
    return store_ensure_dir(copy, err, err_size);
}

int store_read_file(const char *path, char **out, size_t *out_size,
                    char *err, size_t err_size)
{
    FILE *file;
    long length;
    char *buffer;
    size_t got;

    if (path == NULL || out == NULL) {
        return -1;
    }
    file = fopen(path, "rb");
    if (file == NULL) {
        set_error(err, err_size, "cannot open", path);
        return -1;
    }
    if (fseek(file, 0, SEEK_END) != 0 || (length = ftell(file)) < 0) {
        set_error(err, err_size, "cannot size", path);
        fclose(file);
        return -1;
    }
    if ((unsigned long)length > STORE_MAX_READ) {
        if (err != NULL && err_size > 0) {
            snprintf(err, err_size, "file too large: %s", path);
        }
        fclose(file);
        return -1;
    }
    rewind(file);
    buffer = (char *)malloc((size_t)length + 1);
    if (buffer == NULL) {
        fclose(file);
        if (err != NULL && err_size > 0) {
            snprintf(err, err_size, "out of memory reading %s", path);
        }
        return -1;
    }
    got = fread(buffer, 1, (size_t)length, file);
    fclose(file);
    if (got != (size_t)length) {
        set_error(err, err_size, "short read", path);
        free(buffer);
        return -1;
    }
    buffer[length] = '\0';
    *out = buffer;
    if (out_size != NULL) {
        *out_size = (size_t)length;
    }
    return 0;
}

static void fsync_parent(const char *path)
{
    char copy[STORE_PATH_MAX];
    char *slash;
    int fd;

    if (strlen(path) >= sizeof(copy)) {
        return;
    }
    strcpy(copy, path);
    slash = strrchr(copy, '/');
    if (slash == NULL) {
        return;
    }
    if (slash == copy) {
        slash[1] = '\0';
    } else {
        *slash = '\0';
    }
    fd = open(copy, O_RDONLY | O_DIRECTORY);
    if (fd >= 0) {
        (void)fsync(fd);
        close(fd);
    }
}

int store_write_atomic(const char *path, const char *data, size_t length,
                       char *err, size_t err_size)
{
    char temporary[STORE_PATH_MAX];
    int fd;
    size_t written = 0;
    int saved_errno;

    if (path == NULL || data == NULL) {
        return -1;
    }
    if (strlen(path) + 5 >= sizeof(temporary)) {
        if (err != NULL && err_size > 0) {
            snprintf(err, err_size, "file path is too long");
        }
        return -1;
    }
    snprintf(temporary, sizeof(temporary), "%s.tmp", path);
    fd = open(temporary, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) {
        set_error(err, err_size, "cannot write", temporary);
        return -1;
    }
    while (written < length) {
        ssize_t chunk = write(fd, data + written, length - written);
        if (chunk < 0) {
            if (errno == EINTR) {
                continue;
            }
            saved_errno = errno;
            close(fd);
            unlink(temporary);
            errno = saved_errno;
            set_error(err, err_size, "cannot write", temporary);
            return -1;
        }
        written += (size_t)chunk;
    }
    if (fsync(fd) != 0) {
        saved_errno = errno;
        close(fd);
        unlink(temporary);
        errno = saved_errno;
        set_error(err, err_size, "cannot fsync", temporary);
        return -1;
    }
    close(fd);
    if (rename(temporary, path) != 0) {
        saved_errno = errno;
        unlink(temporary);
        errno = saved_errno;
        set_error(err, err_size, "cannot replace", path);
        return -1;
    }
    fsync_parent(path);
    return 0;
}

int store_init(const char *data_dir, char *err, size_t err_size)
{
    char path[STORE_PATH_MAX];

    if (data_dir == NULL) {
        return -1;
    }
    if (store_ensure_dir(data_dir, err, err_size) != 0) {
        return -1;
    }
    snprintf(path, sizeof(path), "%s/games", data_dir);
    if (store_ensure_dir(path, err, err_size) != 0) {
        return -1;
    }
    snprintf(path, sizeof(path), "%s/chat", data_dir);
    if (store_ensure_dir(path, err, err_size) != 0) {
        return -1;
    }
    snprintf(path, sizeof(path), "%s/clips", data_dir);
    if (store_ensure_dir(path, err, err_size) != 0) {
        return -1;
    }
    return 0;
}
