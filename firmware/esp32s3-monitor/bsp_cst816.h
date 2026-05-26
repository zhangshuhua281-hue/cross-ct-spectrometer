#ifndef BSP_CST816_H
#define BSP_CST816_H

#include <Arduino.h>
#include <Wire.h>

bool bsp_touch_init(TwoWire *wire, uint8_t rotation, uint16_t width, uint16_t height);
void bsp_touch_read(void);
bool bsp_touch_get_coordinates(uint16_t *x, uint16_t *y);

#endif
