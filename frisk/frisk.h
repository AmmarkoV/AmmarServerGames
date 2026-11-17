#ifndef FRISK_H
#define FRISK_H

#include <pthread.h>

#define FRISK_DEFAULT_PORT 8090
#define FRISK_DEFAULT_RES_ROOT "src/AmmarServerGames/frisk/res/"
#define FRISK_DEFAULT_DATA_DIR "db/frisk"
#define FRISK_DEFAULT_USERS_DB "db/users.db"

#define FRISK_MAX_PROFILES 512
#define FRISK_MAX_INVITES 512
#define FRISK_MAX_REMEMBER 1024
#define FRISK_PATH_SIZE 1024

/* Every handler takes this for its whole body (except clip streaming).  The
 * account database (uadb) and all FRISK stores are not independently
 * thread-safe, so one coarse lock is simpler and sufficient at LAN scale. */
extern pthread_mutex_t friskLock;

/* Reads FRISK_RES_ROOT, FRISK_DATA_DIR, FRISK_USERS_DB and FRISK_PORT once. */
int frisk_config_init(void);
const char *frisk_res_root(void);
const char *frisk_data_dir(void);
const char *frisk_users_db(void);
unsigned int frisk_port(void);

#endif
