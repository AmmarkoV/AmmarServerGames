#include "accounts.h"

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "../../AmmServerlib/tools/http_tools.h"
#include "../../UserAccounts/sha256.h"
#include "../../UserAccounts/userAccountsWeb.h"
#include "engine/rules.h"
#include "frisk.h"
#include "games.h"
#include "http.h"
#include "store.h"

#define REMEMBER_DAYS 30
#define REMEMBER_SECONDS (REMEMBER_DAYS * 24 * 60 * 60)
#define INVITE_ALPHABET "ABCDEFGHJKLMNPQRSTUVWXYZ23456789"
#define INVITE_MAX_OUTSTANDING 5
#define RATE_WINDOW_SECONDS 600
#define RATE_MAX_FAILURES 5
#define RATE_TABLE 64

struct FriskRemember {
    char selector[64];
    char hash[65];
    char username[32];
    long expires;
    int used;
};

struct FriskAttempt {
    unsigned int client;
    int failures;
    time_t window;
};

static struct UserAccountDatabase *frisk_uadb;
static struct FriskProfile profiles[FRISK_MAX_PROFILES];
static int profile_count;
static struct FriskInvite invites[FRISK_MAX_INVITES];
static int invite_count;
static struct FriskRemember remembers[FRISK_MAX_REMEMBER];
static struct FriskAttempt attempts[RATE_TABLE];
static char profiles_path[FRISK_PATH_SIZE];
static char invites_path[FRISK_PATH_SIZE];
static char remember_path[FRISK_PATH_SIZE];
static time_t last_profiles_save;

static void set_error(char *err, size_t err_size, const char *fmt, ...)
{
    va_list ap;

    if (err == NULL || err_size == 0) {
        return;
    }
    va_start(ap, fmt);
    vsnprintf(err, err_size, fmt, ap);
    va_end(ap);
}

static long now_seconds(void)
{
    return (long)time(NULL);
}

static struct FriskProfile *find_profile(const char *username)
{
    int i;

    if (username == NULL) {
        return NULL;
    }
    for (i = 0; i < profile_count; i++) {
        if (profiles[i].used &&
            strcmp(profiles[i].username, username) == 0) {
            return &profiles[i];
        }
    }
    return NULL;
}

static struct FriskInvite *find_invite(const char *code)
{
    int i;

    if (code == NULL) {
        return NULL;
    }
    for (i = 0; i < invite_count; i++) {
        if (strcmp(invites[i].code, code) == 0) {
            return &invites[i];
        }
    }
    return NULL;
}

static int profiles_have_admin(void)
{
    int i;

    for (i = 0; i < profile_count; i++) {
        if (profiles[i].used && profiles[i].admin) {
            return 1;
        }
    }
    return 0;
}

static int save_profiles(void)
{
    size_t capacity = (size_t)FRISK_MAX_PROFILES * 256 + 64;
    char *buffer = (char *)malloc(capacity);
    size_t used = 0;
    int i;
    char error[256];

    if (buffer == NULL) {
        return 0;
    }
    for (i = 0; i < profile_count; i++) {
        const struct FriskProfile *p = &profiles[i];
        int written;
        if (!p->used) {
            continue;
        }
        written = snprintf(buffer + used, capacity - used,
                           "%s\t%d\t%s\t%s\t%d\t%d\t%d\t%d\t%d\t%d\t%ld\t%ld\n",
                           p->username, p->admin, p->colour, p->glyph,
                           p->games_played, p->wins, p->losses,
                           p->territories_taken, p->armies_killed,
                           p->armies_lost, p->created, p->last_seen);
        if (written < 0 || (size_t)written >= capacity - used) {
            free(buffer);
            return 0;
        }
        used += (size_t)written;
    }
    if (store_write_atomic(profiles_path, buffer, used,
                           error, sizeof(error)) != 0) {
        AmmServer_Warning("frisk: could not save profiles: %s", error);
        free(buffer);
        return 0;
    }
    free(buffer);
    last_profiles_save = time(NULL);
    return 1;
}

static int save_invites(void)
{
    size_t capacity = (size_t)FRISK_MAX_INVITES * 160 + 64;
    char *buffer = (char *)malloc(capacity);
    size_t used = 0;
    int i;
    char error[256];

    if (buffer == NULL) {
        return 0;
    }
    for (i = 0; i < invite_count; i++) {
        const struct FriskInvite *invite = &invites[i];
        int written = snprintf(buffer + used, capacity - used,
                               "%s\t%s\t%ld\t%s\t%ld\t%d\n",
                               invite->code, invite->creator, invite->created,
                               invite->used_by, invite->used_at,
                               invite->revoked);
        if (written < 0 || (size_t)written >= capacity - used) {
            free(buffer);
            return 0;
        }
        used += (size_t)written;
    }
    if (store_write_atomic(invites_path, buffer, used,
                           error, sizeof(error)) != 0) {
        AmmServer_Warning("frisk: could not save invites: %s", error);
        free(buffer);
        return 0;
    }
    free(buffer);
    return 1;
}

static int save_remembers(void)
{
    size_t capacity = (size_t)FRISK_MAX_REMEMBER * 200 + 64;
    char *buffer = (char *)malloc(capacity);
    size_t used = 0;
    int i;
    char error[256];

    if (buffer == NULL) {
        return 0;
    }
    for (i = 0; i < FRISK_MAX_REMEMBER; i++) {
        const struct FriskRemember *remember = &remembers[i];
        int written;
        if (!remember->used) {
            continue;
        }
        written = snprintf(buffer + used, capacity - used,
                           "%s\t%s\t%s\t%ld\n", remember->selector,
                           remember->hash, remember->username,
                           remember->expires);
        if (written < 0 || (size_t)written >= capacity - used) {
            free(buffer);
            return 0;
        }
        used += (size_t)written;
    }
    if (store_write_atomic(remember_path, buffer, used,
                           error, sizeof(error)) != 0) {
        AmmServer_Warning("frisk: could not save remember-me tokens: %s",
                          error);
        free(buffer);
        return 0;
    }
    free(buffer);
    return 1;
}

static char *next_field(char **cursor, char delimiter)
{
    char *field = *cursor;
    char *split;

    if (field == NULL) {
        return NULL;
    }
    split = strchr(field, delimiter);
    if (split != NULL) {
        *split = '\0';
        *cursor = split + 1;
    } else {
        *cursor = NULL;
    }
    return field;
}

static int load_profiles(void)
{
    char *data = NULL;
    size_t size = 0;
    char *line;
    char *lines_save = NULL;
    char error[256];
    int loaded = 0;

    if (access(profiles_path, F_OK) != 0) {
        return 0;
    }
    if (store_read_file(profiles_path, &data, &size,
                        error, sizeof(error)) != 0) {
        AmmServer_Warning("frisk: %s", error);
        return -1;
    }
    line = strtok_r(data, "\n", &lines_save);
    while (line != NULL && profile_count < FRISK_MAX_PROFILES) {
        char *fields[12];
        char *cursor = line;
        char *read;
        int i;
        if (*line == '\0' || *line == '#') {
            line = strtok_r(NULL, "\n", &lines_save);
            continue;
        }
        read = strrchr(line, '\r');
        if (read != NULL) {
            *read = '\0';
        }
        for (i = 0; i < 11; i++) {
            fields[i] = next_field(&cursor, '\t');
            if (fields[i] == NULL) {
                break;
            }
        }
        if (i < 11 || cursor == NULL) {
            line = strtok_r(NULL, "\n", &lines_save);
            continue;
        }
        fields[11] = cursor;
        if (strlen(fields[0]) >= sizeof(profiles[0].username) ||
            strlen(fields[2]) >= sizeof(profiles[0].colour) ||
            strlen(fields[3]) >= sizeof(profiles[0].glyph)) {
            line = strtok_r(NULL, "\n", &lines_save);
            continue;
        }
        if (find_profile(fields[0]) != NULL) {
            line = strtok_r(NULL, "\n", &lines_save);
            continue;
        }
        memset(&profiles[profile_count], 0,
               sizeof(profiles[profile_count]));
        snprintf(profiles[profile_count].username,
                 sizeof(profiles[profile_count].username), "%s", fields[0]);
        profiles[profile_count].admin = atoi(fields[1]);
        snprintf(profiles[profile_count].colour,
                 sizeof(profiles[profile_count].colour), "%s", fields[2]);
        snprintf(profiles[profile_count].glyph,
                 sizeof(profiles[profile_count].glyph), "%s", fields[3]);
        profiles[profile_count].games_played = atoi(fields[4]);
        profiles[profile_count].wins = atoi(fields[5]);
        profiles[profile_count].losses = atoi(fields[6]);
        profiles[profile_count].territories_taken = atoi(fields[7]);
        profiles[profile_count].armies_killed = atoi(fields[8]);
        profiles[profile_count].armies_lost = atoi(fields[9]);
        profiles[profile_count].created = atol(fields[10]);
        profiles[profile_count].last_seen = atol(fields[11]);
        profiles[profile_count].used = 1;
        profile_count++;
        loaded++;
        line = strtok_r(NULL, "\n", &lines_save);
    }
    free(data);
    return loaded;
}

static int load_invites(void)
{
    char *data = NULL;
    size_t size = 0;
    char *line;
    char *lines_save = NULL;
    char error[256];

    if (access(invites_path, F_OK) != 0) {
        return 0;
    }
    if (store_read_file(invites_path, &data, &size,
                        error, sizeof(error)) != 0) {
        AmmServer_Warning("frisk: %s", error);
        return -1;
    }
    line = strtok_r(data, "\n", &lines_save);
    while (line != NULL && invite_count < FRISK_MAX_INVITES) {
        char *fields[6];
        char *cursor = line;
        int i;
        char *read;
        if (*line == '\0' || *line == '#') {
            line = strtok_r(NULL, "\n", &lines_save);
            continue;
        }
        read = strrchr(line, '\r');
        if (read != NULL) {
            *read = '\0';
        }
        for (i = 0; i < 5; i++) {
            fields[i] = next_field(&cursor, '\t');
            if (fields[i] == NULL) {
                break;
            }
        }
        if (i < 5 || cursor == NULL) {
            line = strtok_r(NULL, "\n", &lines_save);
            continue;
        }
        fields[5] = cursor;
        if (strlen(fields[0]) >= sizeof(invites[0].code) ||
            strlen(fields[1]) >= sizeof(invites[0].creator) ||
            strlen(fields[3]) >= sizeof(invites[0].used_by) ||
            find_invite(fields[0]) != NULL) {
            line = strtok_r(NULL, "\n", &lines_save);
            continue;
        }
        memset(&invites[invite_count], 0, sizeof(invites[invite_count]));
        snprintf(invites[invite_count].code,
                 sizeof(invites[invite_count].code), "%s", fields[0]);
        snprintf(invites[invite_count].creator,
                 sizeof(invites[invite_count].creator), "%s", fields[1]);
        invites[invite_count].created = atol(fields[2]);
        snprintf(invites[invite_count].used_by,
                 sizeof(invites[invite_count].used_by), "%s", fields[3]);
        invites[invite_count].used_at = atol(fields[4]);
        invites[invite_count].revoked = atoi(fields[5]);
        invites[invite_count].used = fields[3][0] != '\0';
        invite_count++;
        line = strtok_r(NULL, "\n", &lines_save);
    }
    free(data);
    return 0;
}

static int load_remembers(void)
{
    char *data = NULL;
    size_t size = 0;
    char *line;
    char *lines_save = NULL;
    char error[256];
    long now = now_seconds();
    int count = 0;

    if (access(remember_path, F_OK) != 0) {
        return 0;
    }
    if (store_read_file(remember_path, &data, &size,
                        error, sizeof(error)) != 0) {
        AmmServer_Warning("frisk: %s", error);
        return -1;
    }
    line = strtok_r(data, "\n", &lines_save);
    while (line != NULL) {
        char *fields[4];
        char *cursor = line;
        int i;
        char *read;
        if (*line == '\0' || *line == '#') {
            line = strtok_r(NULL, "\n", &lines_save);
            continue;
        }
        read = strrchr(line, '\r');
        if (read != NULL) {
            *read = '\0';
        }
        for (i = 0; i < 3; i++) {
            fields[i] = next_field(&cursor, '\t');
            if (fields[i] == NULL) {
                break;
            }
        }
        if (i < 3 || cursor == NULL) {
            line = strtok_r(NULL, "\n", &lines_save);
            continue;
        }
        fields[3] = cursor;
        if (strlen(fields[0]) >= sizeof(remembers[0].selector) ||
            strlen(fields[1]) >= sizeof(remembers[0].hash) ||
            strlen(fields[2]) >= sizeof(remembers[0].username)) {
            line = strtok_r(NULL, "\n", &lines_save);
            continue;
        }
        if (atol(fields[3]) <= now) {
            line = strtok_r(NULL, "\n", &lines_save);
            continue;
        }
        for (i = 0; i < FRISK_MAX_REMEMBER; i++) {
            if (!remembers[i].used) {
                snprintf(remembers[i].selector,
                         sizeof(remembers[i].selector), "%s", fields[0]);
                snprintf(remembers[i].hash, sizeof(remembers[i].hash),
                         "%s", fields[1]);
                snprintf(remembers[i].username,
                         sizeof(remembers[i].username), "%s", fields[2]);
                remembers[i].expires = atol(fields[3]);
                remembers[i].used = 1;
                count++;
                break;
            }
        }
        line = strtok_r(NULL, "\n", &lines_save);
    }
    free(data);
    return count;
}

int accounts_init(char *err, size_t err_size)
{
    char error[256];

    snprintf(profiles_path, sizeof(profiles_path), "%s/profiles.db",
             frisk_data_dir());
    snprintf(invites_path, sizeof(invites_path), "%s/invites.db",
             frisk_data_dir());
    snprintf(remember_path, sizeof(remember_path), "%s/remember.db",
             frisk_data_dir());
    if (store_init(frisk_data_dir(), error, sizeof(error)) != 0) {
        set_error(err, err_size, "cannot prepare data directory: %s",
                  error);
        return -1;
    }
    if (load_profiles() < 0 || load_invites() < 0 ||
        load_remembers() < 0) {
        set_error(err, err_size, "cannot load account data from %s",
                  frisk_data_dir());
        return -1;
    }
    if (store_ensure_parent(frisk_users_db(), error, sizeof(error)) != 0) {
        set_error(err, err_size, "cannot prepare user database: %s", error);
        return -1;
    }
    frisk_uadb = uadb_initializeUserAccountDatabase(frisk_users_db());
    if (frisk_uadb == NULL) {
        set_error(err, err_size, "cannot open user database %s",
                  frisk_users_db());
        return -1;
    }
    AmmServer_Warning("frisk: %d profiles, %d invites, %d users",
                      profile_count, invite_count, frisk_uadb->userListSize);
    return 0;
}

void accounts_shutdown(void)
{
    save_profiles();
    save_invites();
    save_remembers();
    if (frisk_uadb != NULL) {
        uadb_closeUserAccountDatabase(&frisk_uadb);
        frisk_uadb = NULL;
    }
}

const struct FriskProfile *accounts_profile(const char *username)
{
    return find_profile(username);
}

int accounts_profile_count(void)
{
    return profile_count;
}

const struct FriskProfile *accounts_profile_at(int index)
{
    if (index < 0 || index >= profile_count || !profiles[index].used) {
        return NULL;
    }
    return &profiles[index];
}

int accounts_ensure_profile(const char *username, char *err,
                            size_t err_size)
{
    struct FriskProfile *profile;

    if (username == NULL || username[0] == '\0') {
        return -1;
    }
    profile = find_profile(username);
    if (profile != NULL) {
        return 0;
    }
    if (profile_count >= FRISK_MAX_PROFILES) {
        set_error(err, err_size, "profile table is full");
        return -1;
    }
    if (strlen(username) >= sizeof(profile->username)) {
        set_error(err, err_size, "user name is too long");
        return -1;
    }
    profile = &profiles[profile_count];
    memset(profile, 0, sizeof(*profile));
    snprintf(profile->username, sizeof(profile->username), "%s", username);
    snprintf(profile->colour, sizeof(profile->colour), "#d9b23a");
    snprintf(profile->glyph, sizeof(profile->glyph), "?");
    profile->admin = profiles_have_admin() ? 0 : 1;
    profile->created = now_seconds();
    profile->last_seen = profile->created;
    profile->used = 1;
    profile_count++;
    if (!save_profiles()) {
        set_error(err, err_size, "could not save profile");
        return -1;
    }
    return 0;
}

int accounts_is_admin(const char *username)
{
    const struct FriskProfile *profile = find_profile(username);
    return profile != NULL && profile->admin;
}

void accounts_touch(const char *username)
{
    struct FriskProfile *profile = find_profile(username);
    time_t now;

    if (profile == NULL) {
        return;
    }
    profile->last_seen = now_seconds();
    now = time(NULL);
    if (now - last_profiles_save >= 60) {
        save_profiles();
    }
}

void accounts_game_finished(struct Game *game, int winner, void *user)
{
    int i;

    (void)winner;
    (void)user;
    if (game == NULL) {
        return;
    }
    for (i = 0; i < game->player_count; i++) {
        const struct Seat *seat = &game->seats[i];
        struct FriskProfile *profile;
        if (seat->username[0] == '\0') {
            continue;
        }
        if (accounts_ensure_profile(seat->username, NULL, 0) != 0) {
            continue;
        }
        profile = find_profile(seat->username);
        if (profile == NULL) {
            continue;
        }
        profile->games_played += seat->stats.games_played;
        profile->wins += seat->stats.wins;
        profile->losses += seat->stats.losses;
        profile->territories_taken += seat->stats.territories_taken;
        profile->armies_killed += seat->stats.armies_killed;
        profile->armies_lost += seat->stats.armies_lost;
        profile->last_seen = now_seconds();
    }
    save_profiles();
}

/* ------------------------------------------------------------------ */
/* Rate limiting (clientID is the closest thing the request exposes   */
/* to an IP; the framework does not hand the peer address to dynamic  */
/* callbacks).                                                        */
/* ------------------------------------------------------------------ */

static struct FriskAttempt *attempt_for(struct AmmServer_DynamicRequest *rqst,
                                        int create)
{
    unsigned int client = rqst != NULL ? rqst->clientID : 0;
    int i;
    int free_slot = -1;

    for (i = 0; i < RATE_TABLE; i++) {
        if (attempts[i].client == client && attempts[i].window != 0) {
            return &attempts[i];
        }
        if (free_slot < 0 && attempts[i].window == 0) {
            free_slot = i;
        }
    }
    if (!create) {
        return NULL;
    }
    if (free_slot < 0) {
        free_slot = 0;
    }
    attempts[free_slot].client = client;
    attempts[free_slot].failures = 0;
    attempts[free_slot].window = time(NULL);
    return &attempts[free_slot];
}

static int rate_limited(struct AmmServer_DynamicRequest *rqst)
{
    struct FriskAttempt *attempt = attempt_for(rqst, 0);
    time_t now = time(NULL);

    if (attempt == NULL) {
        return 0;
    }
    if (now - attempt->window > RATE_WINDOW_SECONDS) {
        attempt->window = 0;
        attempt->failures = 0;
        return 0;
    }
    return attempt->failures >= RATE_MAX_FAILURES;
}

static void rate_fail(struct AmmServer_DynamicRequest *rqst)
{
    struct FriskAttempt *attempt = attempt_for(rqst, 1);
    time_t now = time(NULL);

    if (attempt == NULL) {
        return;
    }
    if (attempt->window == 0 || now - attempt->window > RATE_WINDOW_SECONDS) {
        attempt->window = now;
        attempt->failures = 0;
    }
    attempt->failures++;
    if (attempt->failures >= RATE_MAX_FAILURES) {
        AmmServer_SignalCountAsBadClientBehaviour(rqst);
    }
}

static void rate_clear(struct AmmServer_DynamicRequest *rqst)
{
    struct FriskAttempt *attempt = attempt_for(rqst, 0);
    if (attempt != NULL) {
        attempt->failures = 0;
        attempt->window = 0;
    }
}

/* ------------------------------------------------------------------ */
/* Remember-me                                                        */
/* ------------------------------------------------------------------ */

static int sha256_hex(const char *text, char out[65])
{
    char digest[65];
    SHA256_ToHex((const unsigned char *)text, (unsigned int)strlen(text),
                 digest, sizeof(digest));
    snprintf(out, 65, "%s", digest);
    return 1;
}

static int constant_time_equal(const char *a, const char *b)
{
    unsigned char diff = 0;
    size_t i;

    if (a == NULL || b == NULL || strlen(a) != strlen(b)) {
        return 0;
    }
    for (i = 0; i < strlen(a); i++) {
        diff |= (unsigned char)a[i] ^ (unsigned char)b[i];
    }
    return diff == 0;
}

static struct FriskRemember *find_remember(const char *selector)
{
    int i;

    for (i = 0; i < FRISK_MAX_REMEMBER; i++) {
        if (remembers[i].used &&
            strcmp(remembers[i].selector, selector) == 0) {
            return &remembers[i];
        }
    }
    return NULL;
}

static int user_uid(const char *username, unsigned int *uid)
{
    unsigned int i;

    if (frisk_uadb == NULL) {
        return 0;
    }
    for (i = 0; i < frisk_uadb->userListSize; i++) {
        if (strcmp(frisk_uadb->userList[i].username, username) == 0) {
            *uid = i;
            return 1;
        }
    }
    return 0;
}

static void set_session_user(struct AmmServer_DynamicRequest *rqst,
                             const char *username)
{
    char uid[16];

    unsigned int id = 0;
    _SESSIONset(rqst, "username", username);
    if (user_uid(username, &id)) {
        snprintf(uid, sizeof(uid), "%u", id);
        _SESSIONset(rqst, "uid", uid);
    }
}

void accounts_remember_issue(struct AmmServer_DynamicRequest *rqst,
                             const char *username)
{
    char selector[64];
    char validator[96];
    char hash[65];
    char cookie[192];
    struct FriskRemember *slot = NULL;
    long now = now_seconds();
    int i;

    if (rqst == NULL || username == NULL || username[0] == '\0') {
        return;
    }
    for (i = 0; i < FRISK_MAX_REMEMBER; i++) {
        if (!remembers[i].used || remembers[i].expires <= now) {
            slot = &remembers[i];
            break;
        }
    }
    if (slot == NULL) {
        slot = &remembers[0];
    }
    if (!AmmServer_GenerateSecureToken(selector, sizeof(selector), 16) ||
        !AmmServer_GenerateSecureToken(validator, sizeof(validator), 32)) {
        return;
    }
    sha256_hex(validator, hash);
    memset(slot, 0, sizeof(*slot));
    snprintf(slot->selector, sizeof(slot->selector), "%s", selector);
    snprintf(slot->hash, sizeof(slot->hash), "%s", hash);
    snprintf(slot->username, sizeof(slot->username), "%s", username);
    slot->expires = now + REMEMBER_SECONDS;
    slot->used = 1;
    save_remembers();
    snprintf(cookie, sizeof(cookie), "%s.%s", selector, validator);
    AmmServer_SetCookie(rqst, "frisk_rm", cookie,
                        REMEMBER_DAYS * 24 * 60 * 60, 1);
}

int accounts_remember_login(struct AmmServer_DynamicRequest *rqst,
                            char *out_username, size_t out_size)
{
    char cookie[192];
    char validator[96];
    char hash[65];
    char *dot;
    struct FriskRemember *remember;
    long now = now_seconds();
    char rotated[96];
    char rotated_hash[65];
    char rotated_cookie[192];

    if (rqst == NULL || out_username == NULL || out_size == 0) {
        return 0;
    }
    if (!_COOKIEcpy(rqst, "frisk_rm", cookie, sizeof(cookie))) {
        return 0;
    }
    dot = strchr(cookie, '.');
    if (dot == NULL) {
        return 0;
    }
    *dot = '\0';
    snprintf(validator, sizeof(validator), "%s", dot + 1);
    remember = find_remember(cookie);
    if (remember == NULL || remember->expires <= now) {
        return 0;
    }
    sha256_hex(validator, hash);
    if (!constant_time_equal(hash, remember->hash)) {
        return 0;
    }
    if (frisk_uadb == NULL ||
        !uadbWeb_userAccountExists(frisk_uadb, remember->username)) {
        remember->used = 0;
        save_remembers();
        return 0;
    }
    set_session_user(rqst, remember->username);
    snprintf(out_username, out_size, "%s", remember->username);

    /* Rotate the validator on every successful use. */
    if (AmmServer_GenerateSecureToken(rotated, sizeof(rotated), 32)) {
        sha256_hex(rotated, rotated_hash);
        snprintf(remember->hash, sizeof(remember->hash), "%s",
                 rotated_hash);
        remember->expires = now + REMEMBER_SECONDS;
        save_remembers();
        snprintf(rotated_cookie, sizeof(rotated_cookie), "%s.%s",
                 remember->selector, rotated);
        AmmServer_SetCookie(rqst, "frisk_rm", rotated_cookie,
                            REMEMBER_DAYS * 24 * 60 * 60, 1);
    }
    return 1;
}

void accounts_remember_forget(struct AmmServer_DynamicRequest *rqst)
{
    char cookie[192];
    char *dot;
    struct FriskRemember *remember;

    if (rqst == NULL) {
        return;
    }
    if (_COOKIEcpy(rqst, "frisk_rm", cookie, sizeof(cookie))) {
        dot = strchr(cookie, '.');
        if (dot != NULL) {
            *dot = '\0';
            remember = find_remember(cookie);
            if (remember != NULL) {
                remember->used = 0;
                save_remembers();
            }
        }
    }
    AmmServer_SetCookie(rqst, "frisk_rm", "", -1, 1);
}

/* ------------------------------------------------------------------ */
/* Invites                                                            */
/* ------------------------------------------------------------------ */

static int generate_invite_code(char *out, size_t out_size)
{
    char raw[128];
    int attempt;
    size_t i;

    if (out_size < 11) {
        return 0;
    }
    for (attempt = 0; attempt < 8; attempt++) {
        size_t used = 0;
        if (!AmmServer_GenerateSecureToken(raw, sizeof(raw), 32)) {
            return 0;
        }
        for (i = 0; raw[i] != '\0' && used < 10; i++) {
            unsigned char c = (unsigned char)raw[i];
            if (c >= 'a' && c <= 'z') {
                c = (unsigned char)(c - 'a' + 'A');
            }
            if (strchr(INVITE_ALPHABET, c) != NULL) {
                out[used++] = (char)c;
            }
        }
        if (used == 10) {
            out[10] = '\0';
            if (find_invite(out) == NULL) {
                return 1;
            }
        }
    }
    return 0;
}

int accounts_invite_new(const char *creator, char *out_code,
                        size_t out_code_size, char *err, size_t err_size)
{
    int i, outstanding = 0;
    struct FriskInvite *invite;

    if (creator == NULL || find_profile(creator) == NULL) {
        set_error(err, err_size, "no profile");
        return 0;
    }
    for (i = 0; i < invite_count; i++) {
        if (strcmp(invites[i].creator, creator) == 0 &&
            !invites[i].used && !invites[i].revoked) {
            outstanding++;
        }
    }
    if (outstanding >= INVITE_MAX_OUTSTANDING) {
        set_error(err, err_size, "limit");
        return 0;
    }
    if (invite_count >= FRISK_MAX_INVITES) {
        set_error(err, err_size, "full");
        return 0;
    }
    if (!generate_invite_code(out_code, out_code_size)) {
        set_error(err, err_size, "generate");
        return 0;
    }
    invite = &invites[invite_count++];
    memset(invite, 0, sizeof(*invite));
    snprintf(invite->code, sizeof(invite->code), "%s", out_code);
    snprintf(invite->creator, sizeof(invite->creator), "%s", creator);
    invite->created = now_seconds();
    if (!save_invites()) {
        invite_count--;
        set_error(err, err_size, "save");
        return 0;
    }
    return 1;
}

int accounts_invite_count(void)
{
    return invite_count;
}

const struct FriskInvite *accounts_invite_at(int index)
{
    if (index < 0 || index >= invite_count) {
        return NULL;
    }
    return &invites[index];
}

int accounts_invite_revoke(const char *code, const char *requester,
                           int requester_is_admin, char *err,
                           size_t err_size)
{
    struct FriskInvite *invite;

    if (!requester_is_admin) {
        set_error(err, err_size, "admin");
        return 0;
    }
    invite = find_invite(code);
    if (invite == NULL) {
        set_error(err, err_size, "notfound");
        return 0;
    }
    invite->revoked = 1;
    if (!save_invites()) {
        set_error(err, err_size, "save");
        return 0;
    }
    (void)requester;
    return 1;
}

/* ------------------------------------------------------------------ */
/* JSON helpers                                                       */
/* ------------------------------------------------------------------ */

static void write_profile_json(struct FriskJson *json,
                               const struct FriskProfile *profile)
{
    frisk_json_key_str(json, "username", profile->username);
    frisk_json_key_bool(json, "admin", profile->admin);
    frisk_json_key_str(json, "colour", profile->colour);
    frisk_json_key_str(json, "glyph", profile->glyph);
    frisk_json_key_object(json, "stats");
    frisk_json_key_int(json, "gamesPlayed", profile->games_played);
    frisk_json_key_int(json, "wins", profile->wins);
    frisk_json_key_int(json, "losses", profile->losses);
    frisk_json_key_int(json, "territoriesTaken",
                       profile->territories_taken);
    frisk_json_key_int(json, "armiesKilled", profile->armies_killed);
    frisk_json_key_int(json, "armiesLost", profile->armies_lost);
    frisk_json_object_end(json);
    frisk_json_key_int(json, "created", profile->created);
    frisk_json_key_int(json, "lastSeen", profile->last_seen);
}

static void write_invite_json(struct FriskJson *json,
                              const struct FriskInvite *invite)
{
    frisk_json_key_str(json, "code", invite->code);
    frisk_json_key_str(json, "creator", invite->creator);
    frisk_json_key_int(json, "created", invite->created);
    frisk_json_key_str(json, "usedBy", invite->used_by);
    frisk_json_key_int(json, "usedAt", invite->used_at);
    frisk_json_key_bool(json, "used", invite->used);
    frisk_json_key_bool(json, "revoked", invite->revoked);
}

static void write_session_json(struct FriskJson *json,
                               const char *username, const char *csrf)
{
    const struct FriskProfile *profile = find_profile(username);

    frisk_json_key_int(json, "ok", 1);
    frisk_json_key_str(json, "user", username);
    frisk_json_key_bool(json, "admin",
                        profile != NULL && profile->admin);
    frisk_json_key_str(json, "csrf", csrf);
    frisk_json_key_object(json, "profile");
    if (profile != NULL) {
        write_profile_json(json, profile);
    }
    frisk_json_object_end(json);
}

static int validate_colour(const char *colour)
{
    int i;

    if (colour == NULL || strlen(colour) != 7 || colour[0] != '#') {
        return 0;
    }
    for (i = 1; i < 7; i++) {
        if (!isxdigit((unsigned char)colour[i])) {
            return 0;
        }
    }
    return 1;
}

static int validate_glyph(const char *glyph)
{
    size_t length;
    size_t i;

    if (glyph == NULL) {
        return 0;
    }
    length = strlen(glyph);
    if (length < 1 || length >= 16) {
        return 0;
    }
    for (i = 0; i < length; i++) {
        unsigned char c = (unsigned char)glyph[i];
        if (c < 0x20 || c == '\t' || c == '\n' || c == '\r' || c == 0x7f) {
            return 0;
        }
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* Handlers                                                           */
/* ------------------------------------------------------------------ */

void *accounts_me_callback(struct AmmServer_DynamicRequest *rqst)
{
    char username[32];
    char csrf[64];
    struct FriskJson json;

    pthread_mutex_lock(&friskLock);
    username[0] = '\0';
    if (!AmmServer_CurrentUsername(rqst, username, sizeof(username))) {
        accounts_remember_login(rqst, username, sizeof(username));
    }
    if (username[0] == '\0') {
        frisk_json_begin(&json, rqst);
        frisk_json_key_int(&json, "ok", 1);
        frisk_json_key_raw(&json, "user", "null");
        frisk_json_end(&json);
        pthread_mutex_unlock(&friskLock);
        return 0;
    }
    if (accounts_ensure_profile(username, NULL, 0) != 0) {
        frisk_json_error(rqst, 500, "profile");
        pthread_mutex_unlock(&friskLock);
        return 0;
    }
    accounts_touch(username);
    csrf[0] = '\0';
    frisk_csrf_token(rqst, csrf, sizeof(csrf));
    frisk_json_begin(&json, rqst);
    write_session_json(&json, username, csrf);
    frisk_json_end(&json);
    pthread_mutex_unlock(&friskLock);
    return 0;
}

void *accounts_login_callback(struct AmmServer_DynamicRequest *rqst)
{
    char username[32];
    char password[128];
    char csrf[64];
    struct FriskJson json;

    pthread_mutex_lock(&friskLock);
    if (!frisk_is_post(rqst)) {
        frisk_json_error(rqst, 400, "post");
        pthread_mutex_unlock(&friskLock);
        return 0;
    }
    if (rate_limited(rqst)) {
        frisk_json_error(rqst, 429, "rate");
        pthread_mutex_unlock(&friskLock);
        return 0;
    }
    if (!frisk_post_str(rqst, "name", username, sizeof(username)) ||
        !frisk_post_str(rqst, "password", password, sizeof(password)) ||
        username[0] == '\0' || password[0] == '\0') {
        rate_fail(rqst);
        frisk_json_error(rqst, 401, "login");
        pthread_mutex_unlock(&friskLock);
        return 0;
    }
    if (!AmmServer_Login(rqst, frisk_uadb, username, password)) {
        rate_fail(rqst);
        frisk_json_error(rqst, 401, "login");
        pthread_mutex_unlock(&friskLock);
        return 0;
    }
    rate_clear(rqst);
    if (accounts_ensure_profile(username, NULL, 0) != 0) {
        frisk_json_error(rqst, 500, "profile");
        pthread_mutex_unlock(&friskLock);
        return 0;
    }
    accounts_touch(username);
    accounts_remember_issue(rqst, username);
    csrf[0] = '\0';
    frisk_csrf_token(rqst, csrf, sizeof(csrf));
    frisk_json_begin(&json, rqst);
    write_session_json(&json, username, csrf);
    frisk_json_end(&json);
    pthread_mutex_unlock(&friskLock);
    return 0;
}

void *accounts_logout_callback(struct AmmServer_DynamicRequest *rqst)
{
    char username[32];
    struct FriskJson json;

    pthread_mutex_lock(&friskLock);
    if (!frisk_is_post(rqst)) {
        frisk_json_error(rqst, 400, "post");
        pthread_mutex_unlock(&friskLock);
        return 0;
    }
    if (!frisk_require_user(rqst, username, sizeof(username)) ||
        !frisk_require_csrf(rqst)) {
        pthread_mutex_unlock(&friskLock);
        return 0;
    }
    accounts_remember_forget(rqst);
    AmmServer_Logout(rqst);
    frisk_json_begin(&json, rqst);
    frisk_json_key_int(&json, "ok", 1);
    frisk_json_end(&json);
    pthread_mutex_unlock(&friskLock);
    return 0;
}

void *accounts_register_callback(struct AmmServer_DynamicRequest *rqst)
{
    char username[32];
    char password[128];
    char invite_code[32];
    char csrf[64];
    char error[128];
    struct FriskInvite *invite = NULL;
    int bootstrap;
    struct FriskJson json;
    struct FriskProfile *profile;

    pthread_mutex_lock(&friskLock);
    if (!frisk_is_post(rqst)) {
        frisk_json_error(rqst, 400, "post");
        pthread_mutex_unlock(&friskLock);
        return 0;
    }
    if (rate_limited(rqst)) {
        frisk_json_error(rqst, 429, "rate");
        pthread_mutex_unlock(&friskLock);
        return 0;
    }
    username[0] = password[0] = invite_code[0] = '\0';
    frisk_post_str(rqst, "name", username, sizeof(username));
    frisk_post_str(rqst, "password", password, sizeof(password));
    frisk_post_str(rqst, "invite", invite_code, sizeof(invite_code));
    if (!uadbWeb_isValidUsername(username)) {
        rate_fail(rqst);
        frisk_json_error(rqst, 400, "name");
        pthread_mutex_unlock(&friskLock);
        return 0;
    }
    if (strlen(password) < 8 || strlen(password) > 100) {
        rate_fail(rqst);
        frisk_json_error(rqst, 400, "password");
        pthread_mutex_unlock(&friskLock);
        return 0;
    }
    if (uadbWeb_userAccountExists(frisk_uadb, username)) {
        rate_fail(rqst);
        frisk_json_error(rqst, 409, "exists");
        pthread_mutex_unlock(&friskLock);
        return 0;
    }
    bootstrap = frisk_uadb->userListSize == 0;
    if (!bootstrap) {
        invite = find_invite(invite_code);
        if (invite == NULL || invite->used || invite->revoked) {
            rate_fail(rqst);
            frisk_json_error(rqst, 403, "invite");
            pthread_mutex_unlock(&friskLock);
            return 0;
        }
    }
    if (!uadb_addUser(frisk_uadb, username, password, "0.0.0.0", "frisk")) {
        rate_fail(rqst);
        frisk_json_error(rqst, 500, "register");
        pthread_mutex_unlock(&friskLock);
        return 0;
    }
    if (invite != NULL) {
        invite->used = 1;
        snprintf(invite->used_by, sizeof(invite->used_by), "%s", username);
        invite->used_at = now_seconds();
        save_invites();
    }
    if (accounts_ensure_profile(username, error, sizeof(error)) != 0) {
        frisk_json_error(rqst, 500, "profile");
        pthread_mutex_unlock(&friskLock);
        return 0;
    }
    profile = find_profile(username);
    if (bootstrap && profile != NULL) {
        profile->admin = 1;
        save_profiles();
    }
    if (!AmmServer_Login(rqst, frisk_uadb, username, password)) {
        frisk_json_error(rqst, 500, "login");
        pthread_mutex_unlock(&friskLock);
        return 0;
    }
    rate_clear(rqst);
    accounts_remember_issue(rqst, username);
    csrf[0] = '\0';
    frisk_csrf_token(rqst, csrf, sizeof(csrf));
    frisk_json_begin(&json, rqst);
    write_session_json(&json, username, csrf);
    frisk_json_end(&json);
    pthread_mutex_unlock(&friskLock);
    return 0;
}

void *accounts_invite_new_callback(struct AmmServer_DynamicRequest *rqst)
{
    char username[32];
    char code[16];
    char error[64];
    struct FriskJson json;

    pthread_mutex_lock(&friskLock);
    if (!frisk_is_post(rqst)) {
        frisk_json_error(rqst, 400, "post");
        pthread_mutex_unlock(&friskLock);
        return 0;
    }
    if (!frisk_require_user(rqst, username, sizeof(username)) ||
        !frisk_require_csrf(rqst)) {
        pthread_mutex_unlock(&friskLock);
        return 0;
    }
    if (!accounts_invite_new(username, code, sizeof(code),
                             error, sizeof(error))) {
        if (strcmp(error, "limit") == 0) {
            frisk_json_error(rqst, 409, "limit");
        } else {
            frisk_json_error(rqst, 500, "invite");
        }
        pthread_mutex_unlock(&friskLock);
        return 0;
    }
    frisk_json_begin(&json, rqst);
    frisk_json_key_int(&json, "ok", 1);
    frisk_json_key_str(&json, "code", code);
    frisk_json_end(&json);
    pthread_mutex_unlock(&friskLock);
    return 0;
}

void *accounts_invite_list_callback(struct AmmServer_DynamicRequest *rqst)
{
    char username[32];
    int admin;
    int i;
    struct FriskJson json;

    pthread_mutex_lock(&friskLock);
    if (!frisk_require_user(rqst, username, sizeof(username))) {
        pthread_mutex_unlock(&friskLock);
        return 0;
    }
    admin = accounts_is_admin(username);
    frisk_json_begin(&json, rqst);
    frisk_json_key_int(&json, "ok", 1);
    frisk_json_key_array(&json, "invites");
    for (i = 0; i < invite_count; i++) {
        const struct FriskInvite *invite = &invites[i];
        if (!admin && strcmp(invite->creator, username) != 0) {
            continue;
        }
        frisk_json_array_object(&json);
        write_invite_json(&json, invite);
        frisk_json_object_end(&json);
    }
    frisk_json_array_end(&json);
    frisk_json_end(&json);
    pthread_mutex_unlock(&friskLock);
    return 0;
}

void *accounts_invite_revoke_callback(struct AmmServer_DynamicRequest *rqst)
{
    char username[32];
    char code[32];
    char error[64];
    struct FriskJson json;

    pthread_mutex_lock(&friskLock);
    if (!frisk_is_post(rqst)) {
        frisk_json_error(rqst, 400, "post");
        pthread_mutex_unlock(&friskLock);
        return 0;
    }
    if (!frisk_require_user(rqst, username, sizeof(username)) ||
        !frisk_require_csrf(rqst)) {
        pthread_mutex_unlock(&friskLock);
        return 0;
    }
    if (!frisk_post_str(rqst, "code", code, sizeof(code)) ||
        !accounts_invite_revoke(code, username,
                                accounts_is_admin(username),
                                error, sizeof(error))) {
        frisk_json_error(rqst, 403, "invite");
        pthread_mutex_unlock(&friskLock);
        return 0;
    }
    frisk_json_begin(&json, rqst);
    frisk_json_key_int(&json, "ok", 1);
    frisk_json_end(&json);
    pthread_mutex_unlock(&friskLock);
    return 0;
}

void *accounts_profile_callback(struct AmmServer_DynamicRequest *rqst)
{
    char username[32];
    char requested[32];
    const struct FriskProfile *profile;
    struct FriskJson json;

    pthread_mutex_lock(&friskLock);
    if (!frisk_require_user(rqst, username, sizeof(username))) {
        pthread_mutex_unlock(&friskLock);
        return 0;
    }
    requested[0] = '\0';
    if (_GETexists(rqst, "u") &&
        !frisk_param_str(rqst, "u", requested, sizeof(requested))) {
        frisk_json_error(rqst, 400, "name");
        pthread_mutex_unlock(&friskLock);
        return 0;
    }
    if (requested[0] == '\0') {
        snprintf(requested, sizeof(requested), "%s", username);
    }
    if (!uadbWeb_isValidUsername(requested)) {
        frisk_json_error(rqst, 400, "name");
        pthread_mutex_unlock(&friskLock);
        return 0;
    }
    if (strcmp(requested, username) == 0) {
        accounts_ensure_profile(username, NULL, 0);
    }
    profile = find_profile(requested);
    if (profile == NULL) {
        frisk_json_error(rqst, 404, "profile");
        pthread_mutex_unlock(&friskLock);
        return 0;
    }
    frisk_json_begin(&json, rqst);
    frisk_json_key_int(&json, "ok", 1);
    frisk_json_key_object(&json, "profile");
    write_profile_json(&json, profile);
    frisk_json_object_end(&json);
    {
        char *history = NULL;
        size_t history_size = 0;

        frisk_json_key_array(&json, "history");
        if (games_history_for(requested, 20, &history, &history_size) == 0 &&
            history != NULL && history_size > 0) {
            frisk_json_value_raw(&json, history);
        }
        frisk_json_array_end(&json);
        free(history);
    }
    frisk_json_end(&json);
    pthread_mutex_unlock(&friskLock);
    return 0;
}

void *accounts_profile_set_callback(struct AmmServer_DynamicRequest *rqst)
{
    char username[32];
    char colour[8];
    char glyph[16];
    const struct FriskProfile *profile;
    struct FriskProfile *mutable_profile;
    struct FriskJson json;

    pthread_mutex_lock(&friskLock);
    if (!frisk_is_post(rqst)) {
        frisk_json_error(rqst, 400, "post");
        pthread_mutex_unlock(&friskLock);
        return 0;
    }
    if (!frisk_require_user(rqst, username, sizeof(username)) ||
        !frisk_require_csrf(rqst)) {
        pthread_mutex_unlock(&friskLock);
        return 0;
    }
    if (accounts_ensure_profile(username, NULL, 0) != 0) {
        frisk_json_error(rqst, 500, "profile");
        pthread_mutex_unlock(&friskLock);
        return 0;
    }
    mutable_profile = find_profile(username);
    if (mutable_profile == NULL) {
        frisk_json_error(rqst, 500, "profile");
        pthread_mutex_unlock(&friskLock);
        return 0;
    }
    if (_POSTexists(rqst, "colour")) {
        colour[0] = '\0';
        frisk_post_str(rqst, "colour", colour, sizeof(colour));
        if (!validate_colour(colour)) {
            frisk_json_error(rqst, 400, "colour");
            pthread_mutex_unlock(&friskLock);
            return 0;
        }
        snprintf(mutable_profile->colour,
                 sizeof(mutable_profile->colour), "%s", colour);
    }
    if (_POSTexists(rqst, "glyph")) {
        glyph[0] = '\0';
        frisk_post_str(rqst, "glyph", glyph, sizeof(glyph));
        if (!validate_glyph(glyph)) {
            frisk_json_error(rqst, 400, "glyph");
            pthread_mutex_unlock(&friskLock);
            return 0;
        }
        snprintf(mutable_profile->glyph, sizeof(mutable_profile->glyph),
                 "%s", glyph);
    }
    if (!save_profiles()) {
        frisk_json_error(rqst, 500, "save");
        pthread_mutex_unlock(&friskLock);
        return 0;
    }
    profile = mutable_profile;
    frisk_json_begin(&json, rqst);
    frisk_json_key_int(&json, "ok", 1);
    frisk_json_key_object(&json, "profile");
    write_profile_json(&json, profile);
    frisk_json_object_end(&json);
    frisk_json_end(&json);
    pthread_mutex_unlock(&friskLock);
    return 0;
}
