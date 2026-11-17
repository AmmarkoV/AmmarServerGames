#ifndef FRISK_ADMIN_H
#define FRISK_ADMIN_H

#include "../../AmmServerlib/AmmServerlib.h"

void *admin_overview_callback(struct AmmServer_DynamicRequest *rqst);
void *admin_neutralise_callback(struct AmmServer_DynamicRequest *rqst);

#endif
