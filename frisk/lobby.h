#ifndef FRISK_LOBBY_H
#define FRISK_LOBBY_H

#include "../../AmmServerlib/AmmServerlib.h"

void *lobby_list_callback(struct AmmServer_DynamicRequest *rqst);
void *lobby_create_callback(struct AmmServer_DynamicRequest *rqst);
void *lobby_join_callback(struct AmmServer_DynamicRequest *rqst);
void *lobby_leave_callback(struct AmmServer_DynamicRequest *rqst);
void *lobby_start_callback(struct AmmServer_DynamicRequest *rqst);
void *lobby_kick_callback(struct AmmServer_DynamicRequest *rqst);
void *lobby_delete_callback(struct AmmServer_DynamicRequest *rqst);
void *lobby_map_callback(struct AmmServer_DynamicRequest *rqst);

#endif
