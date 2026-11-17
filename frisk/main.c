/*
 * FRISK - LAN-hosted, asynchronous Risk-like board game.
 *
 * Locking rule (PLAN.md 3.5): every resource handler takes friskLock for its
 * whole body except clip streaming, which will not hold the lock while the
 * file is sent.  One coarse lock covers the account database, profiles,
 * invites, remember-me tokens and all game state; the handlers only do short
 * in-memory work plus one atomic file write.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../../AmmServerlib/AmmServerlib.h"

#include "accounts.h"
#include "admin.h"
#include "chat.h"
#include "frisk.h"
#include "games.h"
#include "http.h"
#include "lobby.h"
#include "play.h"
#include "store.h"

pthread_mutex_t friskLock = PTHREAD_MUTEX_INITIALIZER;

static char res_root[FRISK_PATH_SIZE] = FRISK_DEFAULT_RES_ROOT;
static char data_dir[FRISK_PATH_SIZE] = FRISK_DEFAULT_DATA_DIR;
static char users_db[FRISK_PATH_SIZE] = FRISK_DEFAULT_USERS_DB;
static unsigned int server_port = FRISK_DEFAULT_PORT;

static struct AmmServer_Instance *default_server;
static struct AmmServer_RH_Context me_context = {0};
static struct AmmServer_RH_Context login_context = {0};
static struct AmmServer_RH_Context logout_context = {0};
static struct AmmServer_RH_Context register_context = {0};
static struct AmmServer_RH_Context invite_new_context = {0};
static struct AmmServer_RH_Context invite_list_context = {0};
static struct AmmServer_RH_Context invite_revoke_context = {0};
static struct AmmServer_RH_Context profile_context = {0};
static struct AmmServer_RH_Context profile_set_context = {0};
static struct AmmServer_RH_Context lobby_context = {0};
static struct AmmServer_RH_Context game_create_context = {0};
static struct AmmServer_RH_Context game_join_context = {0};
static struct AmmServer_RH_Context game_leave_context = {0};
static struct AmmServer_RH_Context game_start_context = {0};
static struct AmmServer_RH_Context game_kick_context = {0};
static struct AmmServer_RH_Context game_delete_context = {0};
static struct AmmServer_RH_Context game_state_context = {0};
static struct AmmServer_RH_Context game_log_context = {0};
static struct AmmServer_RH_Context game_act_context = {0};
static struct AmmServer_RH_Context chat_context = {0};
static struct AmmServer_RH_Context chat_say_context = {0};
static struct AmmServer_RH_Context chat_clip_context = {0};
static struct AmmServer_RH_Context clip_context = {0};
static struct AmmServer_RH_Context admin_overview_context = {0};
static struct AmmServer_RH_Context admin_neutralise_context = {0};
static struct AmmServer_RH_Context map_context = {0};

static void copy_env(char *destination, size_t size, const char *name)
{
    const char *value = getenv(name);
    if (value != NULL && value[0] != '\0') {
        snprintf(destination, size, "%s", value);
    }
}

int frisk_config_init(void)
{
    const char *port_text;

    copy_env(res_root, sizeof(res_root), "FRISK_RES_ROOT");
    copy_env(data_dir, sizeof(data_dir), "FRISK_DATA_DIR");
    copy_env(users_db, sizeof(users_db), "FRISK_USERS_DB");
    port_text = getenv("FRISK_PORT");
    if (port_text != NULL && port_text[0] != '\0') {
        long port = strtol(port_text, NULL, 10);
        if (port > 0 && port <= 65535) {
            server_port = (unsigned int)port;
        }
    }
    if (res_root[0] != '\0' &&
        res_root[strlen(res_root) - 1] != '/') {
        size_t length = strlen(res_root);
        if (length + 2 < sizeof(res_root)) {
            res_root[length] = '/';
            res_root[length + 1] = '\0';
        }
    }
    return 1;
}

const char *frisk_res_root(void)
{
    return res_root;
}

const char *frisk_data_dir(void)
{
    return data_dir;
}

const char *frisk_users_db(void)
{
    return users_db;
}

unsigned int frisk_port(void)
{
    return server_port;
}

/* Every resource is session enabled: that makes rqst->sessionToken, the
 * _SESSION* family, AmmServer_CurrentUsername() and CSRF tokens meaningful. */
static void addSessionResourceHandler(
    struct AmmServer_RH_Context *context,
    const char *resourceName,
    unsigned int allocateMemoryBytes,
    void *callback,
    unsigned int scenario)
{
    AmmServer_AddResourceHandler(default_server, context, resourceName,
                                 allocateMemoryBytes, 0, callback, scenario);
    AmmServer_DoNOTCacheResourceHandler(default_server, context);
    context->requestContext.useSessionLifecycle = 1;
}

static int init_dynamic_content(void)
{
    char error[256];
    char path[FRISK_PATH_SIZE];
    static const char *api_dirs[] = {
        "api", "api/game", "api/invite", "api/profile", "api/chat",
        "api/admin"
    };
    size_t i;

    /* AmmarServer resolves a dynamic resource's parent directory under the
     * web root before dispatching it; nested API paths need those empty
     * directories to exist.  They are created here rather than committed. */
    for (i = 0; i < sizeof(api_dirs) / sizeof(api_dirs[0]); i++) {
        snprintf(path, sizeof(path), "%s%s", frisk_res_root(), api_dirs[i]);
        if (store_ensure_dir(path, error, sizeof(error)) != 0) {
            AmmServer_Warning("Could not create %s: %s", path, error);
        }
    }

    if (accounts_init(error, sizeof(error)) != 0) {
        AmmServer_Error("Could not initialize accounts: %s", error);
        return 0;
    }
    if (games_init(accounts_game_finished, NULL, error,
                   sizeof(error)) != 0) {
        AmmServer_Error("Could not initialize games: %s", error);
        return 0;
    }
    if (chat_init(error, sizeof(error)) != 0) {
        AmmServer_Error("Could not initialize chat: %s", error);
        return 0;
    }

    addSessionResourceHandler(&me_context, "/api/me", 16 * 1024,
                              &accounts_me_callback,
                              DIFFERENT_PAGE_FOR_EACH_CLIENT);
    addSessionResourceHandler(&login_context, "/api/login", 16 * 1024,
                              &accounts_login_callback,
                              DIFFERENT_PAGE_FOR_EACH_CLIENT |
                              ENABLE_RECEIVING_FILES);
    addSessionResourceHandler(&logout_context, "/api/logout", 16 * 1024,
                              &accounts_logout_callback,
                              DIFFERENT_PAGE_FOR_EACH_CLIENT |
                              ENABLE_RECEIVING_FILES);
    addSessionResourceHandler(&register_context, "/api/register", 16 * 1024,
                              &accounts_register_callback,
                              DIFFERENT_PAGE_FOR_EACH_CLIENT |
                              ENABLE_RECEIVING_FILES);
    addSessionResourceHandler(&invite_new_context, "/api/invite/new",
                              16 * 1024, &accounts_invite_new_callback,
                              DIFFERENT_PAGE_FOR_EACH_CLIENT |
                              ENABLE_RECEIVING_FILES);
    addSessionResourceHandler(&invite_list_context, "/api/invite/list",
                              64 * 1024, &accounts_invite_list_callback,
                              DIFFERENT_PAGE_FOR_EACH_CLIENT);
    addSessionResourceHandler(&invite_revoke_context, "/api/invite/revoke",
                              16 * 1024, &accounts_invite_revoke_callback,
                              DIFFERENT_PAGE_FOR_EACH_CLIENT |
                              ENABLE_RECEIVING_FILES);
    addSessionResourceHandler(&profile_context, "/api/profile", 32 * 1024,
                              &accounts_profile_callback,
                              DIFFERENT_PAGE_FOR_EACH_CLIENT);
    addSessionResourceHandler(&profile_set_context, "/api/profile/set",
                              32 * 1024, &accounts_profile_set_callback,
                              DIFFERENT_PAGE_FOR_EACH_CLIENT |
                              ENABLE_RECEIVING_FILES);

    addSessionResourceHandler(&lobby_context, "/api/lobby", 256 * 1024,
                              &lobby_list_callback,
                              DIFFERENT_PAGE_FOR_EACH_CLIENT);
    addSessionResourceHandler(&game_create_context, "/api/game/create",
                              16 * 1024, &lobby_create_callback,
                              DIFFERENT_PAGE_FOR_EACH_CLIENT |
                              ENABLE_RECEIVING_FILES);
    addSessionResourceHandler(&game_join_context, "/api/game/join",
                              16 * 1024, &lobby_join_callback,
                              DIFFERENT_PAGE_FOR_EACH_CLIENT |
                              ENABLE_RECEIVING_FILES);
    addSessionResourceHandler(&game_leave_context, "/api/game/leave",
                              16 * 1024, &lobby_leave_callback,
                              DIFFERENT_PAGE_FOR_EACH_CLIENT |
                              ENABLE_RECEIVING_FILES);
    addSessionResourceHandler(&game_start_context, "/api/game/start",
                              16 * 1024, &lobby_start_callback,
                              DIFFERENT_PAGE_FOR_EACH_CLIENT |
                              ENABLE_RECEIVING_FILES);
    addSessionResourceHandler(&game_kick_context, "/api/game/kick",
                              16 * 1024, &lobby_kick_callback,
                              DIFFERENT_PAGE_FOR_EACH_CLIENT |
                              ENABLE_RECEIVING_FILES);
    addSessionResourceHandler(&game_delete_context, "/api/game/delete",
                              16 * 1024, &lobby_delete_callback,
                              DIFFERENT_PAGE_FOR_EACH_CLIENT |
                              ENABLE_RECEIVING_FILES);

    addSessionResourceHandler(&game_state_context, "/api/game/state",
                              1024 * 1024, &play_state_callback,
                              DIFFERENT_PAGE_FOR_EACH_CLIENT);
    addSessionResourceHandler(&game_log_context, "/api/game/log",
                              1024 * 1024, &play_log_callback,
                              DIFFERENT_PAGE_FOR_EACH_CLIENT);
    addSessionResourceHandler(&game_act_context, "/api/game/act",
                              16 * 1024, &play_action_callback,
                              DIFFERENT_PAGE_FOR_EACH_CLIENT |
                              ENABLE_RECEIVING_FILES);

    addSessionResourceHandler(&chat_context, "/api/chat", 256 * 1024,
                              &chat_tail_callback,
                              DIFFERENT_PAGE_FOR_EACH_CLIENT);
    addSessionResourceHandler(&chat_say_context, "/api/chat/say", 16 * 1024,
                              &chat_say_callback,
                              DIFFERENT_PAGE_FOR_EACH_CLIENT |
                              ENABLE_RECEIVING_FILES);
    addSessionResourceHandler(&chat_clip_context, "/api/chat/clip",
                              16 * 1024, &chat_clip_callback,
                              DIFFERENT_PAGE_FOR_EACH_CLIENT |
                              ENABLE_RECEIVING_FILES);
    addSessionResourceHandler(&clip_context, "/api/clip", 16 * 1024,
                              &chat_clip_get_callback,
                              DIFFERENT_PAGE_FOR_EACH_CLIENT |
                              ENABLE_RECEIVING_FILES);
    addSessionResourceHandler(&admin_overview_context, "/api/admin/overview",
                              512 * 1024, &admin_overview_callback,
                              DIFFERENT_PAGE_FOR_EACH_CLIENT);
    addSessionResourceHandler(&admin_neutralise_context,
                              "/api/admin/neutralise", 16 * 1024,
                              &admin_neutralise_callback,
                              DIFFERENT_PAGE_FOR_EACH_CLIENT |
                              ENABLE_RECEIVING_FILES);
    addSessionResourceHandler(&map_context, "/api/map", 16 * 1024,
                              &lobby_map_callback,
                              DIFFERENT_PAGE_FOR_EACH_CLIENT);
    return 1;
}

static int closed;

static void close_dynamic_content(void)
{
    if (closed) {
        return;
    }
    closed = 1;
    if (default_server == NULL) {
        return;
    }
    AmmServer_RemoveResourceHandler(default_server, &me_context, 1);
    AmmServer_RemoveResourceHandler(default_server, &login_context, 1);
    AmmServer_RemoveResourceHandler(default_server, &logout_context, 1);
    AmmServer_RemoveResourceHandler(default_server, &register_context, 1);
    AmmServer_RemoveResourceHandler(default_server, &invite_new_context, 1);
    AmmServer_RemoveResourceHandler(default_server, &invite_list_context, 1);
    AmmServer_RemoveResourceHandler(default_server,
                                    &invite_revoke_context, 1);
    AmmServer_RemoveResourceHandler(default_server, &profile_context, 1);
    AmmServer_RemoveResourceHandler(default_server, &profile_set_context, 1);
    AmmServer_RemoveResourceHandler(default_server, &lobby_context, 1);
    AmmServer_RemoveResourceHandler(default_server, &game_create_context, 1);
    AmmServer_RemoveResourceHandler(default_server, &game_join_context, 1);
    AmmServer_RemoveResourceHandler(default_server, &game_leave_context, 1);
    AmmServer_RemoveResourceHandler(default_server, &game_start_context, 1);
    AmmServer_RemoveResourceHandler(default_server, &game_kick_context, 1);
    AmmServer_RemoveResourceHandler(default_server, &game_delete_context, 1);
    AmmServer_RemoveResourceHandler(default_server, &game_state_context, 1);
    AmmServer_RemoveResourceHandler(default_server, &game_log_context, 1);
    AmmServer_RemoveResourceHandler(default_server, &game_act_context, 1);
    AmmServer_RemoveResourceHandler(default_server, &chat_context, 1);
    AmmServer_RemoveResourceHandler(default_server, &chat_say_context, 1);
    AmmServer_RemoveResourceHandler(default_server, &chat_clip_context, 1);
    AmmServer_RemoveResourceHandler(default_server, &clip_context, 1);
    AmmServer_RemoveResourceHandler(default_server,
                                    &admin_overview_context, 1);
    AmmServer_RemoveResourceHandler(default_server,
                                    &admin_neutralise_context, 1);
    AmmServer_RemoveResourceHandler(default_server, &map_context, 1);
    chat_shutdown();
    games_shutdown();
    accounts_shutdown();
}

int main(int argc, char *argv[])
{
    printf("\nAmmar Server %s - FRISK starting up..\n", AmmServer_Version());
    frisk_config_init();
    AmmServer_CheckIfHeaderBinaryAreTheSame(AMMAR_SERVER_HTTP_HEADER_SPEC);
    AmmServer_RegisterTerminationSignal(&close_dynamic_content);

    default_server = AmmServer_StartWithArgs(
        "Frisk", argc, argv, "0.0.0.0", frisk_port(), 0,
        frisk_res_root(), "public_html/templates/");
    if (default_server == NULL) {
        AmmServer_Error("Could not start the server, shutting down");
        return 1;
    }
    if (!init_dynamic_content()) {
        AmmServer_Error("Could not initialize FRISK content");
        AmmServer_Stop(default_server);
        return 1;
    }
    while (AmmServer_Running(default_server)) {
        sleep(1);
    }
    close_dynamic_content();
    AmmServer_Stop(default_server);
    AmmServer_Warning("FRISK stopped");
    return 0;
}
