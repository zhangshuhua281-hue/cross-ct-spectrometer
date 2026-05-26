#ifndef BSP_UART_H
#define BSP_UART_H

#include <stdint.h>

#include "stm32g4xx_hal.h"

void BSP_UART_Init(void);
HAL_StatusTypeDef BSP_UART_InputReceiveByte(uint8_t *byte, uint32_t timeout_ms);
HAL_StatusTypeDef BSP_UART_InputReceive(uint8_t *data, uint16_t size, uint32_t timeout_ms);
HAL_StatusTypeDef BSP_UART_InputWrite(const uint8_t *data, uint16_t size, uint32_t timeout_ms);
HAL_StatusTypeDef BSP_UART_LinkReceiveByte(uint8_t *byte, uint32_t timeout_ms);
uint8_t BSP_UART_LinkTryReadByte(uint8_t *byte);
HAL_StatusTypeDef BSP_UART_OutputWrite(const uint8_t *data, uint16_t size, uint32_t timeout_ms);
HAL_StatusTypeDef BSP_UART_OutputWriteString(const char *text);
uint8_t BSP_UART_IsLinkWakeActive(void);
uint8_t BSP_UART_IsLinkDataReadyActive(void);
void BSP_UART_SetLinkDataReady(uint8_t active);
void BSP_UART_LinkIrqHandler(void);

#endif
