#include "chat.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "../../AmmServerlib/AmmServerlib.h"
#include "../../AmmServerlib/tools/http_tools.h"
#include "frisk.h"
#include "games.h"
#include "http.h"
#include "store.h"

#define CHAT_ROOM_SIZE 32
#define CHAT_MAX_ROOMS (FRISK_MAX_GAMES + 1)
#define CHAT_MAX_LIMIT 100
#define CHAT_DEFAULT_LIMIT 40
#define CHAT_TEXT_MAX 500
#define CHAT_TEXT_ESCAPED (CHAT_TEXT_MAX * 6 + 16)
#define CHAT_LINE_SIZE 4096
#define CHAT_RATE_TABLE 512
#define CHAT_RATE_PER_MINUTE 20
#define CHAT_CLIP_SECONDS 10
#define CHAT_CLIPS_PER_DAY 30
#define CHAT_RETENTION_SECONDS (30L * 24L * 60L * 60L)
#define CHAT_ORPHAN_GRACE (60L * 60L)
#define CHAT_DEFAULT_CLIP_CAP_MB 256u
#define CHAT_CLIP_NAME_SIZE 96

struct ChatRoomState {
    char room[CHAT_ROOM_SIZE];
    long last_id;
    int loaded;
    time_t last_used;
};

struct ChatMessage {
    long id;
    long time;
    char user[32];
    char kind[8];
    char text[CHAT_TEXT_MAX + 1];
    char file[CHAT_CLIP_NAME_SIZE];
    int duration_ms;
    int size;
    int index;
};

struct ChatRate {
    char user[32];
    time_t last_text;
    time_t text_window;
    int text_count;
    time_t last_clip;
    time_t clip_window;
    int clip_count;
    int used;
};

struct ChatClipRef {
    char name[CHAT_CLIP_NAME_SIZE];
    char room[CHAT_ROOM_SIZE];
};

struct ChatClipFile {
    char name[CHAT_CLIP_NAME_SIZE];
    unsigned long long size;
    time_t mtime;
};

static char chat_dir[FRISK_PATH_SIZE];
static char clips_dir[FRISK_PATH_SIZE];
static unsigned long long clip_cap_bytes;
static int chat_ready;

static struct ChatRoomState rooms[CHAT_MAX_ROOMS];
static int room_count;
static struct ChatRate rates[CHAT_RATE_TABLE];

static pthread_t housekeeping_thread;
static int housekeeping_started;
static pthread_mutex_t housekeeping_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t housekeeping_cond = PTHREAD_COND_INITIALIZER;
static int housekeeping_stop;

static long now_seconds(void)
{
    return (long)time(NULL);
}

static int room_parse(const char *room, int *game_id)
{
    const char *p;
    char canonical[CHAT_ROOM_SIZE];
    long value;

    if (room == NULL || room[0] == '\0') {
        return 0;
    }
    if (strcmp(room, "lobby") == 0) {
        if (game_id != NULL) {
            *game_id = -1;
        }
        return 1;
    }
    if (room[0] != 'g' || room[1] == '\0') {
        return 0;
    }
    for (p = room + 1; *p != '\0'; p++) {
        if (!isdigit((unsigned char)*p)) {
            return 0;
        }
    }
    if (p - (room + 1) > 10) {
        return 0;
    }
    value = strtol(room + 1, NULL, 10);
    if (value <= 0 || value > 1000000000L) {
        return 0;
    }
    snprintf(canonical, sizeof(canonical), "g%ld", value);
    if (strcmp(room, canonical) != 0) {
        return 0;
    }
    if (game_id != NULL) {
        *game_id = (int)value;
    }
    return 1;
}

static int room_exists(const char *room)
{
    int id;

    if (!room_parse(room, &id)) {
        return 0;
    }
    return id < 0 || games_find(id) != NULL;
}

static int room_can_post(const char *room, const char *username)
{
    struct FriskGame *game;
    int id;

    if (!room_parse(room, &id)) {
        return 0;
    }
    if (id < 0) {
        return 1;
    }
    game = games_find(id);
    return game != NULL && game_find_seat(&game->game, username) >= 0;
}

static int room_path(const char *room, char *out, size_t out_size)
{
    int written;

    written = snprintf(out, out_size, "%s/chat/%s.log",
                       frisk_data_dir(), room);
    return written > 0 && (size_t)written < out_size;
}

static struct ChatRoomState *room_state(const char *room)
{
    struct ChatRoomState *oldest;
    int i;

    for (i = 0; i < room_count; i++) {
        if (strcmp(rooms[i].room, room) == 0) {
            rooms[i].last_used = now_seconds();
            return &rooms[i];
        }
    }
    if (room_count < CHAT_MAX_ROOMS) {
        oldest = &rooms[room_count++];
    } else {
        oldest = &rooms[0];
        for (i = 1; i < room_count; i++) {
            if (rooms[i].last_used < oldest->last_used) {
                oldest = &rooms[i];
            }
        }
    }
    memset(oldest, 0, sizeof(*oldest));
    snprintf(oldest->room, sizeof(oldest->room), "%s", room);
    oldest->last_used = now_seconds();
    return oldest;
}

static const char *find_key(const char *line, const char *key,
                            size_t *key_length)
{
    static char pattern[64];
    int written;

    written = snprintf(pattern, sizeof(pattern), "\"%s\":", key);
    if (written < 0 || (size_t)written >= sizeof(pattern)) {
        return NULL;
    }
    if (key_length != NULL) {
        *key_length = (size_t)written;
    }
    return strstr(line, pattern);
}

static int line_int(const char *line, const char *key, long *out)
{
    size_t key_length;
    const char *p = find_key(line, key, &key_length);
    char *end;

    if (p == NULL) {
        return 0;
    }
    p += key_length;
    if (!isdigit((unsigned char)*p) && *p != '-') {
        return 0;
    }
    *out = strtol(p, &end, 10);
    return end != p;
}

static int append_codepoint(char *out, size_t out_size, size_t *used,
                            unsigned int codepoint)
{
    if (codepoint < 0x80) {
        if (*used + 1 >= out_size) {
            return 0;
        }
        out[(*used)++] = (char)codepoint;
    } else if (codepoint < 0x800) {
        if (*used + 2 >= out_size) {
            return 0;
        }
        out[(*used)++] = (char)(0xC0 | (codepoint >> 6));
        out[(*used)++] = (char)(0x80 | (codepoint & 0x3F));
    } else if (codepoint < 0x10000) {
        if (*used + 3 >= out_size) {
            return 0;
        }
        out[(*used)++] = (char)(0xE0 | (codepoint >> 12));
        out[(*used)++] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
        out[(*used)++] = (char)(0x80 | (codepoint & 0x3F));
    } else if (codepoint <= 0x10FFFF) {
        if (*used + 4 >= out_size) {
            return 0;
        }
        out[(*used)++] = (char)(0xF0 | (codepoint >> 18));
        out[(*used)++] = (char)(0x80 | ((codepoint >> 12) & 0x3F));
        out[(*used)++] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
        out[(*used)++] = (char)(0x80 | (codepoint & 0x3F));
    } else {
        return 0;
    }
    return 1;
}

static int hex_value(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

static int line_string(const char *line, const char *key,
                       char *out, size_t out_size)
{
    static char pattern[64];
    const char *p;
    size_t used = 0;
    int written;

    written = snprintf(pattern, sizeof(pattern), "\"%s\":\"", key);
    if (written < 0 || (size_t)written >= sizeof(pattern)) {
        return 0;
    }
    p = strstr(line, pattern);
    if (p == NULL) {
        return 0;
    }
    p += written;
    while (*p != '\0' && *p != '"') {
        unsigned int codepoint;
        if (*p == '\\') {
            p++;
            switch (*p) {
            case '"': codepoint = '"'; p++; break;
            case '\\': codepoint = '\\'; p++; break;
            case '/': codepoint = '/'; p++; break;
            case 'b': codepoint = '\b'; p++; break;
            case 'f': codepoint = '\f'; p++; break;
            case 'n': codepoint = '\n'; p++; break;
            case 'r': codepoint = '\r'; p++; break;
            case 't': codepoint = '\t'; p++; break;
            case 'u':
                if (hex_value(p[1]) < 0 || hex_value(p[2]) < 0 ||
                    hex_value(p[3]) < 0 || hex_value(p[4]) < 0) {
                    return 0;
                }
                codepoint = (unsigned int)((hex_value(p[1]) << 12) |
                                           (hex_value(p[2]) << 8) |
                                           (hex_value(p[3]) << 4) |
                                           hex_value(p[4]));
                p += 5;
                break;
            default:
                return 0;
            }
        } else {
            codepoint = (unsigned char)*p++;
        }
        if (!append_codepoint(out, out_size, &used, codepoint)) {
            return 0;
        }
    }
    if (*p != '"' || used + 1 > out_size) {
        return 0;
    }
    out[used] = '\0';
    return 1;
}

static int utf8_valid(const char *text, size_t length)
{
    const unsigned char *p = (const unsigned char *)text;
    size_t i = 0;

    while (i < length) {
        unsigned char c = p[i++];
        unsigned int need;
        unsigned int value;
        unsigned int minimum;
        unsigned int j;

        if (c < 0x80) {
            continue;
        }
        if ((c & 0xE0) == 0xC0) {
            need = 1;
            value = c & 0x1F;
            minimum = 0x80;
        } else if ((c & 0xF0) == 0xE0) {
            need = 2;
            value = c & 0x0F;
            minimum = 0x800;
        } else if ((c & 0xF8) == 0xF0) {
            need = 3;
            value = c & 0x07;
            minimum = 0x10000;
        } else {
            return 0;
        }
        if (i + need > length) {
            return 0;
        }
        for (j = 0; j < need; j++) {
            unsigned char next = p[i++];
            if ((next & 0xC0) != 0x80) {
                return 0;
            }
            value = (value << 6) | (next & 0x3F);
        }
        if (value < minimum || value > 0x10FFFF ||
            (value >= 0xD800 && value <= 0xDFFF)) {
            return 0;
        }
    }
    return 1;
}

static int sanitize_text(const char *raw, unsigned int raw_length,
                         char *out, size_t out_size)
{
    size_t used = 0;
    unsigned int i;

    if (raw == NULL || raw_length == 0 || raw_length > CHAT_TEXT_MAX) {
        return 0;
    }
    for (i = 0; i < raw_length; i++) {
        unsigned char c = (unsigned char)raw[i];
        if (c == 0 || c < 0x20 || c == 0x7F) {
            continue;
        }
        if (used + 1 >= out_size) {
            return 0;
        }
        out[used++] = (char)c;
    }
    out[used] = '\0';
    if (used == 0) {
        return 0;
    }
    return utf8_valid(out, used);
}

static int clip_extension_ok(const char *extension)
{
    static const char *const allowed[] = {
        "ogg", "weba", "wav", "m4a", "mp3", "amr"
    };
    size_t i;

    for (i = 0; i < sizeof(allowed) / sizeof(allowed[0]); i++) {
        if (strcmp(extension, allowed[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

static int clip_name_valid(const char *name)
{
    const char *dot;
    const char *p;
    size_t length;

    if (name == NULL) {
        return 0;
    }
    length = strlen(name);
    if (length < 3 || length >= CHAT_CLIP_NAME_SIZE) {
        return 0;
    }
    dot = strchr(name, '.');
    if (dot == NULL || dot == name || strchr(dot + 1, '.') != NULL) {
        return 0;
    }
    for (p = name; p < dot; p++) {
        if (!(isalnum((unsigned char)*p) || *p == '_' || *p == '-')) {
            return 0;
        }
    }
    return clip_extension_ok(dot + 1);
}

static int clip_path(const char *name, char *out, size_t out_size)
{
    int written;

    if (!clip_name_valid(name)) {
        return 0;
    }
    written = snprintf(out, out_size, "%s/%s", clips_dir, name);
    return written > 0 && (size_t)written < out_size;
}

static const char *sniff_clip(const unsigned char *bytes, unsigned int size)
{
    if (bytes == NULL) {
        return NULL;
    }
    if (size >= 4 && memcmp(bytes, "OggS", 4) == 0) {
        return "ogg";
    }
    if (size >= 4 && bytes[0] == 0x1A && bytes[1] == 0x45 &&
        bytes[2] == 0xDF && bytes[3] == 0xA3) {
        return "weba";
    }
    if (size >= 12 && memcmp(bytes, "RIFF", 4) == 0 &&
        memcmp(bytes + 8, "WAVE", 4) == 0) {
        return "wav";
    }
    if (size >= 12 && memcmp(bytes + 4, "ftyp", 4) == 0) {
        return "m4a";
    }
    if (size >= 3 && memcmp(bytes, "ID3", 3) == 0) {
        return "mp3";
    }
    if (size >= 2 && bytes[0] == 0xFF && (bytes[1] & 0xE0) == 0xE0) {
        return "mp3";
    }
    if (size >= 5 && memcmp(bytes, "#!AMR", 5) == 0) {
        return "amr";
    }
    return NULL;
}

static int append_line(const char *room, const char *line)
{
    char path[FRISK_PATH_SIZE];
    char error[256];
    FILE *file;
    size_t length = strlen(line);

    if (!room_path(room, path, sizeof(path))) {
        return 0;
    }
    file = fopen(path, "ab");
    if (file == NULL) {
        if (store_ensure_parent(path, error, sizeof(error)) == 0) {
            file = fopen(path, "ab");
        }
    }
    if (file == NULL) {
        AmmServer_Warning("frisk chat: cannot open %s: %s",
                          path, strerror(errno));
        return 0;
    }
    if (fwrite(line, 1, length, file) != length || fflush(file) != 0 ||
        fsync(fileno(file)) != 0) {
        AmmServer_Warning("frisk chat: cannot append to %s: %s",
                          path, strerror(errno));
        fclose(file);
        return 0;
    }
    if (fclose(file) != 0) {
        return 0;
    }
    return 1;
}

static int append_text_line(const char *room, const char *user,
                            const char *kind, const char *text)
{
    char escaped_text[CHAT_TEXT_ESCAPED];
    char escaped_user[128];
    char escaped_kind[32];
    char line[CHAT_LINE_SIZE];
    struct ChatRoomState *state;
    unsigned int text_length;
    unsigned int user_length;
    unsigned int kind_length;
    long id;
    int written;

    if (!chat_ready || room == NULL || user == NULL || kind == NULL ||
        text == NULL) {
        return 0;
    }
    text_length = (unsigned int)strlen(text);
    user_length = (unsigned int)strlen(user);
    kind_length = (unsigned int)strlen(kind);
    if (text_length == 0 || text_length > CHAT_TEXT_MAX ||
        user_length == 0 || user_length >= sizeof(escaped_user) ||
        kind_length == 0 || kind_length >= sizeof(escaped_kind)) {
        return 0;
    }
    {
        unsigned int escaped_length;
        escaped_length = AmmServer_JSONEscape(text, text_length,
                                              escaped_text,
                                              sizeof(escaped_text) - 1);
        if (escaped_length == 0) {
            return 0;
        }
        escaped_text[escaped_length] = '\0';
        escaped_length = AmmServer_JSONEscape(user, user_length,
                                              escaped_user,
                                              sizeof(escaped_user) - 1);
        if (escaped_length == 0) {
            return 0;
        }
        escaped_user[escaped_length] = '\0';
        escaped_length = AmmServer_JSONEscape(kind, kind_length,
                                              escaped_kind,
                                              sizeof(escaped_kind) - 1);
        if (escaped_length == 0) {
            return 0;
        }
        escaped_kind[escaped_length] = '\0';
    }
    state = room_state(room);
    id = state->last_id + 1;
    written = snprintf(line, sizeof(line),
                       "{\"id\":%ld,\"t\":%ld,\"user\":\"%s\","
                       "\"kind\":\"%s\",\"text\":\"%s\"}\n",
                       id, now_seconds(), escaped_user, escaped_kind,
                       escaped_text);
    if (written < 0 || (size_t)written >= sizeof(line) ||
        !append_line(room, line)) {
        return 0;
    }
    state->last_id = id;
    state->loaded = 1;
    return 1;
}

static int append_taunt_line(const char *room, const char *user, int index)
{
    char escaped_user[128];
    char line[512];
    struct ChatRoomState *state;
    unsigned int user_length;
    long id;
    int written;

    if (!chat_ready || user == NULL || index < 0 ||
        index >= FRISK_TAUNT_COUNT) {
        return 0;
    }
    user_length = (unsigned int)strlen(user);
    if (user_length == 0 || user_length >= sizeof(escaped_user)) {
        return 0;
    }
    {
        unsigned int escaped_length = AmmServer_JSONEscape(
            user, user_length, escaped_user, sizeof(escaped_user) - 1);
        if (escaped_length == 0) {
            return 0;
        }
        escaped_user[escaped_length] = '\0';
    }
    state = room_state(room);
    id = state->last_id + 1;
    written = snprintf(line, sizeof(line),
                       "{\"id\":%ld,\"t\":%ld,\"user\":\"%s\","
                       "\"kind\":\"taunt\",\"index\":%d}\n",
                       id, now_seconds(), escaped_user, index);
    if (written < 0 || (size_t)written >= sizeof(line) ||
        !append_line(room, line)) {
        return 0;
    }
    state->last_id = id;
    state->loaded = 1;
    return 1;
}

static int append_clip_line(const char *room, const char *user,
                            const char *file, int duration_ms,
                            unsigned int size)
{
    char escaped_user[128];
    char line[512];
    struct ChatRoomState *state;
    unsigned int user_length;
    long id;
    int written;

    if (!chat_ready || user == NULL || !clip_name_valid(file)) {
        return 0;
    }
    user_length = (unsigned int)strlen(user);
    if (user_length == 0 || user_length >= sizeof(escaped_user)) {
        return 0;
    }
    {
        unsigned int escaped_length = AmmServer_JSONEscape(
            user, user_length, escaped_user, sizeof(escaped_user) - 1);
        if (escaped_length == 0) {
            return 0;
        }
        escaped_user[escaped_length] = '\0';
    }
    state = room_state(room);
    id = state->last_id + 1;
    written = snprintf(line, sizeof(line),
                       "{\"id\":%ld,\"t\":%ld,\"user\":\"%s\","
                       "\"kind\":\"clip\",\"file\":\"%s\","
                       "\"dur\":%d,\"size\":%u}\n",
                       id, now_seconds(), escaped_user, file,
                       duration_ms, size);
    if (written < 0 || (size_t)written >= sizeof(line) ||
        !append_line(room, line)) {
        return 0;
    }
    state->last_id = id;
    state->loaded = 1;
    return 1;
}

static void load_room_last(struct ChatRoomState *state)
{
    char path[FRISK_PATH_SIZE];
    char line[CHAT_LINE_SIZE];
    FILE *file;
    long id;
    long maximum = 0;

    if (state->loaded) {
        return;
    }
    state->loaded = 1;
    if (!room_path(state->room, path, sizeof(path))) {
        return;
    }
    file = fopen(path, "rb");
    if (file == NULL) {
        return;
    }
    while (fgets(line, sizeof(line), file) != NULL) {
        if (line_int(line, "id", &id) && id > maximum) {
            maximum = id;
        }
    }
    fclose(file);
    state->last_id = maximum;
}

static int message_from_line(const char *line, struct ChatMessage *message)
{
    char kind[8];
    long id;
    long timestamp;

    memset(message, 0, sizeof(*message));
    if (!line_int(line, "id", &id) || id <= 0 ||
        !line_int(line, "t", &timestamp) ||
        !line_string(line, "user", message->user, sizeof(message->user)) ||
        !line_string(line, "kind", kind, sizeof(kind))) {
        return 0;
    }
    message->id = id;
    message->time = timestamp;
    snprintf(message->kind, sizeof(message->kind), "%s", kind);
    if (strcmp(kind, "text") == 0 || strcmp(kind, "system") == 0) {
        return line_string(line, "text", message->text,
                           sizeof(message->text));
    }
    if (strcmp(kind, "clip") == 0) {
        long duration = 0;
        long size = 0;
        if (!line_string(line, "file", message->file,
                         sizeof(message->file)) ||
            !clip_name_valid(message->file)) {
            return 0;
        }
        line_int(line, "dur", &duration);
        line_int(line, "size", &size);
        message->duration_ms = (duration > 0 && duration <= 600000)
            ? (int)duration : 0;
        message->size = (size > 0 && size <= (long)FRISK_MAX_CLIP_BYTES)
            ? (int)size : 0;
        return 1;
    }
    if (strcmp(kind, "taunt") == 0) {
        long index = -1;
        if (!line_int(line, "index", &index) || index < 0 ||
            index >= FRISK_TAUNT_COUNT) {
            return 0;
        }
        message->index = (int)index;
        return 1;
    }
    return 0;
}

/* Reads a room's log.  For after==0 the last `limit` messages are returned
 * (ring order fixed up by the caller); otherwise the first messages newer
 * than `after` are returned. */
static int read_messages(const char *room, unsigned int after,
                         unsigned int limit, struct ChatRoomState *state,
                         struct ChatMessage **out, int *out_start,
                         int *out_count, long *out_last)
{
    char path[FRISK_PATH_SIZE];
    char line[CHAT_LINE_SIZE];
    struct ChatMessage *messages;
    struct stat info;
    FILE *file;
    unsigned long total = 0;
    int count = 0;
    long maximum = state->last_id;

    *out = NULL;
    *out_start = 0;
    *out_count = 0;
    *out_last = after;
    if (!room_path(room, path, sizeof(path))) {
        return -1;
    }
    if (stat(path, &info) != 0) {
        *out_last = state->last_id;
        return 0;
    }
    messages = (struct ChatMessage *)calloc(limit, sizeof(*messages));
    if (messages == NULL) {
        return -1;
    }
    file = fopen(path, "rb");
    if (file == NULL) {
        free(messages);
        return -1;
    }
    while (fgets(line, sizeof(line), file) != NULL) {
        struct ChatMessage message;
        size_t length = strlen(line);

        if (length == sizeof(line) - 1 && line[length - 1] != '\n') {
            int c;
            while ((c = fgetc(file)) != EOF && c != '\n') {
                /* discard an over-long line */
            }
        }
        if (!message_from_line(line, &message)) {
            long id;
            if (line_int(line, "id", &id) && id > maximum) {
                maximum = id;
            }
            continue;
        }
        if (message.id > maximum) {
            maximum = message.id;
        }
        if (after == 0) {
            messages[total % limit] = message;
            total++;
        } else if (message.id > (long)after && count < (int)limit) {
            messages[count++] = message;
        }
    }
    if (ferror(file)) {
        fclose(file);
        free(messages);
        return -1;
    }
    fclose(file);
    if (maximum > state->last_id) {
        state->last_id = maximum;
    }
    if (after == 0) {
        if (total > limit) {
            *out_start = (int)(total % (unsigned long)limit);
            *out_count = (int)limit;
        } else {
            *out_start = 0;
            *out_count = (int)total;
        }
        *out_last = maximum;
    } else {
        *out_start = 0;
        *out_count = count;
        if (count > 0 && (unsigned int)count == limit) {
            *out_last = messages[count - 1].id;
        } else if (maximum > (long)after) {
            *out_last = maximum;
        }
    }
    *out = messages;
    return 0;
}

static struct ChatRate *rate_entry(const char *username)
{
    int i;
    int free_slot = -1;
    time_t oldest = 0;

    for (i = 0; i < CHAT_RATE_TABLE; i++) {
        if (rates[i].used && strcmp(rates[i].user, username) == 0) {
            return &rates[i];
        }
        if (!rates[i].used && free_slot < 0) {
            free_slot = i;
        }
    }
    if (free_slot >= 0) {
        memset(&rates[free_slot], 0, sizeof(rates[free_slot]));
        snprintf(rates[free_slot].user, sizeof(rates[free_slot].user),
                 "%s", username);
        rates[free_slot].used = 1;
        return &rates[free_slot];
    }
    /* A busy day with more distinct posters than the table holds: reuse the
     * entry that has been idle longest.  Rate limits are best-effort. */
    for (i = 1; i < CHAT_RATE_TABLE; i++) {
        time_t last = rates[i].last_text > rates[i].last_clip
            ? rates[i].last_text : rates[i].last_clip;
        if (last < oldest || i == 1) {
            oldest = last;
            free_slot = i;
        }
    }
    memset(&rates[free_slot], 0, sizeof(rates[free_slot]));
    snprintf(rates[free_slot].user, sizeof(rates[free_slot].user),
             "%s", username);
    rates[free_slot].used = 1;
    return &rates[free_slot];
}

static int text_rate_ok(const char *username)
{
    struct ChatRate *entry = rate_entry(username);
    time_t now = now_seconds();

    if (entry->last_text != 0 && now - entry->last_text < 1) {
        return 0;
    }
    if (entry->text_window == 0 || now - entry->text_window >= 60) {
        entry->text_window = now;
        entry->text_count = 0;
    }
    if (entry->text_count >= CHAT_RATE_PER_MINUTE) {
        return 0;
    }
    entry->last_text = now;
    entry->text_count++;
    return 1;
}

static int clip_rate_ok(const char *username)
{
    struct ChatRate *entry = rate_entry(username);
    time_t now = now_seconds();

    if (entry->last_clip != 0 && now - entry->last_clip < CHAT_CLIP_SECONDS) {
        return 0;
    }
    if (entry->clip_window == 0 || now - entry->clip_window >= 86400) {
        entry->clip_window = now;
        entry->clip_count = 0;
    }
    if (entry->clip_count >= CHAT_CLIPS_PER_DAY) {
        return 0;
    }
    entry->last_clip = now;
    entry->clip_count++;
    return 1;
}

void chat_system_game(struct FriskGame *game, const char *text)
{
    char room[CHAT_ROOM_SIZE];

    if (!chat_ready || game == NULL || !game->used || text == NULL ||
        text[0] == '\0') {
        return;
    }
    snprintf(room, sizeof(room), "g%d", game->game.id);
    if (!append_text_line(room, "system", "system", text)) {
        AmmServer_Warning("frisk chat: could not append a system line to %s",
                          room);
    }
}

void chat_system_events(struct FriskGame *game, const struct Event *events,
                        int count)
{
    int i;

    if (!chat_ready || game == NULL || events == NULL) {
        return;
    }
    for (i = 0; i < count; i++) {
        switch (events[i].kind) {
        case EVENT_START:
        case EVENT_TURN_BEGIN:
        case EVENT_ELIMINATED:
        case EVENT_GAME_OVER:
        case EVENT_KICK:
        case EVENT_RESIGN:
            chat_system_game(game, events[i].text);
            break;
        default:
            break;
        }
    }
}

void *chat_tail_callback(struct AmmServer_DynamicRequest *rqst)
{
    char username[32];
    char room[CHAT_ROOM_SIZE];
    unsigned int after = 0;
    unsigned int limit = CHAT_DEFAULT_LIMIT;
    struct ChatRoomState *state;
    struct ChatMessage *messages = NULL;
    struct FriskJson json;
    int game_id;
    int start = 0;
    int count = 0;
    int i;
    long last = 0;

    pthread_mutex_lock(&friskLock);
    if (!frisk_require_user(rqst, username, sizeof(username))) {
        pthread_mutex_unlock(&friskLock);
        return 0;
    }
    if (!frisk_param_str(rqst, "room", room, sizeof(room))) {
        frisk_json_error(rqst, 400, "room");
        pthread_mutex_unlock(&friskLock);
        return 0;
    }
    if (!room_parse(room, &game_id)) {
        frisk_json_error(rqst, 400, "room");
        pthread_mutex_unlock(&friskLock);
        return 0;
    }
    if (game_id >= 0 && games_find(game_id) == NULL) {
        frisk_json_error(rqst, 404, "notfound");
        pthread_mutex_unlock(&friskLock);
        return 0;
    }
    if (_GETexists(rqst, "after") &&
        !frisk_param_uint(rqst, "after", 0, 2000000000u, &after)) {
        frisk_json_error(rqst, 400, "params");
        pthread_mutex_unlock(&friskLock);
        return 0;
    }
    if (_GETexists(rqst, "limit") &&
        !frisk_param_uint(rqst, "limit", 1, CHAT_MAX_LIMIT, &limit)) {
        frisk_json_error(rqst, 400, "params");
        pthread_mutex_unlock(&friskLock);
        return 0;
    }
    state = room_state(room);
    load_room_last(state);
    if (read_messages(room, after, limit, state, &messages, &start,
                      &count, &last) != 0) {
        frisk_json_error(rqst, 500, "log");
        pthread_mutex_unlock(&friskLock);
        return 0;
    }

    frisk_json_begin(&json, rqst);
    frisk_json_key_int(&json, "ok", 1);
    frisk_json_key_str(&json, "room", room);
    frisk_json_key_int(&json, "last", last);
    frisk_json_key_int(&json, "taunts", FRISK_TAUNT_COUNT);
    frisk_json_key_array(&json, "messages");
    for (i = 0; i < count; i++) {
        const struct ChatMessage *message =
            &messages[(start + i) % (int)limit];
        frisk_json_array_object(&json);
        frisk_json_key_int(&json, "id", message->id);
        frisk_json_key_int(&json, "t", message->time);
        frisk_json_key_str(&json, "user", message->user);
        frisk_json_key_str(&json, "kind", message->kind);
        if (strcmp(message->kind, "text") == 0 ||
            strcmp(message->kind, "system") == 0) {
            frisk_json_key_str(&json, "text", message->text);
        } else if (strcmp(message->kind, "clip") == 0) {
            frisk_json_key_str(&json, "file", message->file);
            frisk_json_key_int(&json, "dur", message->duration_ms);
            frisk_json_key_int(&json, "size", message->size);
        } else if (strcmp(message->kind, "taunt") == 0) {
            frisk_json_key_int(&json, "index", message->index);
        }
        frisk_json_object_end(&json);
    }
    frisk_json_array_end(&json);
    frisk_json_end(&json);
    free(messages);
    pthread_mutex_unlock(&friskLock);
    return 0;
}

void *chat_say_callback(struct AmmServer_DynamicRequest *rqst)
{
    char username[32];
    char room[CHAT_ROOM_SIZE];
    char text[CHAT_TEXT_MAX + 1];
    const char *raw;
    unsigned int raw_length = 0;
    unsigned int taunt = 0;
    struct ChatRoomState *state;
    struct FriskJson json;
    long id;

    pthread_mutex_lock(&friskLock);
    if (!frisk_require_user(rqst, username, sizeof(username)) ||
        !frisk_require_csrf(rqst)) {
        pthread_mutex_unlock(&friskLock);
        return 0;
    }
    if (!frisk_post_str(rqst, "room", room, sizeof(room))) {
        frisk_json_error(rqst, 400, "room");
        pthread_mutex_unlock(&friskLock);
        return 0;
    }
    if (!room_exists(room)) {
        frisk_json_error(rqst, 404, "notfound");
        pthread_mutex_unlock(&friskLock);
        return 0;
    }
    if (!room_can_post(room, username)) {
        frisk_json_error(rqst, 403, "seat");
        pthread_mutex_unlock(&friskLock);
        return 0;
    }
    if (_POSTexists(rqst, "taunt")) {
        if (!frisk_post_uint(rqst, "taunt", 0, FRISK_TAUNT_COUNT - 1,
                             &taunt)) {
            frisk_json_error(rqst, 400, "taunt");
            pthread_mutex_unlock(&friskLock);
            return 0;
        }
        if (!text_rate_ok(username)) {
            frisk_json_error(rqst, 429, "rate");
            pthread_mutex_unlock(&friskLock);
            return 0;
        }
        state = room_state(room);
        load_room_last(state);
        if (!append_taunt_line(room, username, (int)taunt)) {
            frisk_json_error(rqst, 500, "save");
            pthread_mutex_unlock(&friskLock);
            return 0;
        }
        id = state->last_id;
    } else {
        raw = _POST(rqst, "text", &raw_length);
        if (!sanitize_text(raw, raw_length, text, sizeof(text))) {
            frisk_json_error(rqst, 400, "text");
            pthread_mutex_unlock(&friskLock);
            return 0;
        }
        if (!text_rate_ok(username)) {
            frisk_json_error(rqst, 429, "rate");
            pthread_mutex_unlock(&friskLock);
            return 0;
        }
        state = room_state(room);
        load_room_last(state);
        if (!append_text_line(room, username, "text", text)) {
            frisk_json_error(rqst, 500, "save");
            pthread_mutex_unlock(&friskLock);
            return 0;
        }
        id = state->last_id;
    }

    frisk_json_begin(&json, rqst);
    frisk_json_key_int(&json, "ok", 1);
    frisk_json_key_int(&json, "id", id);
    frisk_json_end(&json);
    pthread_mutex_unlock(&friskLock);
    return 0;
}

void *chat_clip_callback(struct AmmServer_DynamicRequest *rqst)
{
    char username[32];
    char room[CHAT_ROOM_SIZE];
    char token[32];
    char file[CHAT_CLIP_NAME_SIZE];
    char path[FRISK_PATH_SIZE];
    unsigned int size = 0;
    unsigned int duration = 0;
    const char *bytes;
    const char *extension;
    struct FriskJson json;
    long id;
    int written;

    pthread_mutex_lock(&friskLock);
    if (!frisk_require_user(rqst, username, sizeof(username)) ||
        !frisk_require_csrf(rqst)) {
        pthread_mutex_unlock(&friskLock);
        return 0;
    }
    if (!frisk_post_str(rqst, "room", room, sizeof(room))) {
        frisk_json_error(rqst, 400, "room");
        pthread_mutex_unlock(&friskLock);
        return 0;
    }
    if (!room_exists(room)) {
        frisk_json_error(rqst, 404, "notfound");
        pthread_mutex_unlock(&friskLock);
        return 0;
    }
    if (!room_can_post(room, username)) {
        frisk_json_error(rqst, 403, "seat");
        pthread_mutex_unlock(&friskLock);
        return 0;
    }
    if (_POSTexists(rqst, "dur") &&
        !frisk_post_uint(rqst, "dur", 0, 600000, &duration)) {
        frisk_json_error(rqst, 400, "params");
        pthread_mutex_unlock(&friskLock);
        return 0;
    }
    bytes = _FILES(rqst, "clip", VALUE, &size);
    if (bytes == NULL || size == 0) {
        frisk_json_error(rqst, 400, "clip");
        pthread_mutex_unlock(&friskLock);
        return 0;
    }
    if (size > FRISK_MAX_CLIP_BYTES) {
        frisk_json_error(rqst, 413, "toolarge");
        pthread_mutex_unlock(&friskLock);
        return 0;
    }
    extension = sniff_clip((const unsigned char *)bytes, size);
    if (extension == NULL) {
        frisk_json_error(rqst, 415, "clip");
        pthread_mutex_unlock(&friskLock);
        return 0;
    }
    if (!clip_rate_ok(username)) {
        frisk_json_error(rqst, 429, "rate");
        pthread_mutex_unlock(&friskLock);
        return 0;
    }
    if (!AmmServer_GenerateSecureToken(token, sizeof(token), 16)) {
        frisk_json_error(rqst, 500, "save");
        pthread_mutex_unlock(&friskLock);
        return 0;
    }
    written = snprintf(file, sizeof(file), "%s.%s", token, extension);
    if (written < 0 || (size_t)written >= sizeof(file) ||
        !clip_path(file, path, sizeof(path))) {
        frisk_json_error(rqst, 500, "save");
        pthread_mutex_unlock(&friskLock);
        return 0;
    }
    if (!AmmServer_WriteFileFromMemory(path, bytes, size)) {
        frisk_json_error(rqst, 500, "save");
        pthread_mutex_unlock(&friskLock);
        return 0;
    }
    if (!append_clip_line(room, username, file, (int)duration, size)) {
        remove(path);
        frisk_json_error(rqst, 500, "save");
        pthread_mutex_unlock(&friskLock);
        return 0;
    }
    id = room_state(room)->last_id;

    frisk_json_begin(&json, rqst);
    frisk_json_key_int(&json, "ok", 1);
    frisk_json_key_int(&json, "id", id);
    frisk_json_key_str(&json, "file", file);
    frisk_json_key_int(&json, "dur", duration);
    frisk_json_key_int(&json, "size", size);
    frisk_json_end(&json);
    pthread_mutex_unlock(&friskLock);
    return 0;
}

void *chat_clip_get_callback(struct AmmServer_DynamicRequest *rqst)
{
    char username[32];
    char name[CHAT_CLIP_NAME_SIZE];
    char path[FRISK_PATH_SIZE];

    pthread_mutex_lock(&friskLock);
    if (!frisk_require_user(rqst, username, sizeof(username))) {
        pthread_mutex_unlock(&friskLock);
        return 0;
    }
    if (!frisk_param_str(rqst, "f", name, sizeof(name)) ||
        !clip_name_valid(name)) {
        frisk_json_error(rqst, 400, "clip");
        pthread_mutex_unlock(&friskLock);
        return 0;
    }
    if (!clip_path(name, path, sizeof(path))) {
        frisk_json_error(rqst, 400, "clip");
        pthread_mutex_unlock(&friskLock);
        return 0;
    }
    pthread_mutex_unlock(&friskLock);

    if (access(path, R_OK) != 0) {
        frisk_json_error(rqst, 404, "clip");
        return 0;
    }
    if (!AmmServer_DynamicRequestReturnFile(rqst, path)) {
        frisk_json_error(rqst, 500, "clip");
    }
    return 0;
}

static int clip_ref_add(struct ChatClipRef **refs, size_t *count,
                        size_t *capacity, const char *name,
                        const char *room)
{
    size_t i;

    for (i = 0; i < *count; i++) {
        if (strcmp((*refs)[i].name, name) == 0) {
            return 1;
        }
    }
    if (*count == *capacity) {
        size_t next = *capacity == 0 ? 64 : *capacity * 2;
        struct ChatClipRef *grown = (struct ChatClipRef *)realloc(
            *refs, next * sizeof(**refs));
        if (grown == NULL) {
            return 0;
        }
        *refs = grown;
        *capacity = next;
    }
    snprintf((*refs)[*count].name, sizeof((*refs)[*count].name),
             "%s", name);
    snprintf((*refs)[*count].room, sizeof((*refs)[*count].room),
             "%s", room);
    (*count)++;
    return 1;
}

static const struct ChatClipRef *clip_ref_find(const struct ChatClipRef *refs,
                                               size_t count,
                                               const char *name)
{
    size_t i;

    for (i = 0; i < count; i++) {
        if (strcmp(refs[i].name, name) == 0) {
            return &refs[i];
        }
    }
    return NULL;
}

static int clip_file_compare(const void *a, const void *b)
{
    const struct ChatClipFile *left = (const struct ChatClipFile *)a;
    const struct ChatClipFile *right = (const struct ChatClipFile *)b;

    if (left->mtime != right->mtime) {
        return left->mtime < right->mtime ? -1 : 1;
    }
    return strcmp(left->name, right->name);
}

static void cap_clip_directory(void)
{
    DIR *directory;
    struct dirent *entry;
    struct ChatClipFile *files = NULL;
    size_t count = 0;
    size_t capacity = 0;
    unsigned long long total = 0;
    size_t i;

    if (clip_cap_bytes == 0) {
        return;
    }
    directory = opendir(clips_dir);
    if (directory == NULL) {
        return;
    }
    while ((entry = readdir(directory)) != NULL) {
        char path[FRISK_PATH_SIZE];
        struct stat info;

        if (!clip_name_valid(entry->d_name) ||
            strlen(entry->d_name) >= sizeof(files[count].name)) {
            continue;
        }
        if (snprintf(path, sizeof(path), "%s/%s", clips_dir,
                     entry->d_name) >= (int)sizeof(path) ||
            stat(path, &info) != 0 || !S_ISREG(info.st_mode)) {
            continue;
        }
        if (count == capacity) {
            size_t next = capacity == 0 ? 64 : capacity * 2;
            struct ChatClipFile *grown = (struct ChatClipFile *)realloc(
                files, next * sizeof(*files));
            if (grown == NULL) {
                closedir(directory);
                free(files);
                return;
            }
            files = grown;
            capacity = next;
        }
        snprintf(files[count].name, sizeof(files[count].name),
                 "%s", entry->d_name);
        files[count].size = (unsigned long long)info.st_size;
        files[count].mtime = info.st_mtime;
        total += files[count].size;
        count++;
    }
    closedir(directory);
    if (count > 1) {
        qsort(files, count, sizeof(*files), clip_file_compare);
    }
    for (i = 0; i < count && total > clip_cap_bytes; i++) {
        char path[FRISK_PATH_SIZE];
        if (snprintf(path, sizeof(path), "%s/%s", clips_dir,
                     files[i].name) >= (int)sizeof(path)) {
            continue;
        }
        if (remove(path) == 0) {
            total -= files[i].size;
        } else {
            AmmServer_Warning("frisk chat: could not prune clip %s", path);
        }
    }
    free(files);
}

static void housekeeping_once(void)
{
    DIR *directory;
    struct dirent *entry;
    struct ChatClipRef *refs = NULL;
    size_t ref_count = 0;
    size_t ref_capacity = 0;
    int ref_overflow = 0;
    time_t now = now_seconds();

    pthread_mutex_lock(&friskLock);
    directory = opendir(chat_dir);
    if (directory == NULL) {
        pthread_mutex_unlock(&friskLock);
        return;
    }
    while ((entry = readdir(directory)) != NULL) {
        char room[CHAT_ROOM_SIZE];
        char path[FRISK_PATH_SIZE];
        char line[CHAT_LINE_SIZE];
        char name[CHAT_CLIP_NAME_SIZE];
        FILE *file;
        size_t length;

        length = strlen(entry->d_name);
        if (length < 5 || strcmp(entry->d_name + length - 4, ".log") != 0) {
            continue;
        }
        if (length - 4 >= sizeof(room)) {
            continue;
        }
        memcpy(room, entry->d_name, length - 4);
        room[length - 4] = '\0';
        if (!room_parse(room, NULL)) {
            continue;
        }
        if (snprintf(path, sizeof(path), "%s/%s", chat_dir,
                     entry->d_name) >= (int)sizeof(path)) {
            continue;
        }
        file = fopen(path, "rb");
        if (file == NULL) {
            continue;
        }
        while (fgets(line, sizeof(line), file) != NULL) {
            if (strstr(line, "\"kind\":\"clip\"") == NULL) {
                continue;
            }
            if (!line_string(line, "file", name, sizeof(name)) ||
                !clip_name_valid(name)) {
                continue;
            }
            if (!clip_ref_add(&refs, &ref_count, &ref_capacity,
                              name, room)) {
                ref_overflow = 1;
            }
        }
        fclose(file);
    }
    closedir(directory);

    directory = opendir(clips_dir);
    if (directory == NULL) {
        free(refs);
        pthread_mutex_unlock(&friskLock);
        return;
    }
    while ((entry = readdir(directory)) != NULL) {
        char path[FRISK_PATH_SIZE];
        struct stat info;
        const struct ChatClipRef *ref;
        int game_id;

        if (!clip_name_valid(entry->d_name)) {
            continue;
        }
        if (snprintf(path, sizeof(path), "%s/%s", clips_dir,
                     entry->d_name) >= (int)sizeof(path) ||
            stat(path, &info) != 0 || !S_ISREG(info.st_mode)) {
            continue;
        }
        ref = clip_ref_find(refs, ref_count, entry->d_name);
        if (ref == NULL) {
            if (!ref_overflow && now - info.st_mtime > CHAT_ORPHAN_GRACE &&
                remove(path) != 0) {
                AmmServer_Warning("frisk chat: could not remove orphan %s",
                                  path);
            }
            continue;
        }
        if (strcmp(ref->room, "lobby") == 0) {
            continue;
        }
        if (!room_parse(ref->room, &game_id) || game_id < 0) {
            continue;
        }
        {
            struct FriskGame *game = games_find(game_id);
            if (game != NULL) {
                if (game->game.status == GAME_FINISHED &&
                    game->game.updated > 0 &&
                    now - game->game.updated > CHAT_RETENTION_SECONDS &&
                    remove(path) != 0) {
                    AmmServer_Warning("frisk chat: could not remove old clip %s",
                                      path);
                }
            } else if (now - info.st_mtime > CHAT_RETENTION_SECONDS &&
                       remove(path) != 0) {
                AmmServer_Warning("frisk chat: could not remove stale clip %s",
                                  path);
            }
        }
    }
    closedir(directory);
    free(refs);
    cap_clip_directory();
    pthread_mutex_unlock(&friskLock);
}

static void *housekeeping_main(void *unused)
{
    (void)unused;
    for (;;) {
        struct timespec deadline;
        int result;

        pthread_mutex_lock(&housekeeping_mutex);
        if (housekeeping_stop) {
            pthread_mutex_unlock(&housekeeping_mutex);
            break;
        }
        clock_gettime(CLOCK_REALTIME, &deadline);
        deadline.tv_sec += 3600;
        result = pthread_cond_timedwait(&housekeeping_cond,
                                        &housekeeping_mutex, &deadline);
        if (housekeeping_stop) {
            pthread_mutex_unlock(&housekeeping_mutex);
            break;
        }
        pthread_mutex_unlock(&housekeeping_mutex);
        if (result == ETIMEDOUT) {
            housekeeping_once();
        }
    }
    return NULL;
}

int chat_init(char *err, size_t err_size)
{
    const char *override = getenv("FRISK_CLIPS_DIR");
    const char *cap = getenv("FRISK_CLIP_CAP_MB");
    char error[256];

    if (override != NULL && override[0] != '\0') {
        snprintf(clips_dir, sizeof(clips_dir), "%s", override);
    } else {
        snprintf(clips_dir, sizeof(clips_dir), "%suploads/clips",
                 frisk_res_root());
    }
    snprintf(chat_dir, sizeof(chat_dir), "%s/chat", frisk_data_dir());
    if (store_ensure_dir(clips_dir, error, sizeof(error)) != 0) {
        if (err != NULL && err_size > 0) {
            snprintf(err, err_size, "%s", error);
        }
        return -1;
    }
    if (store_ensure_dir(chat_dir, error, sizeof(error)) != 0) {
        if (err != NULL && err_size > 0) {
            snprintf(err, err_size, "%s", error);
        }
        return -1;
    }
    clip_cap_bytes = (unsigned long long)CHAT_DEFAULT_CLIP_CAP_MB *
                     1024ULL * 1024ULL;
    if (cap != NULL && cap[0] != '\0') {
        char *end = NULL;
        unsigned long long megabytes = strtoull(cap, &end, 10);
        if (end != cap) {
            clip_cap_bytes = megabytes * 1024ULL * 1024ULL;
        }
    }
    chat_ready = 1;
    housekeeping_stop = 0;
    if (pthread_create(&housekeeping_thread, NULL, housekeeping_main,
                       NULL) == 0) {
        housekeeping_started = 1;
    } else {
        AmmServer_Warning("frisk chat: housekeeping thread was not started");
    }
    AmmServer_Warning("frisk chat: clips stored in %s", clips_dir);
    return 0;
}

const char *chat_clips_dir(void)
{
    return clips_dir;
}

void chat_shutdown(void)
{
    if (!housekeeping_started) {
        chat_ready = 0;
        return;
    }
    pthread_mutex_lock(&housekeeping_mutex);
    housekeeping_stop = 1;
    pthread_cond_broadcast(&housekeeping_cond);
    pthread_mutex_unlock(&housekeeping_mutex);
    pthread_join(housekeeping_thread, NULL);
    housekeeping_started = 0;
    chat_ready = 0;
}
