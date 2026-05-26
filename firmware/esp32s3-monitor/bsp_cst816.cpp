#include "bsp_cst816.h"

namespace {

constexpr uint8_t kCst816Address = 0x15U;
constexpr uint8_t kRegFingerNum = 0x02U;
constexpr uint8_t kTouchFrameSize = 5U;

TwoWire *g_touchWire = nullptr;
uint8_t g_touchRotation = 0U;
uint16_t g_touchWidth = 0U;
uint16_t g_touchHeight = 0U;
uint16_t g_touchX = 0U;
uint16_t g_touchY = 0U;
bool g_touchPressed = false;

bool touchReadBytes(uint8_t reg, uint8_t *data, size_t size) {
  if ((g_touchWire == nullptr) || (data == nullptr) || (size == 0U)) {
    return false;
  }

  g_touchWire->beginTransmission(kCst816Address);
  g_touchWire->write(reg);
  if (g_touchWire->endTransmission(false) != 0) {
    return false;
  }

  if (g_touchWire->requestFrom((int)kCst816Address, (int)size) != (int)size) {
    return false;
  }

  for (size_t i = 0; i < size; ++i) {
    data[i] = (uint8_t)g_touchWire->read();
  }
  return true;
}

void touchTransform(uint16_t rawX, uint16_t rawY, uint16_t *x, uint16_t *y) {
  uint16_t mappedX = rawX;
  uint16_t mappedY = rawY;

  switch (g_touchRotation & 0x03U) {
    case 1U:
      mappedX = rawY;
      mappedY = (rawX >= g_touchHeight) ? 0U : (uint16_t)(g_touchHeight - 1U - rawX);
      break;
    case 2U:
      mappedX = (rawX >= g_touchWidth) ? 0U : (uint16_t)(g_touchWidth - 1U - rawX);
      mappedY = (rawY >= g_touchHeight) ? 0U : (uint16_t)(g_touchHeight - 1U - rawY);
      break;
    case 3U:
      mappedX = (rawY >= g_touchWidth) ? 0U : (uint16_t)(g_touchWidth - 1U - rawY);
      mappedY = rawX;
      break;
    default:
      break;
  }

  if (mappedX >= g_touchWidth) {
    mappedX = (g_touchWidth > 0U) ? (uint16_t)(g_touchWidth - 1U) : 0U;
  }
  if (mappedY >= g_touchHeight) {
    mappedY = (g_touchHeight > 0U) ? (uint16_t)(g_touchHeight - 1U) : 0U;
  }

  if (x != nullptr) {
    *x = mappedX;
  }
  if (y != nullptr) {
    *y = mappedY;
  }
}

}  // namespace

bool bsp_touch_init(TwoWire *wire, uint8_t rotation, uint16_t width, uint16_t height) {
  uint8_t fingerCount = 0U;

  if ((wire == nullptr) || (width == 0U) || (height == 0U)) {
    return false;
  }

  g_touchWire = wire;
  g_touchRotation = rotation;
  g_touchWidth = width;
  g_touchHeight = height;
  g_touchPressed = false;
  g_touchX = 0U;
  g_touchY = 0U;

  return touchReadBytes(kRegFingerNum, &fingerCount, 1U);
}

void bsp_touch_read(void) {
  uint8_t frame[kTouchFrameSize] = {0};
  uint16_t rawX = 0U;
  uint16_t rawY = 0U;

  if (!touchReadBytes(kRegFingerNum, frame, sizeof(frame))) {
    g_touchPressed = false;
    return;
  }

  if ((frame[0] & 0x0FU) == 0U) {
    g_touchPressed = false;
    return;
  }

  rawX = (uint16_t)(((uint16_t)(frame[1] & 0x0FU) << 8) | frame[2]);
  rawY = (uint16_t)(((uint16_t)(frame[3] & 0x0FU) << 8) | frame[4]);
  touchTransform(rawX, rawY, &g_touchX, &g_touchY);
  g_touchPressed = true;
}

bool bsp_touch_get_coordinates(uint16_t *x, uint16_t *y) {
  if (!g_touchPressed) {
    return false;
  }

  if (x != nullptr) {
    *x = g_touchX;
  }
  if (y != nullptr) {
    *y = g_touchY;
  }
  return true;
}
