#include <Arduino.h>
#include <lvgl.h>
#include <Arduino_GFX_Library.h>
#include <Preferences.h>
#include <Wire.h>
#include <WiFi.h>
#include <string>
#if defined(CONFIG_BLUEDROID_ENABLED)
#include <BLEDevice.h>
#endif
#include "bsp_cst816.h"

LV_FONT_DECLARE(ui_font_cn_16);

#define EXAMPLE_PIN_NUM_LCD_SCLK 39
#define EXAMPLE_PIN_NUM_LCD_MOSI 38
#define EXAMPLE_PIN_NUM_LCD_MISO 40
#define EXAMPLE_PIN_NUM_LCD_DC 42
#define EXAMPLE_PIN_NUM_LCD_RST -1
#define EXAMPLE_PIN_NUM_LCD_CS 45
#define EXAMPLE_PIN_NUM_LCD_BL 1
#define EXAMPLE_PIN_NUM_TP_SDA 48
#define EXAMPLE_PIN_NUM_TP_SCL 47

#define EXAMPLE_LCD_ROTATION 1
#define EXAMPLE_LCD_H_RES 240
#define EXAMPLE_LCD_V_RES 320

#define LEDC_FREQ 5000
#define LEDC_TIMER_10_BIT 10

#define STM32_LINK_BAUD 115200
// Default UART pins for the STM32 display link.
// Change these two macros if your ESP32-S3 board is wired to a different header.
#define STM32_LINK_RX_PIN 44
#define STM32_LINK_TX_PIN 43
#define STM32_DATA_RDY_PIN 18
#define STM32_WAKE_PIN 21

#define MONITOR_RX_LINE_MAX 256
#define MONITOR_CURVE_MAX 240
#define MONITOR_CHART_Y_MAX 1000
#define UI_FONT_CN (&ui_font_cn_16)
#define MONITOR_PREFS_NAMESPACE "g431mon"
#define LINK_STATUS_TOKEN_MAX 16
#define LINK_STATUS_AUTO_INTERVAL_MS 3000U
#define LINK_STATUS_TIMEOUT_MS 1200U
#define LINK_STATUS_STALE_MS 8000U

#define MONITOR_SERIAL_LOG_LINES 64
#define MONITOR_SERIAL_LINE_MAX 128

HardwareSerial Stm32Link(1);
Preferences g_preferences;

Arduino_DataBus *bus = new Arduino_ESP32SPI(
  EXAMPLE_PIN_NUM_LCD_DC,
  EXAMPLE_PIN_NUM_LCD_CS,
  EXAMPLE_PIN_NUM_LCD_SCLK,
  EXAMPLE_PIN_NUM_LCD_MOSI,
  EXAMPLE_PIN_NUM_LCD_MISO);

Arduino_GFX *gfx = new Arduino_ST7789(
  bus,
  EXAMPLE_PIN_NUM_LCD_RST,
  EXAMPLE_LCD_ROTATION,
  true,
  EXAMPLE_LCD_H_RES,
  EXAMPLE_LCD_V_RES);

typedef struct {
  bool valid;
  uint32_t seq;
  uint8_t groupId;
  uint8_t replicateId;
  uint16_t pointCount;
  uint16_t validCount;
  uint16_t firstPixel;
  uint16_t curveFirstPixel;
  uint16_t curveStep;
  uint16_t curveCount;
  float darkAdc;
  uint16_t peakRawPixel;
  uint16_t peakSmoothPixel;
  float intensityMean;
  float intensityMax;
  bool groupValid;
  uint8_t groupStatsId;
  uint8_t groupReplicateCount;
  uint16_t groupValidCount;
  uint16_t groupDownsampledCount;
  float meanPointCv;
  float corrMean;
  float corrMin;
  float peakMean;
  float peakStd;
  uint16_t curvePixels[MONITOR_CURVE_MAX];
  uint16_t curveQ15[MONITOR_CURVE_MAX];
  uint32_t receivedAtMs;
} MonitorPacket;

typedef struct {
  bool inPacket;
  bool curveOpen;
  uint16_t expectedCurveCount;
  uint16_t receivedCurveCount;
  char lastInfo[96];
  char lastError[96];
} ParserState;

typedef enum : uint8_t {
  LINK_QUERY_IDLE = 0,
  LINK_QUERY_WAITING = 1,
  LINK_QUERY_ONLINE = 2,
  LINK_QUERY_TIMEOUT = 3
} LinkQueryState;

typedef struct {
  LinkQueryState state;
  bool requestPending;
  bool autoEnabled;
  bool ready;
  bool wake;
  bool data;
  bool pending;
  uint32_t seq;
  uint32_t replicate;
  uint32_t valid;
  uint32_t sentAtMs;
  uint32_t repliedAtMs;
  uint32_t lastOkAtMs;
  uint32_t roundTripMs;
  uint32_t timeoutCount;
  uint32_t requestCounter;
  char pendingToken[LINK_STATUS_TOKEN_MAX];
  char lastToken[LINK_STATUS_TOKEN_MAX];
} LinkStatusState;

static MonitorPacket g_workPacket;
static MonitorPacket g_livePacket;
static ParserState g_parser;
static LinkStatusState g_linkStatus;

static char g_rxLine[MONITOR_RX_LINE_MAX];
static size_t g_rxLineLen = 0;
static bool g_livePacketDirty = false;
static uint32_t g_lastStatusRefreshMs = 0;
static uint32_t g_lastLinkStatusAutoMs = 0;

static uint32_t screenWidth = 0;
static uint32_t screenHeight = 0;
static uint32_t bufSize = 0;
static lv_color_t *dispDrawBuf = nullptr;
static lv_disp_draw_buf_t drawBuf;
static lv_disp_drv_t dispDrv;
static lv_indev_drv_t indevDrv;
static uint32_t g_lastLvglTickMs = 0;
static bool g_touchReady = false;
static uint32_t g_lastTouchRetryMs = 0;
static bool g_linkStreaming = true;
static bool g_preferencesReady = false;

static char g_wifiSsid[33] = "waveshare";
static char g_wifiPassword[65] = "12345678";
static char g_bleName[32] = "G431-Monitor";
static bool g_wifiAutoConnect = false;
static bool g_bleAutoStart = false;
#if defined(CONFIG_BLUEDROID_ENABLED)
static bool g_bleAdvertisingActive = false;
#endif

enum UiPage : uint8_t {
  UI_PAGE_ACQUIRE = 0,
  UI_PAGE_CURVE = 1,
  UI_PAGE_LINK = 2,
  UI_PAGE_SERIAL = 3,
  UI_PAGE_COUNT
};

static UiPage g_currentPage = UI_PAGE_ACQUIRE;

static lv_obj_t *labelTitle = nullptr;
static lv_obj_t *labelLink = nullptr;
static lv_obj_t *labelAcquireState = nullptr;
static lv_obj_t *labelAcquireG431 = nullptr;
static lv_obj_t *labelAcquireFrame = nullptr;
static lv_obj_t *labelAcquireStats = nullptr;
static lv_obj_t *labelAcquireGroup = nullptr;
static lv_obj_t *labelAcquireInfo = nullptr;
static lv_obj_t *labelCurve = nullptr;
static lv_obj_t *labelWifiStatus = nullptr;
static lv_obj_t *labelBleStatus = nullptr;
static lv_obj_t *chartCurve = nullptr;
static lv_chart_series_t *chartSeries = nullptr;
static lv_coord_t chartPoints[MONITOR_CURVE_MAX];
static lv_obj_t *navButtons[UI_PAGE_COUNT] = { nullptr };
static lv_obj_t *pageContainers[UI_PAGE_COUNT] = { nullptr };
static lv_obj_t *taWifiSsid = nullptr;
static lv_obj_t *taWifiPassword = nullptr;
static lv_obj_t *taBleName = nullptr;
static lv_obj_t *kbInput = nullptr;

static char g_serialLog[MONITOR_SERIAL_LOG_LINES][MONITOR_SERIAL_LINE_MAX];
static uint8_t g_serialLogHead = 0;
static uint8_t g_serialLogCount = 0;
static lv_obj_t *labelSerialLog = nullptr;
static bool g_serialLogDirty = false;

static void updatePageTitle() {
  if (labelTitle == nullptr) {
    return;
  }

  switch (g_currentPage) {
    case UI_PAGE_ACQUIRE:
      lv_label_set_text(labelTitle, "采集控制");
      break;
    case UI_PAGE_CURVE:
      lv_label_set_text(labelTitle, "曲线显示");
      break;
    case UI_PAGE_LINK:
      lv_label_set_text(labelTitle, "连接设置");
      break;
    case UI_PAGE_SERIAL:
      lv_label_set_text(labelTitle, "串口信息");
      break;
    default:
      lv_label_set_text(labelTitle, "STM32 G431");
      break;
  }
}

static void monitorClearPacket(MonitorPacket *packet) {
  if (packet == nullptr) {
    return;
  }
  memset(packet, 0, sizeof(*packet));
}

static void parserSetText(char *dst, size_t dstSize, const char *src) {
  if ((dst == nullptr) || (dstSize == 0U)) {
    return;
  }
  if (src == nullptr) {
    dst[0] = '\0';
    return;
  }
  strncpy(dst, src, dstSize - 1U);
  dst[dstSize - 1U] = '\0';
}

static bool beginPreferences() {
  if (g_preferencesReady) {
    return true;
  }

  g_preferencesReady = g_preferences.begin(MONITOR_PREFS_NAMESPACE, false);
  if (!g_preferencesReady) {
    Serial.println("Preferences begin failed");
  }
  return g_preferencesReady;
}

static void loadPersistentSettings() {
  if (!beginPreferences()) {
    return;
  }

  parserSetText(g_wifiSsid,
                sizeof(g_wifiSsid),
                g_preferences.getString("wifi_ssid", g_wifiSsid).c_str());
  parserSetText(g_wifiPassword,
                sizeof(g_wifiPassword),
                g_preferences.getString("wifi_pass", g_wifiPassword).c_str());
  parserSetText(g_bleName,
                sizeof(g_bleName),
                g_preferences.getString("ble_name", g_bleName).c_str());
  g_wifiAutoConnect = g_preferences.getBool("wifi_auto", false);
  g_bleAutoStart = g_preferences.getBool("ble_auto", false);
}

static void saveWifiSettings() {
  if (!beginPreferences()) {
    return;
  }

  g_preferences.putString("wifi_ssid", g_wifiSsid);
  g_preferences.putString("wifi_pass", g_wifiPassword);
  g_preferences.putBool("wifi_auto", g_wifiAutoConnect);
}

static void saveBleSettings() {
  if (!beginPreferences()) {
    return;
  }

  g_preferences.putString("ble_name", g_bleName);
  g_preferences.putBool("ble_auto", g_bleAutoStart);
}

static void linkStatusSetState(LinkQueryState state) {
  g_linkStatus.state = state;
}

static bool linkStatusIsOnline(uint32_t now) {
  return (g_linkStatus.lastOkAtMs != 0U) && ((now - g_linkStatus.lastOkAtMs) <= LINK_STATUS_STALE_MS);
}

static void sendLinkStatusQuery(bool manualRequest) {
  char line[48];

  if (g_linkStatus.requestPending) {
    return;
  }

  ++g_linkStatus.requestCounter;
  snprintf(g_linkStatus.pendingToken,
           sizeof(g_linkStatus.pendingToken),
           "%08lX",
           (unsigned long)(g_linkStatus.requestCounter & 0xffffffffUL));
  snprintf(line, sizeof(line), "LINK? T=%s\r\n", g_linkStatus.pendingToken);
  Stm32Link.print(line);
  g_linkStatus.requestPending = true;
  g_linkStatus.sentAtMs = millis();
  linkStatusSetState(LINK_QUERY_WAITING);

  if (manualRequest) {
    parserSetText(g_parser.lastInfo, sizeof(g_parser.lastInfo), "Link status query sent");
  }
}

static bool processLinkStatusLine(const char *line) {
  char token[LINK_STATUS_TOKEN_MAX] = { 0 };
  unsigned int ready = 0U;
  unsigned int wake = 0U;
  unsigned int data = 0U;
  unsigned int pending = 0U;
  unsigned long seq = 0UL;
  unsigned long replicate = 0UL;
  unsigned long valid = 0UL;

  if (line == nullptr) {
    return false;
  }

  if (sscanf(line,
             "LINK STATUS T=%15s READY=%u WAKE=%u DATA=%u PENDING=%u SEQ=%lu REP=%lu VALID=%lu",
             token,
             &ready,
             &wake,
             &data,
             &pending,
             &seq,
             &replicate,
             &valid)
      != 8) {
    return false;
  }

  parserSetText(g_linkStatus.lastToken, sizeof(g_linkStatus.lastToken), token);
  g_linkStatus.ready = (ready != 0U);
  g_linkStatus.wake = (wake != 0U);
  g_linkStatus.data = (data != 0U);
  g_linkStatus.pending = (pending != 0U);
  g_linkStatus.seq = (uint32_t)seq;
  g_linkStatus.replicate = (uint32_t)replicate;
  g_linkStatus.valid = (uint32_t)valid;
  g_linkStatus.repliedAtMs = millis();

  if (g_linkStatus.requestPending && (strcmp(token, g_linkStatus.pendingToken) == 0)) {
    g_linkStatus.roundTripMs = g_linkStatus.repliedAtMs - g_linkStatus.sentAtMs;
    g_linkStatus.lastOkAtMs = g_linkStatus.repliedAtMs;
    g_linkStatus.requestPending = false;
    linkStatusSetState(LINK_QUERY_ONLINE);
    g_parser.lastInfo[0] = '\0';
    g_parser.lastError[0] = '\0';
  } else if (!g_linkStatus.requestPending) {
    g_linkStatus.roundTripMs = 0U;
    g_linkStatus.lastOkAtMs = g_linkStatus.repliedAtMs;
    linkStatusSetState(LINK_QUERY_ONLINE);
  } else {
    parserSetText(g_parser.lastError, sizeof(g_parser.lastError), "Link status token mismatch");
  }

  return true;
}

static void serviceLinkStatusQuery() {
  const uint32_t now = millis();

  if (g_linkStatus.requestPending) {
    if ((now - g_linkStatus.sentAtMs) > LINK_STATUS_TIMEOUT_MS) {
      g_linkStatus.requestPending = false;
      g_linkStatus.roundTripMs = 0U;
      g_linkStatus.timeoutCount++;
      linkStatusSetState(LINK_QUERY_TIMEOUT);
      parserSetText(g_parser.lastError, sizeof(g_parser.lastError), "Link status timeout");
    }
    return;
  }

  if (g_linkStatus.autoEnabled && ((now - g_lastLinkStatusAutoMs) >= LINK_STATUS_AUTO_INTERVAL_MS)) {
    g_lastLinkStatusAutoMs = now;
    sendLinkStatusQuery(false);
  }
}

static void monitorCommitPacket() {
  if (!g_parser.inPacket || !g_workPacket.valid) {
    return;
  }

  if (g_parser.expectedCurveCount != g_parser.receivedCurveCount) {
    parserSetText(g_parser.lastError, sizeof(g_parser.lastError), "曲线点数不匹配");
    return;
  }

  g_workPacket.receivedAtMs = millis();
  g_livePacket = g_workPacket;
  g_parser.lastError[0] = '\0';
  g_livePacketDirty = true;
}

static void processSerialLine(const char *line) {
  unsigned long seq = 0;
  unsigned int tmpGroup = 0;
  unsigned int tmpReplicate = 0;
  unsigned int tmpCount = 0;
  unsigned int tmpValid = 0;
  unsigned int tmpFirst = 0;
  unsigned int tmpCurveFirst = 0;
  unsigned int tmpStep = 0;
  unsigned int tmpCurveCount = 0;
  float tmpDark = 0.0f;
  unsigned int tmpPeakRaw = 0;
  unsigned int tmpPeakSmooth = 0;
  float tmpMean = 0.0f;
  float tmpMax = 0.0f;
  float tmpCv = 0.0f;
  float tmpCorrMean = 0.0f;
  float tmpCorrMin = 0.0f;
  float tmpPeakMean = 0.0f;
  float tmpPeakStd = 0.0f;
  unsigned int tmpPixel = 0;
  unsigned int tmpQ15 = 0;

  if ((line == nullptr) || (line[0] == '\0')) {
    return;
  }

  if (processLinkStatusLine(line)) {
    return;
  }

  if (sscanf(line, "PKT BEGIN SEQ=%lu", &seq) == 1) {
    monitorClearPacket(&g_workPacket);
    g_workPacket.seq = seq;
    g_parser.inPacket = true;
    g_parser.curveOpen = false;
    g_parser.expectedCurveCount = 0U;
    g_parser.receivedCurveCount = 0U;
    g_parser.lastError[0] = '\0';
    return;
  }

  if (strcmp(line, "PKT END") == 0) {
    monitorCommitPacket();
    g_parser.inPacket = false;
    g_parser.curveOpen = false;
    g_parser.expectedCurveCount = 0U;
    g_parser.receivedCurveCount = 0U;
    return;
  }

  if (strncmp(line, "INFO ", 5) == 0) {
    parserSetText(g_parser.lastInfo, sizeof(g_parser.lastInfo), line + 5);
    return;
  }

  if (strncmp(line, "ERR ", 4) == 0) {
    parserSetText(g_parser.lastError, sizeof(g_parser.lastError), line + 4);
    return;
  }

  if (!g_parser.inPacket) {
    return;
  }

  if (sscanf(line,
             "FRAME G=%u R=%u POINTS=%u VALID=%u FIRST=%u CURVE_FIRST=%u STEP=%u CURVE_COUNT=%u DARK=%f PEAK_RAW=%u PEAK_SMOOTH=%u IMEAN=%f IMAX=%f",
             &tmpGroup,
             &tmpReplicate,
             &tmpCount,
             &tmpValid,
             &tmpFirst,
             &tmpCurveFirst,
             &tmpStep,
             &tmpCurveCount,
             &tmpDark,
             &tmpPeakRaw,
             &tmpPeakSmooth,
             &tmpMean,
             &tmpMax)
      == 13) {
    g_workPacket.valid = true;
    g_workPacket.groupId = (uint8_t)tmpGroup;
    g_workPacket.replicateId = (uint8_t)tmpReplicate;
    g_workPacket.pointCount = (uint16_t)tmpCount;
    g_workPacket.validCount = (uint16_t)tmpValid;
    g_workPacket.firstPixel = (uint16_t)tmpFirst;
    g_workPacket.curveFirstPixel = (uint16_t)tmpCurveFirst;
    g_workPacket.curveStep = (uint16_t)tmpStep;
    g_workPacket.curveCount = (uint16_t)tmpCurveCount;
    g_workPacket.darkAdc = tmpDark;
    g_workPacket.peakRawPixel = (uint16_t)tmpPeakRaw;
    g_workPacket.peakSmoothPixel = (uint16_t)tmpPeakSmooth;
    g_workPacket.intensityMean = tmpMean;
    g_workPacket.intensityMax = tmpMax;
    return;
  }

  if (sscanf(line,
             "GROUP G=%u N=%u VALID=%u DCOUNT=%u CV=%f CORR_MEAN=%f CORR_MIN=%f PEAK_MEAN=%f PEAK_STD=%f",
             &tmpGroup,
             &tmpReplicate,
             &tmpValid,
             &tmpCurveCount,
             &tmpCv,
             &tmpCorrMean,
             &tmpCorrMin,
             &tmpPeakMean,
             &tmpPeakStd)
      == 9) {
    g_workPacket.groupValid = true;
    g_workPacket.groupStatsId = (uint8_t)tmpGroup;
    g_workPacket.groupReplicateCount = (uint8_t)tmpReplicate;
    g_workPacket.groupValidCount = (uint16_t)tmpValid;
    g_workPacket.groupDownsampledCount = (uint16_t)tmpCurveCount;
    g_workPacket.meanPointCv = tmpCv;
    g_workPacket.corrMean = tmpCorrMean;
    g_workPacket.corrMin = tmpCorrMin;
    g_workPacket.peakMean = tmpPeakMean;
    g_workPacket.peakStd = tmpPeakStd;
    return;
  }

  if (sscanf(line, "CURVE_BEGIN FIRST=%u STEP=%u COUNT=%u", &tmpFirst, &tmpStep, &tmpCurveCount) == 3) {
    g_parser.curveOpen = true;
    g_parser.expectedCurveCount = (uint16_t)tmpCurveCount;
    g_parser.receivedCurveCount = 0U;
    g_workPacket.curveFirstPixel = (uint16_t)tmpFirst;
    g_workPacket.curveStep = (uint16_t)tmpStep;
    return;
  }

  if (strcmp(line, "CURVE_END") == 0) {
    g_parser.curveOpen = false;
    return;
  }

  if (g_parser.curveOpen && (sscanf(line, "CURVE %u %u", &tmpPixel, &tmpQ15) == 2)) {
    if (g_parser.receivedCurveCount < MONITOR_CURVE_MAX) {
      g_workPacket.curvePixels[g_parser.receivedCurveCount] = (uint16_t)tmpPixel;
      g_workPacket.curveQ15[g_parser.receivedCurveCount] = (uint16_t)tmpQ15;
    }
    g_parser.receivedCurveCount++;
    return;
  }
}

static void appendSerialLog(const char *line) {
  if (line == nullptr) {
    return;
  }

  size_t len = strlen(line);
  if (len >= MONITOR_SERIAL_LINE_MAX) {
    len = MONITOR_SERIAL_LINE_MAX - 1U;
  }

  memcpy(g_serialLog[g_serialLogHead], line, len);
  g_serialLog[g_serialLogHead][len] = '\0';

  g_serialLogHead++;
  if (g_serialLogHead >= MONITOR_SERIAL_LOG_LINES) {
    g_serialLogHead = 0;
  }
  if (g_serialLogCount < MONITOR_SERIAL_LOG_LINES) {
    g_serialLogCount++;
  }
  g_serialLogDirty = true;
}

static void pollStm32Link() {
  while (Stm32Link.available() > 0) {
    const int byteValue = Stm32Link.read();
    if (byteValue < 0) {
      break;
    }

    if ((byteValue == '\r') || (byteValue == '\n')) {
      if (g_rxLineLen > 0U) {
        g_rxLine[g_rxLineLen] = '\0';
        processSerialLine(g_rxLine);
        appendSerialLog(g_rxLine);
        g_rxLineLen = 0U;
      }
      continue;
    }

    if (g_rxLineLen < (MONITOR_RX_LINE_MAX - 1U)) {
      g_rxLine[g_rxLineLen++] = (char)byteValue;
    } else {
      g_rxLineLen = 0U;
      parserSetText(g_parser.lastError, sizeof(g_parser.lastError), "串口数据行过长");
    }
  }
}

static void myDispFlush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *colorP) {
  (void)area;
  (void)colorP;
  lv_disp_flush_ready(disp);
}

static bool initTouchController(bool verbose) {
  for (uint8_t attempt = 0; attempt < 8U; ++attempt) {
    if (bsp_touch_init(&Wire, gfx->getRotation(), (uint16_t)screenWidth, (uint16_t)screenHeight)) {
      if (verbose) {
        Serial.printf("touch init ok on attempt %u\n", (unsigned int)(attempt + 1U));
      }
      g_touchReady = true;
      return true;
    }
    delay(60);
  }

  if (verbose) {
    Serial.println("touch init failed after retries");
  }
  g_touchReady = false;
  return false;
}

static void myTouchpadRead(lv_indev_drv_t *indev, lv_indev_data_t *data) {
  uint16_t touchX = 0U;
  uint16_t touchY = 0U;
  (void)indev;

  if (!g_touchReady) {
    data->state = LV_INDEV_STATE_RELEASED;
    return;
  }

  bsp_touch_read();
  if (bsp_touch_get_coordinates(&touchX, &touchY)) {
    if (touchX >= screenWidth) {
      touchX = (uint16_t)(screenWidth - 1U);
    }
    if (touchY >= screenHeight) {
      touchY = (uint16_t)(screenHeight - 1U);
    }
    data->point.x = (lv_coord_t)touchX;
    data->point.y = (lv_coord_t)touchY;
    data->state = LV_INDEV_STATE_PRESSED;
  } else {
    data->state = LV_INDEV_STATE_RELEASED;
  }
}

static lv_obj_t *createCard(lv_obj_t *parent, const lv_color_t color, int32_t height) {
  lv_obj_t *card = lv_obj_create(parent);
  lv_obj_set_width(card, lv_pct(100));
  if (height == LV_SIZE_CONTENT) {
    lv_obj_set_height(card, LV_SIZE_CONTENT);
  } else {
    lv_obj_set_height(card, height);
  }
  lv_obj_set_style_radius(card, 12, 0);
  lv_obj_set_style_border_width(card, 0, 0);
  lv_obj_set_style_bg_color(card, color, 0);
  lv_obj_set_style_shadow_width(card, 12, 0);
  lv_obj_set_style_shadow_opa(card, LV_OPA_20, 0);
  lv_obj_set_style_shadow_ofs_y(card, 4, 0);
  lv_obj_set_style_pad_all(card, 8, 0);
  lv_obj_set_style_pad_row(card, 4, 0);
  lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  return card;
}

static lv_obj_t *createCardTitle(lv_obj_t *parent, const char *text) {
  lv_obj_t *label = lv_label_create(parent);
  lv_obj_set_style_text_font(label, UI_FONT_CN, 0);
  lv_obj_set_style_text_color(label, lv_color_hex(0x1f2933), 0);
  lv_label_set_text(label, text);
  return label;
}

static lv_obj_t *createBodyLabel(lv_obj_t *parent, const char *text, const lv_color_t color) {
  lv_obj_t *label = lv_label_create(parent);
  lv_obj_set_style_text_font(label, UI_FONT_CN, 0);
  lv_obj_set_style_text_color(label, color, 0);
  lv_label_set_text(label, text);
  lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(label, lv_pct(100));
  return label;
}

static void copyUiText(char *dst, size_t dstSize, lv_obj_t *textArea) {
  if ((dst == nullptr) || (dstSize == 0U)) {
    return;
  }
  if (textArea == nullptr) {
    dst[0] = '\0';
    return;
  }
  parserSetText(dst, dstSize, lv_textarea_get_text(textArea));
}

static void hideKeyboard() {
  if (kbInput == nullptr) {
    return;
  }
  lv_keyboard_set_textarea(kbInput, nullptr);
  lv_obj_add_flag(kbInput, LV_OBJ_FLAG_HIDDEN);
}

static void showKeyboard(lv_obj_t *textArea) {
  if ((kbInput == nullptr) || (textArea == nullptr)) {
    return;
  }
  lv_keyboard_set_textarea(kbInput, textArea);
  lv_keyboard_set_mode(kbInput, LV_KEYBOARD_MODE_TEXT_LOWER);
  lv_obj_clear_flag(kbInput, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(kbInput);
}

static void setLinkStreaming(bool enabled) {
  g_linkStreaming = enabled;
  digitalWrite(STM32_WAKE_PIN, enabled ? HIGH : LOW);
  parserSetText(g_parser.lastInfo,
                sizeof(g_parser.lastInfo),
                enabled ? "已允许 STM32 输出数据" : "已暂停 STM32 输出数据");
  sendLinkStatusQuery(false);
}

static void updateWifiStatusLabel() {
  char text[192];

  if (labelWifiStatus == nullptr) {
    return;
  }

  if (WiFi.status() == WL_CONNECTED) {
    const IPAddress ip = WiFi.localIP();
    const String ssid = WiFi.SSID();
    snprintf(text,
             sizeof(text),
             "WiFi 已连接\nSSID %s\nIP %u.%u.%u.%u  RSSI %d dBm",
             ssid.c_str(),
             ip[0],
             ip[1],
             ip[2],
             ip[3],
             WiFi.RSSI());
  } else if (g_wifiSsid[0] != '\0') {
    snprintf(text,
             sizeof(text),
             "WiFi 未连接\n目标 %s\n点按 连接 开始连接",
             g_wifiSsid);
  } else {
    snprintf(text, sizeof(text), "WiFi 未连接\n请输入 SSID 和密码");
  }

  lv_label_set_text(labelWifiStatus, text);
}

static void stopBleBroadcast() {
#if defined(CONFIG_BLUEDROID_ENABLED)
  if (BLEDevice::getInitialized()) {
    BLEDevice::stopAdvertising();
    BLEDevice::deinit(true);
  }
  g_bleAdvertisingActive = false;
#endif
}

static void startBleBroadcast(const char *deviceName) {
#if defined(CONFIG_BLUEDROID_ENABLED)
  stopBleBroadcast();
  BLEDevice::init(std::string((deviceName != nullptr) ? deviceName : "G431-Monitor"));
  (void)BLEDevice::createServer();
  BLEDevice::startAdvertising();
  g_bleAdvertisingActive = true;
#else
  (void)deviceName;
#endif
}

static void updateBleStatusLabel() {
  char text[160];

  if (labelBleStatus == nullptr) {
    return;
  }

#if defined(CONFIG_BLUEDROID_ENABLED)
  snprintf(text,
           sizeof(text),
           "BLE %s\n名称 %s",
           g_bleAdvertisingActive ? "广播中" : "空闲",
           g_bleName);
#else
  snprintf(text,
           sizeof(text),
           "BLE 不可用\n当前内核未打开 Bluedroid");
#endif
  lv_label_set_text(labelBleStatus, text);
}

static void handleKeyboardEvent(lv_event_t *event) {
  const lv_event_code_t code = lv_event_get_code(event);
  if ((code == LV_EVENT_READY) || (code == LV_EVENT_CANCEL)) {
    hideKeyboard();
  }
}

static void handleTextAreaEvent(lv_event_t *event) {
  const lv_event_code_t code = lv_event_get_code(event);
  if ((code == LV_EVENT_FOCUSED) || (code == LV_EVENT_CLICKED)) {
    showKeyboard(lv_event_get_target(event));
  }
}

static void handleAcquireEnable(lv_event_t *event) {
  (void)event;
  setLinkStreaming(true);
}

static void handleAcquireHold(lv_event_t *event) {
  (void)event;
  setLinkStreaming(false);
}

static void handleLinkCheck(lv_event_t *event) {
  (void)event;
  sendLinkStatusQuery(true);
}

static void handleWifiJoin(lv_event_t *event) {
  (void)event;
  copyUiText(g_wifiSsid, sizeof(g_wifiSsid), taWifiSsid);
  copyUiText(g_wifiPassword, sizeof(g_wifiPassword), taWifiPassword);

  if (g_wifiSsid[0] == '\0') {
    parserSetText(g_parser.lastError, sizeof(g_parser.lastError), "WiFi 名称为空");
    updateWifiStatusLabel();
    return;
  }

  g_wifiAutoConnect = true;
  saveWifiSettings();
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(g_wifiSsid, g_wifiPassword);
  parserSetText(g_parser.lastInfo, sizeof(g_parser.lastInfo), "已请求连接 WiFi");
  updateWifiStatusLabel();
}

static void handleWifiDrop(lv_event_t *event) {
  (void)event;
  g_wifiAutoConnect = false;
  saveWifiSettings();
  WiFi.setAutoReconnect(false);
  WiFi.disconnect(true);
  parserSetText(g_parser.lastInfo, sizeof(g_parser.lastInfo), "WiFi 已断开");
  updateWifiStatusLabel();
}

static void handleBleStart(lv_event_t *event) {
  (void)event;
  copyUiText(g_bleName, sizeof(g_bleName), taBleName);
  if (g_bleName[0] == '\0') {
    parserSetText(g_parser.lastError, sizeof(g_parser.lastError), "BLE 名称为空");
    updateBleStatusLabel();
    return;
  }

  g_bleAutoStart = true;
  saveBleSettings();
  startBleBroadcast(g_bleName);
  parserSetText(g_parser.lastInfo, sizeof(g_parser.lastInfo), "BLE 已开始广播");
  updateBleStatusLabel();
}

static void handleBleStop(lv_event_t *event) {
  (void)event;
  g_bleAutoStart = false;
  saveBleSettings();
  stopBleBroadcast();
  parserSetText(g_parser.lastInfo, sizeof(g_parser.lastInfo), "BLE 已停止广播");
  updateBleStatusLabel();
}

static lv_obj_t *createActionButton(lv_obj_t *parent,
                                    const char *text,
                                    const lv_color_t bgColor,
                                    lv_event_cb_t callback) {
  lv_obj_t *button = lv_btn_create(parent);
  lv_obj_set_height(button, 42);
  lv_obj_set_flex_grow(button, 1);
  lv_obj_set_style_radius(button, 12, 0);
  lv_obj_set_style_border_width(button, 0, 0);
  lv_obj_set_style_bg_color(button, bgColor, 0);
  lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, nullptr);

  lv_obj_t *label = lv_label_create(button);
  lv_label_set_text(label, text);
  lv_obj_set_style_text_font(label, UI_FONT_CN, 0);
  lv_obj_set_style_text_color(label, lv_color_hex(0xf8fafc), 0);
  lv_obj_center(label);
  return button;
}

static lv_obj_t *createInputField(lv_obj_t *parent,
                                  const char *placeholder,
                                  const char *value,
                                  bool passwordMode) {
  lv_obj_t *textArea = lv_textarea_create(parent);
  lv_obj_set_width(textArea, lv_pct(100));
  lv_textarea_set_one_line(textArea, true);
  lv_textarea_set_placeholder_text(textArea, placeholder);
  lv_textarea_set_text(textArea, (value != nullptr) ? value : "");
  lv_textarea_set_password_mode(textArea, passwordMode);
  lv_obj_set_style_text_font(textArea, UI_FONT_CN, 0);
  lv_obj_set_style_radius(textArea, 10, 0);
  lv_obj_set_style_border_width(textArea, 1, 0);
  lv_obj_set_style_border_color(textArea, lv_color_hex(0xcbd2d9), 0);
  lv_obj_set_style_bg_color(textArea, lv_color_hex(0xffffff), 0);
  lv_obj_set_style_pad_left(textArea, 8, 0);
  lv_obj_set_style_pad_right(textArea, 8, 0);
  lv_obj_add_event_cb(textArea, handleTextAreaEvent, LV_EVENT_ALL, nullptr);
  return textArea;
}

static void updateNavButtons() {
  for (uint8_t i = 0; i < UI_PAGE_COUNT; ++i) {
    if (navButtons[i] == nullptr) {
      continue;
    }

    const bool active = ((uint8_t)g_currentPage == i);
    lv_obj_set_style_bg_color(navButtons[i],
                              active ? lv_color_hex(0xf0b429) : lv_color_hex(0x243b53),
                              0);
    lv_obj_set_style_text_color(navButtons[i],
                                active ? lv_color_hex(0x102a43) : lv_color_hex(0xf0f4f8),
                                0);
  }
}

static void showPage(UiPage page) {
  g_currentPage = page;
  for (uint8_t i = 0; i < UI_PAGE_COUNT; ++i) {
    if (pageContainers[i] == nullptr) {
      continue;
    }

    if ((uint8_t)page == i) {
      lv_obj_clear_flag(pageContainers[i], LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(pageContainers[i], LV_OBJ_FLAG_HIDDEN);
    }
  }
  updatePageTitle();
  updateNavButtons();
}

static void handleNavButton(lv_event_t *event) {
  const UiPage page = (UiPage)(uintptr_t)lv_event_get_user_data(event);
  showPage(page);
}

static lv_obj_t *createNavButton(lv_obj_t *parent, const char *text, UiPage page) {
  lv_obj_t *button = lv_btn_create(parent);
  lv_obj_set_width(button, lv_pct(100));
  lv_obj_set_height(button, 56);
  lv_obj_set_style_radius(button, 9, 0);
  lv_obj_set_style_border_width(button, 0, 0);
  lv_obj_add_event_cb(button, handleNavButton, LV_EVENT_CLICKED, (void *)(uintptr_t)page);

  lv_obj_t *label = lv_label_create(button);
  lv_label_set_text(label, text);
  lv_obj_set_style_text_font(label, UI_FONT_CN, 0);
  lv_obj_center(label);
  return button;
}

static void refreshAcquirePage() {
  char text[192];
  const bool wakeHigh = digitalRead(STM32_WAKE_PIN) == HIGH;
  const bool dataReadyHigh = digitalRead(STM32_DATA_RDY_PIN) == HIGH;
  const uint32_t now = millis();
  const uint32_t ageMs = (g_livePacket.valid != 0U) ? (now - g_livePacket.receivedAtMs) : 0U;
  const uint32_t linkAgeMs = (g_linkStatus.lastOkAtMs != 0U) ? (now - g_linkStatus.lastOkAtMs) : 0U;
  const bool g431Online = linkStatusIsOnline(now);
  const char *g431Summary = g_linkStatus.requestPending ? "CHK" : (g431Online ? "ON" : ((g_linkStatus.state == LINK_QUERY_TIMEOUT) ? "TO" : "WAIT"));

  if (labelAcquireState != nullptr) {
    snprintf(text,
             sizeof(text),
             "模式 %s  WAKE %s  DATA %s  TP %s",
             g_linkStreaming ? "运行" : "暂停",
             wakeHigh ? "H" : "L",
             dataReadyHigh ? "H" : "L",
             g_touchReady ? "正常" : "异常");
    lv_label_set_text(labelAcquireState, text);
    snprintf(text,
             sizeof(text),
             "G431:%s RUN:%s W:%s D:%s TP:%s",
             g431Summary,
             g_linkStreaming ? "ON" : "HOLD",
             wakeHigh ? "H" : "L",
             dataReadyHigh ? "H" : "L",
             g_touchReady ? "OK" : "ER");
    lv_label_set_text(labelAcquireState, text);
  }

  if (labelAcquireG431 != nullptr) {
    if (g_linkStatus.requestPending) {
      snprintf(text,
               sizeof(text),
               "G431: CHECKING  T=%s  %lums",
               g_linkStatus.pendingToken,
               (unsigned long)(now - g_linkStatus.sentAtMs));
    } else if (g431Online) {
      snprintf(text,
               sizeof(text),
               "G431: ONLINE  RTT %lums  SEQ %lu  REP %lu  VALID %lu",
               (unsigned long)g_linkStatus.roundTripMs,
               (unsigned long)g_linkStatus.seq,
               (unsigned long)g_linkStatus.replicate,
               (unsigned long)g_linkStatus.valid);
    } else if (g_linkStatus.state == LINK_QUERY_TIMEOUT) {
      snprintf(text,
               sizeof(text),
               "G431: TIMEOUT  last_ok %lums  miss %lu",
               (unsigned long)linkAgeMs,
               (unsigned long)g_linkStatus.timeoutCount);
    } else {
      snprintf(text, sizeof(text), "G431: UNKNOWN  press Check G431");
    }
    lv_label_set_text(labelAcquireG431, text);
  }

  if (labelAcquireFrame != nullptr) {
    if (g_livePacket.valid) {
      snprintf(text,
               sizeof(text),
               "组 %u  次 %u  Seq %lu  点数 %u  更新 %lums",
               g_livePacket.groupId,
               g_livePacket.replicateId,
               (unsigned long)g_livePacket.seq,
               g_livePacket.validCount,
               (unsigned long)ageMs);
    } else {
      snprintf(text, sizeof(text), "等待 STM32 数据");
    }
    lv_label_set_text(labelAcquireFrame, text);
  }

  if (labelAcquireInfo != nullptr) {
    if (g_parser.lastError[0] != '\0') {
      snprintf(text, sizeof(text), "错误: %s", g_parser.lastError);
    } else if ((g_linkStatus.state == LINK_QUERY_TIMEOUT) && (g_linkStatus.timeoutCount > 0U)) {
      snprintf(text, sizeof(text), "Link: G431 status timeout");
    } else if (g_parser.lastInfo[0] != '\0') {
      snprintf(text, sizeof(text), "提示: %s", g_parser.lastInfo);
    } else {
      snprintf(text, sizeof(text), "点按 运行/暂停 控制 STM32 输出");
    }
    lv_label_set_text(labelAcquireInfo, text);
  }
}

static void buildUi() {
  lv_obj_t *root = lv_scr_act();
  lv_obj_set_style_bg_color(root, lv_color_hex(0xe8edf3), 0);
  lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(root, 4, 0);
  lv_obj_set_style_pad_row(root, 4, 0);
  lv_obj_set_style_pad_column(root, 4, 0);
  lv_obj_set_flex_flow(root, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(root, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *navPanel = lv_obj_create(root);
  lv_obj_set_width(navPanel, 38);
  lv_obj_set_height(navPanel, lv_pct(100));
  lv_obj_set_style_radius(navPanel, 14, 0);
  lv_obj_set_style_bg_color(navPanel, lv_color_hex(0x102a43), 0);
  lv_obj_set_style_border_width(navPanel, 0, 0);
  lv_obj_set_style_pad_all(navPanel, 4, 0);
  lv_obj_set_style_pad_row(navPanel, 6, 0);
  lv_obj_set_flex_flow(navPanel, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(navPanel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_clear_flag(navPanel, LV_OBJ_FLAG_SCROLLABLE);

  navButtons[UI_PAGE_ACQUIRE] = createNavButton(navPanel, "采\n集", UI_PAGE_ACQUIRE);
  navButtons[UI_PAGE_CURVE] = createNavButton(navPanel, "曲\n线", UI_PAGE_CURVE);
  navButtons[UI_PAGE_LINK] = createNavButton(navPanel, "设\n置", UI_PAGE_LINK);
  navButtons[UI_PAGE_SERIAL] = createNavButton(navPanel, "串\n口", UI_PAGE_SERIAL);

  lv_obj_t *mainPanel = lv_obj_create(root);
  lv_obj_set_height(mainPanel, lv_pct(100));
  lv_obj_set_flex_grow(mainPanel, 1);
  lv_obj_set_style_radius(mainPanel, 14, 0);
  lv_obj_set_style_bg_color(mainPanel, lv_color_hex(0xf8fafc), 0);
  lv_obj_set_style_border_width(mainPanel, 0, 0);
  lv_obj_set_style_pad_all(mainPanel, 4, 0);
  lv_obj_set_style_pad_row(mainPanel, 4, 0);
  lv_obj_set_flex_flow(mainPanel, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(mainPanel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_clear_flag(mainPanel, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *headerCard = createCard(mainPanel, lv_color_hex(0xffffff), LV_SIZE_CONTENT);
  lv_obj_set_style_pad_all(headerCard, 4, 0);
  lv_obj_set_style_shadow_width(headerCard, 6, 0);

  labelTitle = lv_label_create(headerCard);
  lv_obj_set_style_text_font(labelTitle, UI_FONT_CN, 0);
  lv_obj_set_style_text_color(labelTitle, lv_color_hex(0x102a43), 0);
  lv_label_set_text(labelTitle, "采集控制");

  labelLink = lv_label_create(headerCard);
  lv_obj_set_width(labelLink, lv_pct(100));
  lv_obj_set_style_text_font(labelLink, UI_FONT_CN, 0);
  lv_obj_set_style_text_color(labelLink, lv_color_hex(0x486581), 0);
  lv_label_set_long_mode(labelLink, LV_LABEL_LONG_WRAP);
  lv_label_set_text(labelLink, "连接启动中...");

  lv_obj_t *pageHost = lv_obj_create(mainPanel);
  lv_obj_set_width(pageHost, lv_pct(100));
  lv_obj_set_flex_grow(pageHost, 1);
  lv_obj_set_style_bg_opa(pageHost, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(pageHost, 0, 0);
  lv_obj_set_style_pad_all(pageHost, 0, 0);
  lv_obj_clear_flag(pageHost, LV_OBJ_FLAG_SCROLLABLE);

  for (uint8_t i = 0; i < UI_PAGE_COUNT; ++i) {
    pageContainers[i] = lv_obj_create(pageHost);
    lv_obj_set_size(pageContainers[i], lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_opa(pageContainers[i], LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(pageContainers[i], 0, 0);
    lv_obj_set_style_pad_all(pageContainers[i], 0, 0);
    lv_obj_set_style_pad_right(pageContainers[i], 4, 0);
    lv_obj_set_style_pad_bottom(pageContainers[i], 16, 0);
    lv_obj_set_style_pad_row(pageContainers[i], 6, 0);
    lv_obj_set_flex_flow(pageContainers[i], LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(pageContainers[i], LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_add_flag(pageContainers[i], LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(pageContainers[i], LV_DIR_VER);
    lv_obj_set_scrollbar_mode(pageContainers[i], LV_SCROLLBAR_MODE_ACTIVE);
    lv_obj_add_flag(pageContainers[i], LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_add_flag(pageContainers[i], LV_OBJ_FLAG_SCROLL_ELASTIC);
  }

  lv_obj_t *acquireControlCard = createCard(pageContainers[UI_PAGE_ACQUIRE], lv_color_hex(0xffffff), LV_SIZE_CONTENT);
  createCardTitle(acquireControlCard, "采集控制");
  lv_obj_t *acquireButtonRow = lv_obj_create(acquireControlCard);
  lv_obj_set_width(acquireButtonRow, lv_pct(100));
  lv_obj_set_height(acquireButtonRow, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(acquireButtonRow, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(acquireButtonRow, 0, 0);
  lv_obj_set_style_pad_all(acquireButtonRow, 0, 0);
  lv_obj_set_style_pad_column(acquireButtonRow, 8, 0);
  lv_obj_set_flex_flow(acquireButtonRow, LV_FLEX_FLOW_ROW);
  lv_obj_clear_flag(acquireButtonRow, LV_OBJ_FLAG_SCROLLABLE);
  (void)createActionButton(acquireButtonRow, "运行", lv_color_hex(0x0f766e), handleAcquireEnable);
  (void)createActionButton(acquireButtonRow, "暂停", lv_color_hex(0xb45309), handleAcquireHold);

  lv_obj_t *acquireStatusCard = createCard(pageContainers[UI_PAGE_ACQUIRE], lv_color_hex(0xfef6e4), LV_SIZE_CONTENT);
  lv_obj_set_style_min_height(acquireStatusCard, 182, 0);
  createCardTitle(acquireStatusCard, "数据状态");
  labelAcquireState = createBodyLabel(acquireStatusCard, "模式 --", lv_color_hex(0x7c2d12));
  labelAcquireFrame = createBodyLabel(acquireStatusCard, "等待 STM32 数据", lv_color_hex(0x7c2d12));
  labelAcquireStats = createBodyLabel(acquireStatusCard, "底值 --  均值 --  最大 --", lv_color_hex(0x9a3412));
  labelAcquireGroup = createBodyLabel(acquireStatusCard, "分组结果等待中", lv_color_hex(0x9a3412));
  labelAcquireInfo = createBodyLabel(acquireStatusCard, "点按 运行/暂停 控制 STM32 输出", lv_color_hex(0x9a3412));

  labelAcquireG431 = createBodyLabel(acquireStatusCard, "G431: UNKNOWN", lv_color_hex(0x7c2d12));
  lv_obj_t *acquireStatusButtonRow = lv_obj_create(acquireStatusCard);
  lv_obj_set_width(acquireStatusButtonRow, lv_pct(100));
  lv_obj_set_height(acquireStatusButtonRow, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(acquireStatusButtonRow, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(acquireStatusButtonRow, 0, 0);
  lv_obj_set_style_pad_all(acquireStatusButtonRow, 0, 0);
  lv_obj_set_style_pad_column(acquireStatusButtonRow, 8, 0);
  lv_obj_set_flex_flow(acquireStatusButtonRow, LV_FLEX_FLOW_ROW);
  lv_obj_clear_flag(acquireStatusButtonRow, LV_OBJ_FLAG_SCROLLABLE);
  (void)createActionButton(acquireStatusButtonRow, "Check G431", lv_color_hex(0x1d4ed8), handleLinkCheck);

  lv_obj_t *curveCard = createCard(pageContainers[UI_PAGE_CURVE], lv_color_hex(0xeaf5f0), lv_pct(100));
  lv_obj_set_flex_grow(curveCard, 1);
  lv_obj_set_style_pad_all(curveCard, 6, 0);
  lv_obj_set_style_pad_row(curveCard, 3, 0);
  createCardTitle(curveCard, "数据曲线");
  chartCurve = lv_chart_create(curveCard);
  lv_obj_set_width(chartCurve, lv_pct(100));
  lv_obj_set_flex_grow(chartCurve, 1);
  lv_obj_set_style_bg_color(chartCurve, lv_color_hex(0xf8fffb), 0);
  lv_obj_set_style_border_width(chartCurve, 0, 0);
  lv_obj_set_style_line_width(chartCurve, 2, LV_PART_ITEMS);
  lv_chart_set_type(chartCurve, LV_CHART_TYPE_LINE);
  lv_chart_set_div_line_count(chartCurve, 5, 8);
  lv_chart_set_point_count(chartCurve, MONITOR_CURVE_MAX);
  lv_chart_set_range(chartCurve, LV_CHART_AXIS_PRIMARY_Y, 0, MONITOR_CHART_Y_MAX);
  chartSeries = lv_chart_add_series(chartCurve, lv_palette_main(LV_PALETTE_TEAL), LV_CHART_AXIS_PRIMARY_Y);
  lv_chart_set_ext_y_array(chartCurve, chartSeries, chartPoints);
  labelCurve = createBodyLabel(curveCard, "等待曲线数据", lv_color_hex(0x285943));
  lv_obj_set_style_text_font(labelCurve, UI_FONT_CN, 0);

  lv_obj_t *wifiCard = createCard(pageContainers[UI_PAGE_LINK], lv_color_hex(0xffffff), LV_SIZE_CONTENT);
  createCardTitle(wifiCard, "WiFi");
  taWifiSsid = createInputField(wifiCard, "WiFi 名称", g_wifiSsid, false);
  taWifiPassword = createInputField(wifiCard, "密码", g_wifiPassword, true);
  lv_obj_t *wifiButtonRow = lv_obj_create(wifiCard);
  lv_obj_set_width(wifiButtonRow, lv_pct(100));
  lv_obj_set_height(wifiButtonRow, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(wifiButtonRow, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(wifiButtonRow, 0, 0);
  lv_obj_set_style_pad_all(wifiButtonRow, 0, 0);
  lv_obj_set_style_pad_column(wifiButtonRow, 8, 0);
  lv_obj_set_flex_flow(wifiButtonRow, LV_FLEX_FLOW_ROW);
  lv_obj_clear_flag(wifiButtonRow, LV_OBJ_FLAG_SCROLLABLE);
  (void)createActionButton(wifiButtonRow, "连接", lv_color_hex(0x0f766e), handleWifiJoin);
  (void)createActionButton(wifiButtonRow, "断开", lv_color_hex(0xb91c1c), handleWifiDrop);
  labelWifiStatus = createBodyLabel(wifiCard, "WiFi 未连接", lv_color_hex(0x334e68));

  lv_obj_t *bleCard = createCard(pageContainers[UI_PAGE_LINK], lv_color_hex(0xeaf2ff), LV_SIZE_CONTENT);
  lv_obj_set_style_min_height(bleCard, 132, 0);
  createCardTitle(bleCard, "BLE");
  taBleName = createInputField(bleCard, "BLE 名称", g_bleName, false);
  lv_obj_t *bleButtonRow = lv_obj_create(bleCard);
  lv_obj_set_width(bleButtonRow, lv_pct(100));
  lv_obj_set_height(bleButtonRow, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(bleButtonRow, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(bleButtonRow, 0, 0);
  lv_obj_set_style_pad_all(bleButtonRow, 0, 0);
  lv_obj_set_style_pad_column(bleButtonRow, 8, 0);
  lv_obj_set_flex_flow(bleButtonRow, LV_FLEX_FLOW_ROW);
  lv_obj_clear_flag(bleButtonRow, LV_OBJ_FLAG_SCROLLABLE);
  (void)createActionButton(bleButtonRow, "开始", lv_color_hex(0x1d4ed8), handleBleStart);
  (void)createActionButton(bleButtonRow, "停止", lv_color_hex(0x6b7280), handleBleStop);
  labelBleStatus = createBodyLabel(bleCard, "BLE 空闲", lv_color_hex(0x1e3a8a));

  lv_obj_t *serialCard = createCard(pageContainers[UI_PAGE_SERIAL], lv_color_hex(0x1a1a2e), lv_pct(100));
  lv_obj_set_flex_grow(serialCard, 1);
  lv_obj_set_style_pad_all(serialCard, 6, 0);
  lv_obj_set_style_pad_row(serialCard, 2, 0);

  lv_obj_t *serialHeader = lv_label_create(serialCard);
  lv_obj_set_style_text_font(serialHeader, UI_FONT_CN, 0);
  lv_obj_set_style_text_color(serialHeader, lv_color_hex(0xcccccc), 0);
  lv_label_set_text(serialHeader, "原始串口数据 (最新 64 条)");

  labelSerialLog = lv_label_create(serialCard);
  lv_obj_set_width(labelSerialLog, lv_pct(100));
  lv_obj_set_flex_grow(labelSerialLog, 1);
  lv_obj_set_style_text_font(labelSerialLog, LV_FONT_DEFAULT, 0);
  lv_obj_set_style_text_color(labelSerialLog, lv_color_hex(0x00ff41), 0);
  lv_obj_set_style_bg_color(labelSerialLog, lv_color_hex(0x0d0d0d), 0);
  lv_obj_set_style_bg_opa(labelSerialLog, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(labelSerialLog, 4, 0);
  lv_obj_set_style_radius(labelSerialLog, 4, 0);
  lv_label_set_long_mode(labelSerialLog, LV_LABEL_LONG_WRAP);
  lv_label_set_text(labelSerialLog, "等待串口数据...");

  kbInput = lv_keyboard_create(root);
  lv_obj_set_size(kbInput, screenWidth - 48U, 104);
  lv_obj_set_style_text_font(kbInput, UI_FONT_CN, 0);
  lv_obj_set_style_text_font(kbInput, UI_FONT_CN, LV_PART_ITEMS);
  lv_obj_add_flag(kbInput, LV_OBJ_FLAG_HIDDEN | LV_OBJ_FLAG_FLOATING);
  lv_obj_align(kbInput, LV_ALIGN_BOTTOM_RIGHT, -4, -4);
  lv_obj_add_event_cb(kbInput, handleKeyboardEvent, LV_EVENT_ALL, nullptr);

  showPage(UI_PAGE_ACQUIRE);
}

static void refreshLinkStatus() {
  char text[112];
  const uint32_t now = millis();
  const bool wakeHigh = digitalRead(STM32_WAKE_PIN) == HIGH;
  const bool dataReadyHigh = digitalRead(STM32_DATA_RDY_PIN) == HIGH;
  const bool freshPacket = g_livePacket.valid && ((now - g_livePacket.receivedAtMs) < 5000U);
  const char *g431State = g_linkStatus.requestPending ? "CHECK" : (linkStatusIsOnline(now) ? "ONLINE" : ((g_linkStatus.state == LINK_QUERY_TIMEOUT) ? "TIMEOUT" : "WAIT"));
  (void)g431State;

  snprintf(text,
           sizeof(text),
           "TP:%s  WAKE:%s  DATA:%s  UART:%s  SEQ:%lu",
           g_touchReady ? "正常" : "异常",
           wakeHigh ? "H" : "L",
           dataReadyHigh ? "H" : "L",
           freshPacket ? "在线" : "等待",
           (unsigned long)g_livePacket.seq);
  lv_label_set_text(labelLink, text);
  snprintf(text,
           sizeof(text),
           "G431:%s TP:%s W:%s D:%s U:%s",
           g431State,
           g_touchReady ? "OK" : "ER",
           wakeHigh ? "H" : "L",
           dataReadyHigh ? "H" : "L",
           freshPacket ? "ON" : "WAIT");
  lv_label_set_text(labelLink, text);
}

static void applyLivePacketToUi() {
  char text[160];
  uint16_t count = 0U;
  uint16_t i = 0U;

  if (!g_livePacket.valid) {
    return;
  }

  snprintf(text,
           sizeof(text),
           "底值 %.2f   均值 %.2f   最大 %.2f   曲线 %u",
           g_livePacket.darkAdc,
           g_livePacket.intensityMean,
           g_livePacket.intensityMax,
           g_livePacket.curveCount);
  if (labelAcquireStats != nullptr) {
    lv_label_set_text(labelAcquireStats, text);
  }

  if (g_livePacket.groupValid) {
    snprintf(text,
             sizeof(text),
             "峰值 %u/%u   组数 %u   CV %.4f   相关 %.4f",
             g_livePacket.peakRawPixel,
             g_livePacket.peakSmoothPixel,
             g_livePacket.groupReplicateCount,
             g_livePacket.meanPointCv,
             g_livePacket.corrMean);
  } else {
    snprintf(text,
             sizeof(text),
             "峰值 %u/%u   分组结果等待中",
             g_livePacket.peakRawPixel,
             g_livePacket.peakSmoothPixel);
  }
  if (labelAcquireGroup != nullptr) {
    lv_label_set_text(labelAcquireGroup, text);
  }

  memset(chartPoints, 0, sizeof(chartPoints));
  count = g_livePacket.curveCount;
  if (count > MONITOR_CURVE_MAX) {
    count = MONITOR_CURVE_MAX;
  }
  for (i = 0U; i < count; ++i) {
    chartPoints[i] = (lv_coord_t)(((uint32_t)g_livePacket.curveQ15[i] * MONITOR_CHART_Y_MAX) / 32767U);
  }
  if (count < 2U) {
    lv_chart_set_point_count(chartCurve, 2U);
    chartPoints[0] = 0;
    chartPoints[1] = 0;
  } else {
    lv_chart_set_point_count(chartCurve, count);
  }
  lv_chart_refresh(chartCurve);

  if (count > 0U) {
    const uint16_t curveLastPixel = g_livePacket.curvePixels[count - 1U];
    snprintf(text,
             sizeof(text),
             "像素 %u -> %u   比例 0..1   末值 %.3f",
             g_livePacket.curveFirstPixel,
             curveLastPixel,
             (float)g_livePacket.curveQ15[count - 1U] / 32767.0f);
    if (labelCurve != nullptr) {
      lv_label_set_text(labelCurve, text);
    }
  } else {
    if (labelCurve != nullptr) {
      lv_label_set_text(labelCurve, "曲线数据为空");
    }
  }

  refreshAcquirePage();
}

static void refreshSerialPage() {
  uint8_t idx;
  uint8_t count;

  if (!g_serialLogDirty || (labelSerialLog == nullptr)) {
    return;
  }
  g_serialLogDirty = false;

  count = g_serialLogCount;

  // Calculate total size: line lengths + newlines + null
  size_t total = 1;
  if (count >= MONITOR_SERIAL_LOG_LINES) {
    idx = g_serialLogHead;
  } else {
    idx = 0;
  }
  for (uint8_t i = 0; i < count; ++i) {
    total += strlen(g_serialLog[idx]) + 1U;
    idx++;
    if (idx >= MONITOR_SERIAL_LOG_LINES) {
      idx = 0;
    }
  }

  char *buffer = (char *)malloc(total);
  if (buffer == nullptr) {
    return;
  }
  buffer[0] = '\0';

  if (count >= MONITOR_SERIAL_LOG_LINES) {
    idx = g_serialLogHead;
  } else {
    idx = 0;
  }
  for (uint8_t i = 0; i < count; ++i) {
    strcat(buffer, g_serialLog[idx]);
    strcat(buffer, "\n");
    idx++;
    if (idx >= MONITOR_SERIAL_LOG_LINES) {
      idx = 0;
    }
  }

  lv_label_set_text(labelSerialLog, buffer);
  free(buffer);
}

static void initDisplayAndLvgl() {
  if (!gfx->begin()) {
    Serial.println("gfx->begin() failed");
  }
  gfx->fillScreen(BLACK);

  pinMode(EXAMPLE_PIN_NUM_LCD_BL, OUTPUT);
  digitalWrite(EXAMPLE_PIN_NUM_LCD_BL, HIGH);

  lv_init();

  screenWidth = gfx->width();
  screenHeight = gfx->height();
  bufSize = screenWidth * screenHeight;

  dispDrawBuf = (lv_color_t *)heap_caps_malloc(bufSize * sizeof(lv_color_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (dispDrawBuf == nullptr) {
    dispDrawBuf = (lv_color_t *)heap_caps_malloc(bufSize * sizeof(lv_color_t), MALLOC_CAP_8BIT);
  }

  if (dispDrawBuf == nullptr) {
    Serial.println("LVGL draw buffer alloc failed");
    while (true) {
      delay(1000);
    }
  }

  lv_disp_draw_buf_init(&drawBuf, dispDrawBuf, nullptr, bufSize);

  lv_disp_drv_init(&dispDrv);
  dispDrv.hor_res = screenWidth;
  dispDrv.ver_res = screenHeight;
  dispDrv.flush_cb = myDispFlush;
  dispDrv.draw_buf = &drawBuf;
  dispDrv.direct_mode = true;
  lv_disp_t *display = lv_disp_drv_register(&dispDrv);

  Wire.begin(EXAMPLE_PIN_NUM_TP_SDA, EXAMPLE_PIN_NUM_TP_SCL);
  Wire.setClock(400000);
  initTouchController(true);

  lv_indev_drv_init(&indevDrv);
  indevDrv.type = LV_INDEV_TYPE_POINTER;
  indevDrv.disp = display;
  indevDrv.read_cb = myTouchpadRead;
  lv_indev_drv_register(&indevDrv);

  buildUi();
}

void setup() {
  Serial.begin(115200);
  monitorClearPacket(&g_workPacket);
  monitorClearPacket(&g_livePacket);
  memset(&g_parser, 0, sizeof(g_parser));
  memset(&g_linkStatus, 0, sizeof(g_linkStatus));
  memset(chartPoints, 0, sizeof(chartPoints));
  loadPersistentSettings();

  pinMode(STM32_DATA_RDY_PIN, INPUT_PULLDOWN);
  pinMode(STM32_WAKE_PIN, OUTPUT);
  digitalWrite(STM32_WAKE_PIN, LOW);

  initDisplayAndLvgl();

  Stm32Link.begin(STM32_LINK_BAUD, SERIAL_8N1, STM32_LINK_RX_PIN, STM32_LINK_TX_PIN);
  setLinkStreaming(true);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(g_wifiAutoConnect);
  if (g_wifiAutoConnect && (g_wifiSsid[0] != '\0')) {
    WiFi.begin(g_wifiSsid, g_wifiPassword);
  }
#if defined(CONFIG_BLUEDROID_ENABLED)
  if (g_bleAutoStart && (g_bleName[0] != '\0')) {
    startBleBroadcast(g_bleName);
  }
#endif

  parserSetText(g_parser.lastInfo, sizeof(g_parser.lastInfo), "ESP32 监视器已启动并加载记忆设置");
  g_linkStatus.autoEnabled = true;
  g_lastLinkStatusAutoMs = millis();
#if !LV_TICK_CUSTOM
  g_lastLvglTickMs = millis();
#endif
  refreshLinkStatus();
  refreshAcquirePage();
  updateWifiStatusLabel();
  updateBleStatusLabel();
  applyLivePacketToUi();
}

void loop() {
#if !LV_TICK_CUSTOM
  const uint32_t now = millis();
  lv_tick_inc(now - g_lastLvglTickMs);
  g_lastLvglTickMs = now;
#endif

  pollStm32Link();
  serviceLinkStatusQuery();

  if (g_livePacketDirty) {
    applyLivePacketToUi();
    g_livePacketDirty = false;
  }

  if ((millis() - g_lastStatusRefreshMs) >= 100U) {
    refreshLinkStatus();
    refreshAcquirePage();
    refreshSerialPage();
    updateWifiStatusLabel();
    updateBleStatusLabel();
    g_lastStatusRefreshMs = millis();
  }

  if (!g_touchReady && ((millis() - g_lastTouchRetryMs) >= 1000U)) {
    g_lastTouchRetryMs = millis();
    initTouchController(false);
  }

  lv_timer_handler();
#if (LV_COLOR_16_SWAP != 0)
  gfx->draw16bitBeRGBBitmap(0, 0, (uint16_t *)dispDrawBuf, screenWidth, screenHeight);
#else
  gfx->draw16bitRGBBitmap(0, 0, (uint16_t *)dispDrawBuf, screenWidth, screenHeight);
#endif
  delay(5);
}
