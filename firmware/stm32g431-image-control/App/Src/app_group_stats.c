#include "app_group_stats.h"

#include <math.h>
#include <string.h>

#include "app_processing.h"

typedef struct
{
  uint8_t used;
  AppGroupStats stats;
  uint16_t last_curve_q15[APP_DOWNSAMPLED_POINTS_MAX];
  uint8_t has_last_curve;
  float peak_pixel_sum;
  float peak_pixel_sq_sum;
} AppGroupSlot;

static AppGroupSlot g_slots[APP_GROUP_COUNT_MAX] __attribute__((section(".ccm_ram")));

static AppGroupSlot *find_or_create_slot(uint8_t group_id)
{
  uint8_t i;
  AppGroupSlot *free_slot = 0;

  for (i = 0U; i < APP_GROUP_COUNT_MAX; ++i)
  {
    if (g_slots[i].used != 0U)
    {
      if (g_slots[i].stats.group_id == group_id)
      {
        return &g_slots[i];
      }
    }
    else if (free_slot == 0)
    {
      free_slot = &g_slots[i];
    }
  }

  if (free_slot != 0)
  {
    memset(free_slot, 0, sizeof(*free_slot));
    free_slot->used = 1U;
    free_slot->stats.group_id = group_id;
    free_slot->stats.min_pair_correlation = 1.0f;
    return free_slot;
  }

  return 0;
}

void AppGroupStats_Init(void)
{
  memset(g_slots, 0, sizeof(g_slots));
}

void AppGroupStats_Accumulate(const AppFrame *frame)
{
  AppGroupSlot *slot;
  uint16_t curve_count;
  uint16_t i;

  if ((frame == NULL) || (frame->valid_count == 0U) || (frame->downsampled_count == 0U))
  {
    return;
  }

  slot = find_or_create_slot(frame->group_id);
  if (slot == 0)
  {
    return;
  }

  curve_count = frame->downsampled_count;
  slot->stats.valid_count = frame->valid_count;

  if (slot->stats.replicate_count == 0U)
  {
    slot->stats.downsampled_count = curve_count;
    slot->stats.mean_point_cv = 0.0f;
    slot->stats.mean_pair_correlation = 0.0f;
    slot->stats.min_pair_correlation = 1.0f;
  }
  else
  {
    float cv_sum = 0.0f;
    uint16_t cv_count = 0U;
    float corr = 0.0f;
    uint16_t shared_count = (slot->stats.downsampled_count < curve_count) ?
                            slot->stats.downsampled_count : curve_count;

    if ((slot->has_last_curve != 0U) && (shared_count > 0U))
    {
      corr = AppProcessing_ComputePairCorrelationQ15(slot->last_curve_q15, frame->smooth_norm_q15, shared_count);
      if (slot->stats.replicate_count == 1U)
      {
        slot->stats.mean_pair_correlation = corr;
        slot->stats.min_pair_correlation = corr;
      }
      else
      {
        float n = (float)(slot->stats.replicate_count - 1U);
        slot->stats.mean_pair_correlation = (slot->stats.mean_pair_correlation * n + corr) / (n + 1.0f);
        if (corr < slot->stats.min_pair_correlation)
        {
          slot->stats.min_pair_correlation = corr;
        }
      }
    }

    for (i = 0U; i < shared_count; ++i)
    {
      float prev_value = (float)slot->last_curve_q15[i];
      float curr_value = (float)frame->smooth_norm_q15[i];
      float denom = (prev_value + curr_value) * 0.5f;

      if (denom > 1.0f)
      {
        float approx_cv = fabsf(curr_value - prev_value) / denom;
        cv_sum += approx_cv;
        cv_count++;
      }
    }

    if (cv_count > 0U)
    {
      slot->stats.mean_point_cv = cv_sum / (float)cv_count;
    }

    slot->stats.downsampled_count = curve_count;
  }

  memcpy(slot->last_curve_q15, frame->smooth_norm_q15, (uint32_t)curve_count * sizeof(uint16_t));
  slot->has_last_curve = 1U;
  slot->stats.replicate_count++;

  slot->peak_pixel_sum += (float)frame->peak_pixel_smooth;
  slot->peak_pixel_sq_sum += (float)frame->peak_pixel_smooth * (float)frame->peak_pixel_smooth;
  slot->stats.mean_peak_pixel = slot->peak_pixel_sum / (float)slot->stats.replicate_count;

  if (slot->stats.replicate_count > 1U)
  {
    float mean = slot->stats.mean_peak_pixel;
    float var = (slot->peak_pixel_sq_sum - (float)slot->stats.replicate_count * mean * mean) /
                (float)(slot->stats.replicate_count - 1U);
    slot->stats.peak_pixel_std = (var > 0.0f) ? sqrtf(var) : 0.0f;
  }
  else
  {
    slot->stats.peak_pixel_std = 0.0f;
  }
}

uint8_t AppGroupStats_GetCount(void)
{
  uint8_t i;
  uint8_t count = 0U;

  for (i = 0U; i < APP_GROUP_COUNT_MAX; ++i)
  {
    if (g_slots[i].used != 0U)
    {
      count++;
    }
  }

  return count;
}

const AppGroupStats *AppGroupStats_GetByIndex(uint8_t index)
{
  uint8_t i;
  uint8_t seen = 0U;

  for (i = 0U; i < APP_GROUP_COUNT_MAX; ++i)
  {
    if (g_slots[i].used != 0U)
    {
      if (seen == index)
      {
        return &g_slots[i].stats;
      }
      seen++;
    }
  }

  return 0;
}
