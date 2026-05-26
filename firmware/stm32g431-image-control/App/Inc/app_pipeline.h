#ifndef APP_PIPELINE_H
#define APP_PIPELINE_H

#include <stdint.h>

#include "app_types.h"

typedef enum
{
  APP_PIPELINE_IDLE = 0,
  APP_PIPELINE_COLLECTING,
  APP_PIPELINE_READY,
  APP_PIPELINE_ERROR
} AppPipelineStatus;

void AppPipeline_Init(void);
AppPipelineStatus AppPipeline_ProcessAsciiLine(const char *text, AppFrame *frame, char *error_text, uint16_t error_size);
AppPipelineStatus AppPipeline_ProcessF103Frame12Bit(const uint8_t *data,
                                                    uint16_t size,
                                                    uint8_t group_id,
                                                    uint8_t replicate_id,
                                                    AppFrame *frame,
                                                    char *error_text,
                                                    uint16_t error_size);

#endif
