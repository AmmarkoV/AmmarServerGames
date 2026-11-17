#ifndef FRISK_PLAY_H
#define FRISK_PLAY_H

#include "../../AmmServerlib/AmmServerlib.h"

void *play_state_callback(struct AmmServer_DynamicRequest *rqst);
void *play_log_callback(struct AmmServer_DynamicRequest *rqst);
void *play_action_callback(struct AmmServer_DynamicRequest *rqst);

#endif
