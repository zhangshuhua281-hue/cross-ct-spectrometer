#include "main.h"
#include "stm32g4xx_it.h"
#include "bsp_uart.h"

void NMI_Handler(void)
{
}

void HardFault_Handler(void)
{
  Error_Handler();
}

void MemManage_Handler(void)
{
  Error_Handler();
}

void BusFault_Handler(void)
{
  Error_Handler();
}

void UsageFault_Handler(void)
{
  Error_Handler();
}

void SVC_Handler(void)
{
}

void DebugMon_Handler(void)
{
}

void PendSV_Handler(void)
{
}

void SysTick_Handler(void)
{
  HAL_IncTick();
}

void USART3_IRQHandler(void)
{
  BSP_UART_LinkIrqHandler();
}
