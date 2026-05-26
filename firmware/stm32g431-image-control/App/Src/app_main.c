#include "app_main.h"

#include <stdio.h>
#include <string.h>

#include "app_config.h"
#include "app_group_stats.h"
#include "app_pipeline.h"
#include "app_types.h"
#include "bsp_uart.h"

static AppFrame g_frame;
static uint32_t g_packet_seq = 0U;
static uint8_t g_next_replicate_id = 1U;
static char g_link_rx_line[APP_UART_RX_LINE_MAX];
static uint16_t g_link_rx_len = 0U;
static uint8_t g_link_rx_overflow = 0U;
static uint8_t g_last_replicate_id = 0U;
static uint16_t g_last_valid_count = 0U;

typedef struct
{
  uint8_t valid;
  uint8_t group_stats_valid;
  uint8_t group_id;
  uint8_t replicate_id;
  uint16_t point_count;
  uint16_t valid_count;
  uint16_t first_pixel;
  uint16_t curve_start_pixel;
  uint16_t curve_count;
  uint16_t peak_pixel_raw;
  uint16_t peak_pixel_smooth;
  float dark_adc;
  float intensity_mean;
  float intensity_max;
  AppGroupStats group_stats;
  uint16_t curve_q15[APP_DOWNSAMPLED_POINTS_MAX];
} AppDisplayPacket;

static AppDisplayPacket g_pending_packet;
static void queue_frame_packet(const AppFrame *frame, const AppGroupStats *stats);
static void emit_link_status(const char *token);
static void handle_link_command(const char *line);
static void poll_link_commands(void);

static void process_ready_frame(AppFrame *frame)
{
  const AppGroupStats *stats;
  uint8_t i;

  if (frame == NULL)
  {
    return;
  }

  AppGroupStats_Accumulate(frame);

  stats = NULL;
  for (i = 0U; i < AppGroupStats_GetCount(); ++i)
  {
    stats = AppGroupStats_GetByIndex(i);
    if ((stats != NULL) && (stats->group_id == frame->group_id))
    {
      break;
    }
  }

  queue_frame_packet(frame, stats);
}

static void copy_frame_to_packet(const AppFrame *frame, const AppGroupStats *stats)
{
  if (frame == NULL)
  {
    return;
  }

  memset(&g_pending_packet, 0, sizeof(g_pending_packet));
  g_pending_packet.valid = 1U;
  g_pending_packet.group_id = frame->group_id;
  g_pending_packet.replicate_id = frame->replicate_id;
  g_pending_packet.point_count = frame->point_count;
  g_pending_packet.valid_count = frame->valid_count;
  g_pending_packet.first_pixel = frame->first_pixel;
  g_pending_packet.curve_start_pixel = (uint16_t)(frame->first_pixel + APP_SKIP_FIRST_ROWS);
  g_pending_packet.curve_count = frame->downsampled_count;
  g_pending_packet.peak_pixel_raw = frame->peak_pixel_raw;
  g_pending_packet.peak_pixel_smooth = frame->peak_pixel_smooth;
  g_pending_packet.dark_adc = frame->dark_adc;
  g_pending_packet.intensity_mean = frame->intensity_mean;
  g_pending_packet.intensity_max = frame->intensity_max;
  g_last_replicate_id = frame->replicate_id;
  g_last_valid_count = frame->valid_count;
  memcpy(g_pending_packet.curve_q15,
         frame->smooth_norm_q15,
         (uint32_t)frame->downsampled_count * sizeof(frame->smooth_norm_q15[0]));

  if (stats != NULL)
  {
    g_pending_packet.group_stats_valid = 1U;
    g_pending_packet.group_stats = *stats;
  }
}

static void emit_info_line(const char *prefix, const char *text)
{
  char line[APP_UART_TX_LINE_MAX];

  snprintf(line, sizeof(line), "%s %s\r\n", prefix, text);
  (void)BSP_UART_OutputWriteString(line);
}

static void emit_link_status(const char *token)
{
  char line[APP_UART_TX_LINE_MAX];
  const uint8_t replicate_id = (g_pending_packet.valid != 0U) ? g_pending_packet.replicate_id : g_last_replicate_id;
  const uint16_t valid_count = (g_pending_packet.valid != 0U) ? g_pending_packet.valid_count : g_last_valid_count;

  snprintf(line,
           sizeof(line),
            "LINK STATUS T=%s READY=1 WAKE=%u DATA=%u PENDING=%u SEQ=%lu REP=%u VALID=%u\r\n",
            token,
            BSP_UART_IsLinkWakeActive(),
            BSP_UART_IsLinkDataReadyActive(),
            (g_pending_packet.valid != 0U) ? 1U : 0U,
            (unsigned long)g_packet_seq,
            replicate_id,
            valid_count);
  (void)BSP_UART_OutputWriteString(line);
}

static void handle_link_command(const char *line)
{
  static const char kLinkQueryPrefix[] = "LINK? T=";
  const char *token;

  if (line == NULL)
  {
    return;
  }

  if (strncmp(line, kLinkQueryPrefix, sizeof(kLinkQueryPrefix) - 1U) != 0)
  {
    return;
  }

  token = line + (sizeof(kLinkQueryPrefix) - 1U);
  if (token[0] == '\0')
  {
    return;
  }

  emit_link_status(token);
}

static void poll_link_commands(void)
{
  uint8_t byte = 0U;

  while (BSP_UART_LinkTryReadByte(&byte) != 0U)
  {
    if ((byte == '\r') || (byte == '\n'))
    {
      if ((g_link_rx_overflow == 0U) && (g_link_rx_len > 0U))
      {
        g_link_rx_line[g_link_rx_len] = '\0';
        handle_link_command(g_link_rx_line);
      }
      g_link_rx_len = 0U;
      g_link_rx_overflow = 0U;
      continue;
    }

    if (g_link_rx_overflow != 0U)
    {
      continue;
    }

    if (g_link_rx_len < (APP_UART_RX_LINE_MAX - 1U))
    {
      g_link_rx_line[g_link_rx_len++] = (char)byte;
    }
    else
    {
      g_link_rx_len = 0U;
      g_link_rx_overflow = 1U;
    }
  }
}

static void emit_pending_packet(void)
{
  uint16_t i;
  char line[APP_UART_TX_LINE_MAX];

  if (g_pending_packet.valid == 0U)
  {
    return;
  }

  g_packet_seq++;
  snprintf(line, sizeof(line), "PKT BEGIN SEQ=%lu\r\n", (unsigned long)g_packet_seq);
  (void)BSP_UART_OutputWriteString(line);

  snprintf(line,
           sizeof(line),
           "FRAME G=%u R=%u POINTS=%u VALID=%u FIRST=%u CURVE_FIRST=%u STEP=%u CURVE_COUNT=%u DARK=%.2f PEAK_RAW=%u PEAK_SMOOTH=%u IMEAN=%.2f IMAX=%.2f\r\n",
           g_pending_packet.group_id,
           g_pending_packet.replicate_id,
           g_pending_packet.point_count,
           g_pending_packet.valid_count,
           g_pending_packet.first_pixel,
           g_pending_packet.curve_start_pixel,
           APP_DOWNSAMPLE_STEP,
           g_pending_packet.curve_count,
           g_pending_packet.dark_adc,
           g_pending_packet.peak_pixel_raw,
           g_pending_packet.peak_pixel_smooth,
           g_pending_packet.intensity_mean,
           g_pending_packet.intensity_max);
  (void)BSP_UART_OutputWriteString(line);

  if (g_pending_packet.group_stats_valid != 0U)
  {
    snprintf(line,
             sizeof(line),
             "GROUP G=%u N=%u VALID=%u DCOUNT=%u CV=%.5f CORR_MEAN=%.5f CORR_MIN=%.5f PEAK_MEAN=%.2f PEAK_STD=%.2f\r\n",
             g_pending_packet.group_stats.group_id,
             g_pending_packet.group_stats.replicate_count,
             g_pending_packet.group_stats.valid_count,
             g_pending_packet.group_stats.downsampled_count,
             g_pending_packet.group_stats.mean_point_cv,
             g_pending_packet.group_stats.mean_pair_correlation,
             g_pending_packet.group_stats.min_pair_correlation,
             g_pending_packet.group_stats.mean_peak_pixel,
             g_pending_packet.group_stats.peak_pixel_std);
    (void)BSP_UART_OutputWriteString(line);
  }

  snprintf(line,
           sizeof(line),
           "CURVE_BEGIN FIRST=%u STEP=%u COUNT=%u\r\n",
           g_pending_packet.curve_start_pixel,
           APP_DOWNSAMPLE_STEP,
           g_pending_packet.curve_count);
  (void)BSP_UART_OutputWriteString(line);

  for (i = 0U; i < g_pending_packet.curve_count; ++i)
  {
    uint16_t pixel = (uint16_t)(g_pending_packet.curve_start_pixel + i * APP_DOWNSAMPLE_STEP);
    snprintf(line,
             sizeof(line),
             "CURVE %u %u\r\n",
             pixel,
             g_pending_packet.curve_q15[i]);
    (void)BSP_UART_OutputWriteString(line);
  }

  (void)BSP_UART_OutputWriteString("CURVE_END\r\n");
  (void)BSP_UART_OutputWriteString("PKT END\r\n");

  g_pending_packet.valid = 0U;
  BSP_UART_SetLinkDataReady(0U);
}

static void queue_frame_packet(const AppFrame *frame, const AppGroupStats *stats)
{
  copy_frame_to_packet(frame, stats);
  BSP_UART_SetLinkDataReady(1U);
  emit_pending_packet();
}

void App_OnFrameText(const char *frame_text)
{
  char error_text[48];
  AppPipelineStatus status;

  status = AppPipeline_ProcessAsciiLine(frame_text, &g_frame, error_text, sizeof(error_text));
  if (status == APP_PIPELINE_COLLECTING)
  {
    return;
  }
  if (status != APP_PIPELINE_READY)
  {
    emit_info_line("ERR", error_text);
    return;
  }

  process_ready_frame(&g_frame);
}

void App_OnFrameBinary(const uint8_t *frame_data, uint16_t frame_size)
{
  char error_text[48];
  AppPipelineStatus status;

  status = AppPipeline_ProcessF103Frame12Bit(frame_data,
                                             frame_size,
                                             APP_F103_DEFAULT_GROUP_ID,
                                             g_next_replicate_id,
                                             &g_frame,
                                             error_text,
                                             sizeof(error_text));
  if (status != APP_PIPELINE_READY)
  {
    emit_info_line("ERR", error_text);
    return;
  }

  process_ready_frame(&g_frame);
  g_next_replicate_id++;
  if (g_next_replicate_id > APP_GROUP_REPLICATE_MAX)
  {
    g_next_replicate_id = 1U;
    AppGroupStats_Init();
  }
}

void App_Init(void)
{
  BSP_UART_Init();
  AppPipeline_Init();
  AppGroupStats_Init();
  memset(&g_pending_packet, 0, sizeof(g_pending_packet));
  g_link_rx_len = 0U;
  g_link_rx_overflow = 0U;
  g_last_replicate_id = 0U;
  g_last_valid_count = 0U;
  BSP_UART_SetLinkDataReady(0U);

  /* DIAG: test PA2 pin as GPIO (USART2 TX) */
  {
    GPIO_InitTypeDef gpio = {0};
    gpio.Pin = GPIO_PIN_2;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOA, &gpio);
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_2, GPIO_PIN_RESET);  /* LOW ~500ms */
    for (volatile uint32_t i = 0U; i < 17000000U; i++) { __NOP(); }
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_2, GPIO_PIN_SET);     /* HIGH ~500ms */
    for (volatile uint32_t i = 0U; i < 17000000U; i++) { __NOP(); }
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_2, GPIO_PIN_RESET);   /* LOW ~500ms */
    for (volatile uint32_t i = 0U; i < 17000000U; i++) { __NOP(); }
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_2, GPIO_PIN_SET);     /* HIGH ~500ms */
    for (volatile uint32_t i = 0U; i < 17000000U; i++) { __NOP(); }
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_2, GPIO_PIN_RESET);   /* hold LOW for measurement */
  }

  emit_info_line("INFO", "STM32G431 image-control ready");
}

void App_Loop(void)
{
  uint8_t request = APP_F103_REQUEST_12BIT_CMD;

  emit_pending_packet();
  poll_link_commands();

  if (g_pending_packet.valid != 0U)
  {
    return;
  }

#if (APP_INPUT_MODE_F103_BINARY != 0U)
  emit_info_line("DBG1", "pre-write");
  if (BSP_UART_InputWrite(&request, 1U, 20U) != HAL_OK)
  {
    emit_info_line("ERR", "f103 request failed");
    return;
  }
  emit_info_line("DBG2", "post-write");

  if (BSP_UART_InputReceive((uint8_t *)g_frame.sample_x2, sizeof(g_frame.sample_x2), APP_F103_RX_TIMEOUT_MS) != HAL_OK)
  {
    emit_info_line("ERR", "f103 frame timeout");
    return;
  }
  emit_info_line("DBG3", "post-receive");

  App_OnFrameBinary((const uint8_t *)g_frame.sample_x2, (uint16_t)sizeof(g_frame.sample_x2));
#else
  {
    uint8_t byte = 0U;
    static char g_rx_line[APP_UART_RX_LINE_MAX];
    static uint16_t g_rx_len = 0U;

    if (BSP_UART_InputReceiveByte(&byte, 10U) != HAL_OK)
    {
      return;
    }

    if ((byte == '\r') || (byte == '\n'))
    {
      if (g_rx_len > 0U)
      {
        g_rx_line[g_rx_len] = '\0';
        App_OnFrameText(g_rx_line);
        g_rx_len = 0U;
      }
      return;
    }

    if (g_rx_len < (APP_UART_RX_LINE_MAX - 1U))
    {
      g_rx_line[g_rx_len++] = (char)byte;
    }
    else
    {
      g_rx_len = 0U;
      emit_info_line("ERR", "line too long");
    }
  }
#endif
}
