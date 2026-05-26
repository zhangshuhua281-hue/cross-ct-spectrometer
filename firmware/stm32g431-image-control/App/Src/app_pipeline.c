#include "app_pipeline.h"

#include <stdio.h>
#include <string.h>

#include "app_config.h"
#include "app_processing.h"

static uint16_t g_expected_points = 0U;
static uint16_t g_received_points = 0U;
static uint8_t g_header_received = 0U;

static void pipeline_reset(void)
{
  g_expected_points = 0U;
  g_received_points = 0U;
  g_header_received = 0U;
}

void AppPipeline_Init(void)
{
  pipeline_reset();
}

AppPipelineStatus AppPipeline_ProcessAsciiLine(const char *text, AppFrame *frame, char *error_text, uint16_t error_size)
{
  int parsed = 0;

  if ((text == NULL) || (frame == NULL))
  {
    return APP_PIPELINE_ERROR;
  }

  if (strncmp(text, "FRAME ", 6) == 0)
  {
    memset(frame, 0, sizeof(*frame));
    if (sscanf(text, "FRAME %hhu %hhu %hu%n",
               &frame->group_id,
               &frame->replicate_id,
               &frame->point_count,
               &parsed) != 3)
    {
      if ((error_text != NULL) && (error_size > 0U))
      {
        snprintf(error_text, error_size, "bad header");
      }
      pipeline_reset();
      return APP_PIPELINE_ERROR;
    }
    if ((frame->point_count == 0U) || (frame->point_count > APP_FRAME_POINTS_MAX))
    {
      if ((error_text != NULL) && (error_size > 0U))
      {
        snprintf(error_text, error_size, "point count out of range");
      }
      pipeline_reset();
      return APP_PIPELINE_ERROR;
    }
    g_expected_points = frame->point_count;
    g_received_points = 0U;
    g_header_received = 1U;
    return APP_PIPELINE_COLLECTING;
  }

  if ((strncmp(text, "END", 3) == 0) && (g_header_received != 0U))
  {
    if (g_received_points != g_expected_points)
    {
      if ((error_text != NULL) && (error_size > 0U))
      {
        snprintf(error_text, error_size, "point count mismatch");
      }
      pipeline_reset();
      return APP_PIPELINE_ERROR;
    }

    AppProcessing_Run(frame);
    pipeline_reset();
    return APP_PIPELINE_READY;
  }

  if (g_header_received != 0U)
  {
    uint16_t pixel = 0U;
    uint16_t adc = 0U;

    if (g_received_points >= g_expected_points)
    {
      if ((error_text != NULL) && (error_size > 0U))
      {
        snprintf(error_text, error_size, "too many points");
      }
      pipeline_reset();
      return APP_PIPELINE_ERROR;
    }

    if (sscanf(text, "%hu,%hu", &pixel, &adc) != 2)
    {
      if ((error_text != NULL) && (error_size > 0U))
      {
        snprintf(error_text, error_size, "bad point line");
      }
      pipeline_reset();
      return APP_PIPELINE_ERROR;
    }

    if (g_received_points == 0U)
    {
      frame->first_pixel = pixel;
    }
    else if (pixel != (uint16_t)(frame->first_pixel + g_received_points))
    {
      if ((error_text != NULL) && (error_size > 0U))
      {
        snprintf(error_text, error_size, "pixel axis not contiguous");
      }
      pipeline_reset();
      return APP_PIPELINE_ERROR;
    }

    frame->sample_x2[g_received_points] = (int16_t)adc;
    g_received_points++;
    return APP_PIPELINE_COLLECTING;
  }

  if ((error_text != NULL) && (error_size > 0U))
  {
    snprintf(error_text, error_size, "unexpected line");
  }
  return APP_PIPELINE_ERROR;
}

AppPipelineStatus AppPipeline_ProcessF103Frame12Bit(const uint8_t *data,
                                                    uint16_t size,
                                                    uint8_t group_id,
                                                    uint8_t replicate_id,
                                                    AppFrame *frame,
                                                    char *error_text,
                                                    uint16_t error_size)
{
  uint16_t i;

  if ((data == NULL) || (frame == NULL))
  {
    if ((error_text != NULL) && (error_size > 0U))
    {
      snprintf(error_text, error_size, "null frame");
    }
    return APP_PIPELINE_ERROR;
  }

  if (size != (uint16_t)(APP_FRAME_POINTS_MAX * 2U))
  {
    if ((error_text != NULL) && (error_size > 0U))
    {
      snprintf(error_text, error_size, "bad f103 frame size");
    }
    return APP_PIPELINE_ERROR;
  }

  /* Parse raw data into sample_x2 BEFORE clearing other fields.
     This handles the in-place case where data points to frame->sample_x2. */
  for (i = 0U; i < APP_FRAME_POINTS_MAX; ++i)
  {
    uint16_t raw = (uint16_t)data[(uint16_t)(i * 2U)] |
                   (uint16_t)((uint16_t)data[(uint16_t)(i * 2U + 1U)] << 8);
    frame->sample_x2[i] = (int16_t)raw;
  }

  /* Clear frame fields (sample_x2 is left intact) */
  frame->point_count = APP_FRAME_POINTS_MAX;
  frame->valid_count = 0U;
  frame->first_pixel = APP_F103_FIRST_PIXEL;
  frame->group_id = group_id;
  frame->replicate_id = replicate_id;
  frame->downsampled_count = 0U;
  frame->dark_adc = 0.0f;
  frame->peak_pixel_raw = 0U;
  frame->peak_pixel_smooth = 0U;
  frame->intensity_mean = 0.0f;
  frame->intensity_max = 0.0f;
  memset(frame->smooth_norm_q15, 0, sizeof(frame->smooth_norm_q15));

  AppProcessing_Run(frame);
  return APP_PIPELINE_READY;
}
