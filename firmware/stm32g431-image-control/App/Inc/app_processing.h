#ifndef APP_PROCESSING_H
#define APP_PROCESSING_H

#include <stdint.h>

#include "app_types.h"

void AppProcessing_Run(AppFrame *frame);
float AppProcessing_ComputePairCorrelationQ15(const uint16_t *a, const uint16_t *b, uint16_t count);

#endif
