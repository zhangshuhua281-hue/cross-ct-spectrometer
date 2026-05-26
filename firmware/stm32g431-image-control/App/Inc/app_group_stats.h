#ifndef APP_GROUP_STATS_H
#define APP_GROUP_STATS_H

#include <stdint.h>

#include "app_types.h"

void AppGroupStats_Init(void);
void AppGroupStats_Accumulate(const AppFrame *frame);
uint8_t AppGroupStats_GetCount(void);
const AppGroupStats *AppGroupStats_GetByIndex(uint8_t index);

#endif
