#ifndef APP_TYPES_H
#define APP_TYPES_H

#include <stdint.h>

#include "app_config.h"

typedef struct
{
  uint16_t point_count;
  uint16_t valid_count;
  uint16_t first_pixel;
  uint8_t group_id;
  uint8_t replicate_id;
  int16_t sample_x2[APP_FRAME_POINTS_MAX];
  uint16_t downsampled_count;
  uint16_t smooth_norm_q15[APP_DOWNSAMPLED_POINTS_MAX];
  float dark_adc;
  uint16_t peak_pixel_raw;
  uint16_t peak_pixel_smooth;
  float intensity_mean;
  float intensity_max;
} AppFrame;

typedef struct
{
  uint8_t group_id;
  uint8_t replicate_count;
  uint16_t valid_count;
  uint16_t downsampled_count;
  float mean_point_cv;
  float mean_pair_correlation;
  float min_pair_correlation;
  float mean_peak_pixel;
  float peak_pixel_std;
} AppGroupStats;

#endif
