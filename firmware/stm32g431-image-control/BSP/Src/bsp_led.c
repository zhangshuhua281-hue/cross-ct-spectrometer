#include "bsp_led.h"

void BSP_LED_Init(void)
{
  GPIO_InitTypeDef gpio = {0};

  BSP_LED_GPIO_CLK_ENABLE();

  gpio.Pin = BSP_LED_PIN;
  gpio.Mode = GPIO_MODE_OUTPUT_PP;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(BSP_LED_GPIO_PORT, &gpio);

  HAL_GPIO_WritePin(BSP_LED_GPIO_PORT, BSP_LED_PIN, GPIO_PIN_RESET);
}

void BSP_LED_Toggle(void)
{
  HAL_GPIO_TogglePin(BSP_LED_GPIO_PORT, BSP_LED_PIN);
}

void BSP_LED_Write(GPIO_PinState state)
{
  HAL_GPIO_WritePin(BSP_LED_GPIO_PORT, BSP_LED_PIN, state);
}
