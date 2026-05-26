#ifndef BSP_LED_H
#define BSP_LED_H

#include "stm32g4xx_hal.h"

#define BSP_LED_GPIO_PORT GPIOA
#define BSP_LED_GPIO_CLK_ENABLE() __HAL_RCC_GPIOA_CLK_ENABLE()
#define BSP_LED_PIN GPIO_PIN_5

void BSP_LED_Init(void);
void BSP_LED_Toggle(void);
void BSP_LED_Write(GPIO_PinState state);

#endif
