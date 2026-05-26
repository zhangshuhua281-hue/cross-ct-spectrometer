#include "bsp_uart.h"

#include "app_config.h"
#include "main.h"

static UART_HandleTypeDef g_uart_in;
static UART_HandleTypeDef g_uart_out;
static uint8_t g_link_rx_irq_byte = 0U;

#define BSP_LINK_RX_BUFFER_SIZE       64U

static volatile uint8_t g_link_rx_buffer[BSP_LINK_RX_BUFFER_SIZE] __attribute__((section(".ccm_ram")));
static volatile uint16_t g_link_rx_head = 0U;
static volatile uint16_t g_link_rx_tail = 0U;

#define BSP_LINK_WAKE_GPIO_PORT      GPIOA
#define BSP_LINK_WAKE_GPIO_PIN       GPIO_PIN_0
#define BSP_LINK_DATA_RDY_GPIO_PORT  GPIOA
#define BSP_LINK_DATA_RDY_GPIO_PIN   GPIO_PIN_5

static void link_rx_irq_rearm(void)
{
  (void)HAL_UART_Receive_IT(&g_uart_out, &g_link_rx_irq_byte, 1U);
}

static void uart_gpio_init(void)
{
  GPIO_InitTypeDef gpio = {0};

  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_USART2_CLK_ENABLE();
  __HAL_RCC_USART3_CLK_ENABLE();

  gpio.Mode = GPIO_MODE_AF_PP;
  gpio.Pull = GPIO_PULLUP;
  gpio.Speed = GPIO_SPEED_FREQ_HIGH;

  gpio.Pin = GPIO_PIN_2 | GPIO_PIN_3;
  gpio.Alternate = GPIO_AF7_USART2;
  HAL_GPIO_Init(GPIOA, &gpio);

  gpio.Pin = GPIO_PIN_10 | GPIO_PIN_11;
  gpio.Alternate = GPIO_AF7_USART3;
  HAL_GPIO_Init(GPIOB, &gpio);

  gpio.Pin = BSP_LINK_WAKE_GPIO_PIN;
  gpio.Mode = GPIO_MODE_INPUT;
  gpio.Pull = GPIO_PULLDOWN;
  gpio.Speed = GPIO_SPEED_FREQ_LOW;
  gpio.Alternate = 0U;
  HAL_GPIO_Init(BSP_LINK_WAKE_GPIO_PORT, &gpio);

  gpio.Pin = BSP_LINK_DATA_RDY_GPIO_PIN;
  gpio.Mode = GPIO_MODE_OUTPUT_PP;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(BSP_LINK_DATA_RDY_GPIO_PORT, &gpio);
  HAL_GPIO_WritePin(BSP_LINK_DATA_RDY_GPIO_PORT, BSP_LINK_DATA_RDY_GPIO_PIN, GPIO_PIN_RESET);
}

static void uart_common_init(UART_HandleTypeDef *huart, USART_TypeDef *instance)
{
  huart->Instance = instance;
  huart->Init.BaudRate = (instance == USART2) ? APP_UART_IN_BAUDRATE : APP_UART_OUT_BAUDRATE;
  huart->Init.WordLength = UART_WORDLENGTH_8B;
  huart->Init.StopBits = UART_STOPBITS_1;
  huart->Init.Parity = UART_PARITY_NONE;
  huart->Init.Mode = UART_MODE_TX_RX;
  huart->Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart->Init.OverSampling = UART_OVERSAMPLING_16;
  huart->Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart->Init.ClockPrescaler = UART_PRESCALER_DIV1;
  huart->AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(huart) != HAL_OK)
  {
    Error_Handler();
  }

  /* For USART2 only: skip FIFO Ex functions — they may corrupt the TX state
     after the disable/re-enable cycle.  FIFO is already disabled after reset. */
  if (instance == USART2)
  {
    return;
  }

  if (HAL_UARTEx_SetTxFifoThreshold(huart, UART_TXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_SetRxFifoThreshold(huart, UART_RXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_DisableFifoMode(huart) != HAL_OK)
  {
    Error_Handler();
  }
}

void BSP_UART_Init(void)
{
  uart_gpio_init();
  uart_common_init(&g_uart_in, USART2);
  uart_common_init(&g_uart_out, USART3);
  g_link_rx_head = 0U;
  g_link_rx_tail = 0U;
  __HAL_UART_CLEAR_FLAG(&g_uart_out, UART_CLEAR_OREF | UART_CLEAR_NEF | UART_CLEAR_PEF | UART_CLEAR_FEF);
  HAL_NVIC_SetPriority(USART3_IRQn, 5U, 0U);
  HAL_NVIC_EnableIRQ(USART3_IRQn);
  link_rx_irq_rearm();
}

HAL_StatusTypeDef BSP_UART_InputReceiveByte(uint8_t *byte, uint32_t timeout_ms)
{
  return HAL_UART_Receive(&g_uart_in, byte, 1U, timeout_ms);
}

HAL_StatusTypeDef BSP_UART_InputReceive(uint8_t *data, uint16_t size, uint32_t timeout_ms)
{
  return HAL_UART_Receive(&g_uart_in, data, size, timeout_ms);
}

HAL_StatusTypeDef BSP_UART_InputWrite(const uint8_t *data, uint16_t size, uint32_t timeout_ms)
{
  return HAL_UART_Transmit(&g_uart_in, (uint8_t *)data, size, timeout_ms);
}

HAL_StatusTypeDef BSP_UART_LinkReceiveByte(uint8_t *byte, uint32_t timeout_ms)
{
  return HAL_UART_Receive(&g_uart_out, byte, 1U, timeout_ms);
}

uint8_t BSP_UART_LinkTryReadByte(uint8_t *byte)
{
  uint16_t tail;

  if (byte == NULL)
  {
    return 0U;
  }

  tail = g_link_rx_tail;
  if (tail == g_link_rx_head)
  {
    return 0U;
  }

  *byte = g_link_rx_buffer[tail];
  tail++;
  if (tail >= BSP_LINK_RX_BUFFER_SIZE)
  {
    tail = 0U;
  }
  g_link_rx_tail = tail;
  return 1U;
}

HAL_StatusTypeDef BSP_UART_OutputWrite(const uint8_t *data, uint16_t size, uint32_t timeout_ms)
{
  return HAL_UART_Transmit(&g_uart_out, (uint8_t *)data, size, timeout_ms);
}

HAL_StatusTypeDef BSP_UART_OutputWriteString(const char *text)
{
  uint16_t size = 0U;
  while (text[size] != '\0')
  {
    size++;
  }
  return BSP_UART_OutputWrite((const uint8_t *)text, size, 1000U);
}

uint8_t BSP_UART_IsLinkWakeActive(void)
{
  return (HAL_GPIO_ReadPin(BSP_LINK_WAKE_GPIO_PORT, BSP_LINK_WAKE_GPIO_PIN) == GPIO_PIN_SET) ? 1U : 0U;
}

uint8_t BSP_UART_IsLinkDataReadyActive(void)
{
  return (HAL_GPIO_ReadPin(BSP_LINK_DATA_RDY_GPIO_PORT, BSP_LINK_DATA_RDY_GPIO_PIN) == GPIO_PIN_SET) ? 1U : 0U;
}

void BSP_UART_SetLinkDataReady(uint8_t active)
{
  HAL_GPIO_WritePin(BSP_LINK_DATA_RDY_GPIO_PORT,
                    BSP_LINK_DATA_RDY_GPIO_PIN,
                    (active != 0U) ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void BSP_UART_LinkIrqHandler(void)
{
  HAL_UART_IRQHandler(&g_uart_out);
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  if ((huart == NULL) || (huart->Instance != USART3))
  {
    return;
  }

  {
    uint16_t next_head = (uint16_t)(g_link_rx_head + 1U);
    if (next_head >= BSP_LINK_RX_BUFFER_SIZE)
    {
      next_head = 0U;
    }

    if (next_head != g_link_rx_tail)
    {
      g_link_rx_buffer[g_link_rx_head] = g_link_rx_irq_byte;
      g_link_rx_head = next_head;
    }
  }

  link_rx_irq_rearm();
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
  if ((huart == NULL) || (huart->Instance != USART3))
  {
    return;
  }

  __HAL_UART_CLEAR_FLAG(huart, UART_CLEAR_OREF | UART_CLEAR_NEF | UART_CLEAR_PEF | UART_CLEAR_FEF);
  link_rx_irq_rearm();
}
