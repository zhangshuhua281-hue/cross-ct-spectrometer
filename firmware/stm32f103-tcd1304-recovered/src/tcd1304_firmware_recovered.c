/*
 * TCD1304 STM32F103C8T6 firmware, reconstructed from live flash dump.
 *
 * Important:
 * - This is not the original source tree.
 * - Names, structure and comments were reconstructed from the binary.
 * - The command protocol, timing table and data frame formatting are recovered
 *   from the live MCU image and are the most useful parts for future edits.
 * - Low-level timer waveforms are documented but should be validated on a scope
 *   before this file is used as a direct replacement firmware.
 */

#include <stdbool.h>
#include <stdint.h>

#define TCD1304_PIXEL_COUNT        3648u
#define TCD1304_RAW_SKIP_PIXELS   32u
#define TCD1304_RAW_SAMPLE_COUNT  3694u

#define CMD_READ_12BIT            0xA1u
#define CMD_READ_8BIT             0xA2u
#define CMD_READ_SUMMARY          0xA3u
#define CMD_INTEGRATION_FIRST     0xB1u
#define CMD_INTEGRATION_LAST      0xD8u

typedef struct {
    uint8_t command;
    uint32_t timing_b;
    uint32_t timing_c;
    uint32_t timing_a;
    uint32_t integration_us_x10;
} IntegrationPreset;

/*
 * Recovered command table.
 *
 * The integration time matches:
 *     integration_time_us = timing_a * timing_c / 72
 *
 * integration_us_x10 stores microseconds * 10 so 94.4 ms can be represented.
 */
static const IntegrationPreset kIntegrationPresets[] = {
    {0xB1, 15000, 36,   20,      100},
    {0xB2, 15000, 36,   40,      200},
    {0xB3, 15000, 36,   100,     500},
    {0xB4, 15000, 36,   120,     600},
    {0xB5, 15000, 36,   150,     750},
    {0xB6, 15000, 36,   200,     1000},
    {0xB7, 15000, 36,   1000,    5000},
    {0xB8, 15000, 36,   2500,    12500},
    {0xB9, 15000, 36,   5000,    25000},
    {0xBA, 15000, 36,   15000,   75000},
    {0xBB, 20000, 36,   20000,   100000},
    {0xBC, 30000, 36,   30000,   150000},
    {0xBD, 40000, 36,   40000,   200000},
    {0xBE, 50000, 36,   50000,   250000},
    {0xBF, 60000, 36,   60000,   300000},
    {0xC0, 35000, 72,   35000,   350000},
    {0xC1, 40000, 72,   40000,   400000},
    {0xC2, 45000, 72,   45000,   450000},
    {0xC3, 50000, 72,   50000,   500000},
    {0xC4, 55000, 72,   55000,   550000},
    {0xC5, 60000, 72,   60000,   600000},
    {0xC6, 65000, 72,   65000,   650000},
    {0xC7, 35000, 144,  35000,   700000},
    {0xC8, 37500, 144,  37500,   750000},
    {0xC9, 40000, 144,  40000,   800000},
    {0xCA, 42500, 144,  42500,   850000},
    {0xCB, 45000, 144,  45000,   900000},
    {0xCC, 47200, 144,  47200,   944000},
    {0xCD, 50000, 144,  50000,   1000000},
    {0xCE, 50000, 288,  50000,   2000000},
    {0xCF, 37500, 576,  37500,   3000000},
    {0xD0, 25000, 1152, 25000,   4000000},
    {0xD1, 31250, 1152, 31250,   5000000},
    {0xD2, 37500, 1152, 37500,   6000000},
    {0xD3, 43750, 1152, 43750,   7000000},
    {0xD4, 50000, 1152, 50000,   8000000},
    {0xD5, 56250, 1152, 56250,   9000000},
    {0xD6, 62500, 1152, 62500,   10000000},
    {0xD7, 62500, 2304, 62500,   20000000},
    {0xD8, 46875, 4608, 46875,   30000000},
};

static volatile uint32_t g_timing_a = 100;
static volatile uint32_t g_timing_b = 15000;
static volatile uint32_t g_timing_c = 36;
static volatile uint8_t g_dma_frame_done;

static volatile uint16_t g_adc_raw[TCD1304_RAW_SAMPLE_COUNT];
static volatile uint16_t g_tx_samples[TCD1304_PIXEL_COUNT];

static uint16_t g_max_pos;
static uint16_t g_min_pos;
static uint16_t g_average;
static uint16_t g_max_value;
static uint16_t g_min_value;

/*
 * Hardware functions recovered from the binary but not expanded here.
 * Port these to STM32 StdPeriph/HAL if rebuilding a complete project.
 */
static void board_clock_init_72mhz(void);
static void board_delay_init(void);
static void board_gpio_adc_dma_init(void);
static void board_tcd1304_timer_init(void);
static void board_usart1_init_921600(void);
static void board_restart_capture_timers(void);
static void board_adc_stop(void);
static bool board_dma1_channel1_transfer_complete(void);
static void board_dma1_channel1_clear_transfer_complete(void);
static bool board_usart1_rxne_pending(void);
static uint8_t board_usart1_read_byte(void);
static void board_usart1_send_byte(uint8_t value);

static void set_integration_preset(uint8_t command)
{
    unsigned int i;

    for (i = 0; i < (sizeof(kIntegrationPresets) / sizeof(kIntegrationPresets[0])); ++i) {
        if (kIntegrationPresets[i].command == command) {
            g_timing_b = kIntegrationPresets[i].timing_b;
            g_timing_c = kIntegrationPresets[i].timing_c;
            g_timing_a = kIntegrationPresets[i].timing_a;
            board_restart_capture_timers();
            return;
        }
    }
}

static void copy_raw_to_tx_12bit(void)
{
    uint16_t i;

    for (i = 0; i < TCD1304_PIXEL_COUNT; ++i) {
        g_tx_samples[i] = g_adc_raw[i + TCD1304_RAW_SKIP_PIXELS];
    }
}

static void copy_raw_to_tx_8bit(void)
{
    uint16_t i;

    for (i = 0; i < TCD1304_PIXEL_COUNT; ++i) {
        g_tx_samples[i] = (uint16_t)(g_adc_raw[i + TCD1304_RAW_SKIP_PIXELS] >> 4);
    }
}

static uint16_t find_max_pos(void)
{
    uint16_t i;
    uint16_t pos = 0;
    uint16_t max_value = 0;

    for (i = 100; i < TCD1304_PIXEL_COUNT; ++i) {
        if (g_tx_samples[i] > max_value) {
            max_value = g_tx_samples[i];
            pos = i;
        }
    }

    g_max_value = max_value;
    return pos;
}

static uint16_t find_min_pos(void)
{
    uint16_t i;
    uint16_t pos = 0;
    uint16_t min_value = 4096;

    for (i = 100; i < TCD1304_PIXEL_COUNT; ++i) {
        if (g_tx_samples[i] < min_value) {
            min_value = g_tx_samples[i];
            pos = i;
        }
    }

    g_min_value = min_value;
    return pos;
}

static uint16_t calculate_average(void)
{
    uint16_t i;
    uint32_t sum = 0;

    for (i = 1000; i < 2000; ++i) {
        sum += g_tx_samples[i];
    }

    return (uint16_t)(sum / 1000u);
}

static uint16_t send_base100_word(uint16_t value)
{
    uint8_t high = (uint8_t)(value / 100u);
    uint8_t low = (uint8_t)(value % 100u);

    board_usart1_send_byte(high);
    board_usart1_send_byte(low);

    return (uint16_t)(high + low);
}

static void send_12bit_frame(void)
{
    uint16_t i;

    copy_raw_to_tx_12bit();
    for (i = 0; i < TCD1304_PIXEL_COUNT; ++i) {
        uint16_t value = g_tx_samples[i];
        board_usart1_send_byte((uint8_t)(value & 0xFFu));
        board_usart1_send_byte((uint8_t)(value >> 8));
    }
}

static void send_8bit_frame(void)
{
    uint16_t i;

    copy_raw_to_tx_8bit();
    for (i = 0; i < TCD1304_PIXEL_COUNT; ++i) {
        board_usart1_send_byte((uint8_t)g_tx_samples[i]);
    }
}

static void send_summary_frame(void)
{
    uint16_t checksum = 0;

    copy_raw_to_tx_12bit();

    g_max_pos = find_max_pos();
    g_min_pos = find_min_pos();
    g_average = calculate_average();

    board_usart1_send_byte(0xFEu);
    checksum += send_base100_word(g_max_pos);
    checksum += send_base100_word(g_min_pos);
    checksum += send_base100_word(g_average);
    checksum += send_base100_word(g_max_value);
    checksum += send_base100_word(g_min_value);
    board_usart1_send_byte((uint8_t)checksum);
}

void USART1_IRQHandler(void)
{
    uint8_t command;

    if (!board_usart1_rxne_pending()) {
        return;
    }

    command = board_usart1_read_byte();
    if (command == CMD_READ_12BIT) {
        send_12bit_frame();
    } else if (command == CMD_READ_8BIT) {
        send_8bit_frame();
    } else if (command == CMD_READ_SUMMARY) {
        send_summary_frame();
    } else {
        set_integration_preset(command);
    }
}

void DMA1_Channel1_IRQHandler(void)
{
    if (board_dma1_channel1_transfer_complete()) {
        board_dma1_channel1_clear_transfer_complete();
        g_dma_frame_done = 1;
        board_adc_stop();
    }
}

int main(void)
{
    g_timing_b = 15000;
    g_timing_c = 36;
    g_timing_a = 100;

    board_clock_init_72mhz();
    board_delay_init();
    board_gpio_adc_dma_init();
    board_tcd1304_timer_init();
    board_usart1_init_921600();
    board_restart_capture_timers();

    for (;;) {
        if (g_dma_frame_done) {
            g_dma_frame_done = 0;
        }
    }
}

/*
 * Recovered hardware notes for the board_* functions:
 *
 * - USART1, PA9 TX as AF_PP, PA10 RX as floating input, 921600 8N1.
 * - ADC1 regular channel 2, DMA1 Channel1, halfword samples.
 * - DMA source is ADC1->DR (0x4001244C), destination is g_adc_raw.
 * - DMA transfer count in the live image is 0xE6E samples.
 * - The transmitted 3648 pixels start at raw sample offset 32.
 * - TIM2 IRQ is used by the capture timing sequence.
 * - Recovered PWM/timer pins include PA8, PA0, PB0 and PB9.
 * - Integration command changes write timing_b, timing_c and timing_a, then
 *   restart the timer sequence.
 */

