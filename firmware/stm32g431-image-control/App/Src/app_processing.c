#include "app_processing.h"

#include <math.h>
#include <string.h>

#include "app_config.h"

/* frame->sample_x2 + APP_SKIP_FIRST_ROWS eliminated: median_filter_inplace uses sample_x2 directly */

static const float g_savgol_coeff[APP_SMOOTH_WINDOW] = {
  -9.609141669913e-03f, -8.959875340865e-03f, -8.319323996099e-03f, -7.687487635616e-03f, -7.064366259415e-03f, -6.449959867497e-03f,
  -5.844268459862e-03f, -5.247292036509e-03f, -4.659030597438e-03f, -4.079484142650e-03f, -3.508652672145e-03f, -2.946536185922e-03f,
  -2.393134683982e-03f, -1.848448166324e-03f, -1.312476632948e-03f, -7.852200838555e-04f, -2.666785190452e-04f, 2.431480614825e-04f,
  7.442596577278e-04f, 1.236656269690e-03f, 1.720337897371e-03f, 2.195304540768e-03f, 2.661556199883e-03f, 3.119092874716e-03f,
  3.567914565266e-03f, 4.008021271534e-03f, 4.439412993519e-03f, 4.862089731221e-03f, 5.276051484641e-03f, 5.681298253779e-03f,
  6.077830038634e-03f, 6.465646839206e-03f, 6.844748655496e-03f, 7.215135487503e-03f, 7.576807335228e-03f, 7.929764198670e-03f,
  8.274006077830e-03f, 8.609532972707e-03f, 8.936344883302e-03f, 9.254441809614e-03f, 9.563823751644e-03f, 9.864490709391e-03f,
  1.015644268286e-02f, 1.043967967204e-02f, 1.071420167694e-02f, 1.098000869755e-02f, 1.123710073389e-02f, 1.148547778594e-02f,
  1.172513985371e-02f, 1.195608693720e-02f, 1.217831903640e-02f, 1.239183615132e-02f, 1.259663828196e-02f, 1.279272542832e-02f,
  1.298009759039e-02f, 1.315875476819e-02f, 1.332869696170e-02f, 1.348992417092e-02f, 1.364243639587e-02f, 1.378623363653e-02f,
  1.392131589291e-02f, 1.404768316500e-02f, 1.416533545282e-02f, 1.427427275635e-02f, 1.437449507560e-02f, 1.446600241056e-02f,
  1.454879476125e-02f, 1.462287212765e-02f, 1.468823450977e-02f, 1.474488190761e-02f, 1.479281432116e-02f, 1.483203175043e-02f,
  1.486253419542e-02f, 1.488432165613e-02f, 1.489739413255e-02f, 1.490175162469e-02f, 1.489739413255e-02f, 1.488432165613e-02f,
  1.486253419542e-02f, 1.483203175043e-02f, 1.479281432116e-02f, 1.474488190761e-02f, 1.468823450977e-02f, 1.462287212765e-02f,
  1.454879476125e-02f, 1.446600241056e-02f, 1.437449507560e-02f, 1.427427275635e-02f, 1.416533545282e-02f, 1.404768316500e-02f,
  1.392131589291e-02f, 1.378623363653e-02f, 1.364243639587e-02f, 1.348992417092e-02f, 1.332869696170e-02f, 1.315875476819e-02f,
  1.298009759039e-02f, 1.279272542832e-02f, 1.259663828196e-02f, 1.239183615132e-02f, 1.217831903640e-02f, 1.195608693720e-02f,
  1.172513985371e-02f, 1.148547778594e-02f, 1.123710073389e-02f, 1.098000869755e-02f, 1.071420167694e-02f, 1.043967967204e-02f,
  1.015644268286e-02f, 9.864490709391e-03f, 9.563823751644e-03f, 9.254441809614e-03f, 8.936344883302e-03f, 8.609532972707e-03f,
  8.274006077830e-03f, 7.929764198670e-03f, 7.576807335228e-03f, 7.215135487503e-03f, 6.844748655496e-03f, 6.465646839206e-03f,
  6.077830038634e-03f, 5.681298253779e-03f, 5.276051484641e-03f, 4.862089731221e-03f, 4.439412993519e-03f, 4.008021271534e-03f,
  3.567914565266e-03f, 3.119092874716e-03f, 2.661556199883e-03f, 2.195304540768e-03f, 1.720337897371e-03f, 1.236656269690e-03f,
  7.442596577278e-04f, 2.431480614825e-04f, -2.666785190452e-04f, -7.852200838555e-04f, -1.312476632948e-03f, -1.848448166324e-03f,
  -2.393134683982e-03f, -2.946536185922e-03f, -3.508652672145e-03f, -4.079484142650e-03f, -4.659030597438e-03f, -5.247292036509e-03f,
  -5.844268459862e-03f, -6.449959867497e-03f, -7.064366259415e-03f, -7.687487635616e-03f, -8.319323996099e-03f, -8.959875340865e-03f,
  -9.609141669913e-03f
};

static uint16_t clamp_window(uint16_t count, uint16_t requested)
{
  uint16_t window = requested;

  if (count == 0U)
  {
    return 1U;
  }
  if (window > count)
  {
    window = count;
  }
  if ((window & 1U) == 0U)
  {
    if (window > 1U)
    {
      window--;
    }
    else
    {
      window = 1U;
    }
  }
  return window;
}

static uint32_t compute_median_i16_twice(const int16_t *input, uint16_t count)
{
  int16_t scratch[APP_DARK_ROWS];
  uint16_t i;

  if ((input == NULL) || (count == 0U))
  {
    return 0U;
  }

  if (count > APP_DARK_ROWS)
  {
    count = APP_DARK_ROWS;
  }

  for (i = 0U; i < count; ++i)
  {
    scratch[i] = input[i];
  }

  for (i = 0U; i < count; ++i)
  {
    uint16_t j;
    for (j = i + 1U; j < count; ++j)
    {
      if (scratch[j] < scratch[i])
      {
        int16_t tmp = scratch[i];
        scratch[i] = scratch[j];
        scratch[j] = tmp;
      }
    }
  }

  if ((count & 1U) != 0U)
  {
    return (uint32_t)((uint16_t)scratch[count / 2U]) * 2U;
  }

  return (uint32_t)((uint16_t)scratch[(count / 2U) - 1U]) + (uint32_t)((uint16_t)scratch[count / 2U]);
}

static int16_t clamp_i32_to_i16(int32_t value)
{
  if (value > 32767)
  {
    return 32767;
  }
  if (value < -32768)
  {
    return -32768;
  }
  return (int16_t)value;
}

static void median_filter_inplace(int16_t *data, uint16_t count, uint16_t window)
{
  int16_t scratch[APP_MEDIAN_WINDOW];
  int16_t delay_buf[APP_MEDIAN_WINDOW / 2U];
  uint16_t half;
  uint16_t i;
  uint16_t di = 0U;

  window = clamp_window(count, window);
  half = window / 2U;

  for (i = 0U; i < count; ++i)
  {
    uint16_t j;
    uint16_t len = 0U;
    int16_t median;

    for (j = 0U; j < window; ++j)
    {
      int32_t sample_index = (int32_t)i + (int32_t)j - (int32_t)half;
      if (sample_index < 0)
      {
        sample_index = 0;
      }
      else if (sample_index >= (int32_t)count)
      {
        sample_index = (int32_t)count - 1;
      }
      scratch[len++] = data[(uint16_t)sample_index];
    }

    for (j = 0U; j < len; ++j)
    {
      uint16_t k;
      for (k = j + 1U; k < len; ++k)
      {
        if (scratch[k] < scratch[j])
        {
          int16_t tmp = scratch[j];
          scratch[j] = scratch[k];
          scratch[k] = tmp;
        }
      }
    }

    median = scratch[len / 2U];

    /* Delayed write: data[i-half] is no longer needed for future windows */
    if (i >= half)
    {
      data[i - half] = delay_buf[di];
    }
    delay_buf[di] = median;
    di = (di + 1U) % half;
  }

  /* Flush remaining buffered medians to the last half positions */
  for (i = 0U; i < half; ++i)
  {
    data[count - half + i] = delay_buf[di];
    di = (di + 1U) % half;
  }
}

static float moving_average_sample_x2(const int16_t *input, uint16_t count, uint16_t center, uint16_t window)
{
  uint16_t half;
  uint16_t i;
  int32_t sum_x2 = 0;

  window = clamp_window(count, window);
  half = window / 2U;

  if ((input == NULL) || (count == 0U))
  {
    return 0.0f;
  }

  for (i = 0U; i < window; ++i)
  {
    int32_t sample_index = (int32_t)center + (int32_t)i - (int32_t)half;
    if (sample_index < 0)
    {
      sample_index = 0;
    }
    else if (sample_index >= (int32_t)count)
    {
      sample_index = (int32_t)count - 1;
    }
    sum_x2 += input[(uint16_t)sample_index];
  }

  return ((float)sum_x2 * 0.5f) / (float)window;
}

static float savgol_sample_x2(const int16_t *input, uint16_t count, uint16_t center)
{
  uint16_t half = APP_SMOOTH_WINDOW / 2U;
  uint16_t i;
  float acc = 0.0f;

  if ((input == NULL) || (count == 0U))
  {
    return 0.0f;
  }

  if (count < APP_SMOOTH_WINDOW)
  {
    return moving_average_sample_x2(input, count, center, count);
  }

  for (i = 0U; i < APP_SMOOTH_WINDOW; ++i)
  {
    int32_t sample_index = (int32_t)center + (int32_t)i - (int32_t)half;
    if (sample_index < 0)
    {
      sample_index = 0;
    }
    else if (sample_index >= (int32_t)count)
    {
      sample_index = (int32_t)count - 1;
    }
    acc += g_savgol_coeff[i] * (float)input[(uint16_t)sample_index];
  }

  return acc * 0.5f;
}

static float clamp_non_negative(float value)
{
  return (value > 0.0f) ? value : 0.0f;
}

void AppProcessing_Run(AppFrame *frame)
{
  uint16_t dark_count;
  uint32_t dark_adc_twice;
  uint32_t i;
  uint16_t peak_raw_index = 0U;
  uint16_t peak_smooth_index = 0U;
  int32_t raw_max_x2 = 0;
  int64_t raw_sum_x2 = 0;
  float smooth_max = 0.0f;

  if ((frame == NULL) || (frame->point_count == 0U) || (frame->point_count > APP_FRAME_POINTS_MAX))
  {
    return;
  }

  frame->valid_count = (frame->point_count > APP_SKIP_FIRST_ROWS) ? (frame->point_count - APP_SKIP_FIRST_ROWS) : 0U;
  frame->downsampled_count = 0U;
  memset(frame->smooth_norm_q15, 0, sizeof(frame->smooth_norm_q15));

  if (frame->valid_count == 0U)
  {
    frame->dark_adc = 0.0f;
    frame->peak_pixel_raw = frame->first_pixel;
    frame->peak_pixel_smooth = frame->first_pixel;
    frame->intensity_mean = 0.0f;
    frame->intensity_max = 0.0f;
    return;
  }

  dark_count = (frame->point_count < APP_DARK_ROWS) ? frame->point_count : APP_DARK_ROWS;
  dark_adc_twice = compute_median_i16_twice(frame->sample_x2, dark_count);
  frame->dark_adc = (float)dark_adc_twice * 0.5f;

  for (i = 0U; i < frame->valid_count; ++i)
  {
    uint16_t src = (uint16_t)(APP_SKIP_FIRST_ROWS + i);
    int32_t raw_x2 = (int32_t)dark_adc_twice - ((int32_t)frame->sample_x2[src] * 2);
    frame->sample_x2[src] = clamp_i32_to_i16(raw_x2);
    raw_sum_x2 += (int64_t)frame->sample_x2[src];

    if ((i == 0U) || (frame->sample_x2[src] > raw_max_x2))
    {
      raw_max_x2 = frame->sample_x2[src];
      peak_raw_index = src;
    }
  }

  median_filter_inplace(frame->sample_x2 + APP_SKIP_FIRST_ROWS, frame->valid_count, APP_MEDIAN_WINDOW);

  for (i = 0U; i < frame->valid_count; ++i)
  {
    float smooth_value = clamp_non_negative(savgol_sample_x2(frame->sample_x2 + APP_SKIP_FIRST_ROWS, frame->valid_count, (uint16_t)i));
    if ((i == 0U) || (smooth_value > smooth_max))
    {
      smooth_max = smooth_value;
      peak_smooth_index = (uint16_t)(APP_SKIP_FIRST_ROWS + i);
    }
  }

  for (i = 0U; i < frame->valid_count; i += APP_DOWNSAMPLE_STEP)
  {
    uint16_t out_index = frame->downsampled_count;
    float smooth_value;
    float normalized;

    if (out_index >= APP_DOWNSAMPLED_POINTS_MAX)
    {
      break;
    }

    smooth_value = clamp_non_negative(savgol_sample_x2(frame->sample_x2 + APP_SKIP_FIRST_ROWS, frame->valid_count, (uint16_t)i));
    normalized = (smooth_max > 0.0f) ? (smooth_value / smooth_max) : 0.0f;

    if (normalized <= 0.0f)
    {
      frame->smooth_norm_q15[out_index] = 0U;
    }
    else if (normalized >= 1.0f)
    {
      frame->smooth_norm_q15[out_index] = APP_NORM_Q15_SCALE;
    }
    else
    {
      frame->smooth_norm_q15[out_index] = (uint16_t)(normalized * (float)APP_NORM_Q15_SCALE + 0.5f);
    }
    frame->downsampled_count++;
  }

  frame->intensity_mean = ((float)raw_sum_x2 * 0.5f) / (float)frame->valid_count;
  frame->intensity_max = (float)raw_max_x2 * 0.5f;
  frame->peak_pixel_raw = (uint16_t)(frame->first_pixel + peak_raw_index);
  frame->peak_pixel_smooth = (uint16_t)(frame->first_pixel + peak_smooth_index);
}

float AppProcessing_ComputePairCorrelationQ15(const uint16_t *a, const uint16_t *b, uint16_t count)
{
  float mean_a = 0.0f;
  float mean_b = 0.0f;
  float num = 0.0f;
  float den_a = 0.0f;
  float den_b = 0.0f;
  uint16_t i;

  if ((a == NULL) || (b == NULL) || (count == 0U))
  {
    return 0.0f;
  }

  for (i = 0U; i < count; ++i)
  {
    mean_a += (float)a[i];
    mean_b += (float)b[i];
  }
  mean_a /= (float)count;
  mean_b /= (float)count;

  for (i = 0U; i < count; ++i)
  {
    float da = a[i] - mean_a;
    float db = b[i] - mean_b;
    num += da * db;
    den_a += da * da;
    den_b += db * db;
  }

  if ((den_a <= 1e-9f) || (den_b <= 1e-9f))
  {
    return 0.0f;
  }

  return num / sqrtf(den_a * den_b);
}
