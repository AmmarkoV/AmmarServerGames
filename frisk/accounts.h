#ifndef FRISK_ACCOUNTS_H
#define FRISK_ACCOUNTS_H

#include <stddef.h>

#include "../../AmmServerlib/AmmServerlib.h"

struct Game;

struct FriskProfile {
    char username[32];
    int admin;
    char colour[8];
    char glyph[16];
    int games_played;
    int wins;
    int losses;
    int territories_taken;
    int armies_killed;
    int armies_lost;
    long created;
    long last_seen;
    int used;
};

struct FriskInvite {
    char code[16];
    char creator[32];
    long created;
    char used_by[32];
    long used_at;
    int used;
    int revoked;
};

int accounts_init(char *err, size_t err_size);
void accounts_shutdown(void);

/* The returned pointer is owned by the module and is only safe while the
 * caller holds friskLock. */
const struct FriskProfile *accounts_profile(const char *username);
int accounts_profile_count(void);
const struct FriskProfile *accounts_profile_at(int index);
int accounts_ensure_profile(const char *username, char *err, size_t err_size);
int accounts_is_admin(const char *username);
void accounts_touch(const char *username);

/* GameEndHook installed on each game (PLAN.md 5.13).  Called with friskLock
 * held by the game action. */
void accounts_game_finished(struct Game *game, int winner, void *user);

/* Remember-me.  Both assume friskLock is held by the HTTP handler. */
int accounts_remember_login(struct AmmServer_DynamicRequest *rqst,
                            char *out_username, size_t out_size);
void accounts_remember_issue(struct AmmServer_DynamicRequest *rqst,
                             const char *username);
void accounts_remember_forget(struct AmmServer_DynamicRequest *rqst);

int accounts_invite_new(const char *creator, char *out_code,
                        size_t out_code_size, char *err, size_t err_size);
int accounts_invite_count(void);
const struct FriskInvite *accounts_invite_at(int index);
int accounts_invite_revoke(const char *code, const char *requester,
                           int requester_is_admin, char *err,
                           size_t err_size);

/* Resource handler callbacks registered from main.c. */
void *accounts_me_callback(struct AmmServer_DynamicRequest *rqst);
void *accounts_login_callback(struct AmmServer_DynamicRequest *rqst);
void *accounts_logout_callback(struct AmmServer_DynamicRequest *rqst);
void *accounts_register_callback(struct AmmServer_DynamicRequest *rqst);
void *accounts_invite_new_callback(struct AmmServer_DynamicRequest *rqst);
void *accounts_invite_list_callback(struct AmmServer_DynamicRequest *rqst);
void *accounts_invite_revoke_callback(struct AmmServer_DynamicRequest *rqst);
void *accounts_profile_callback(struct AmmServer_DynamicRequest *rqst);
void *accounts_profile_set_callback(struct AmmServer_DynamicRequest *rqst);

#endif
