#ifndef APP_MAIN_H
#define APP_MAIN_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void App_Init(void);
void App_Loop(void);
void App_OnFrameText(const char *frame_text);
void App_OnFrameBinary(const uint8_t *frame_data, uint16_t frame_size);

#ifdef __cplusplus
}
#endif

#endif
