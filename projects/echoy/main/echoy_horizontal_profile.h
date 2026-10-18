#pragma once
#include "mosaico_mag_classifier.h"

/* One common Flash-backed profile for every Echoy device. The pinned S31 V1
 * values are retained except left_reject_z: its false-left capture is Z=560,
 * while the confirmed B-C-A horizontal seam measures Z=620..623 with X/Y
 * saturated. 600 separates those observed cases, retaining the Z guard and
 * margin on both sides. No device ID, learned baseline or NVS value selects it.
 * Vertical fields keep the BSP's valid defaults; vertical output is excluded. */
static const mosaico_mag_calibration_t echoy_horizontal_profile = {
    .name = "echoy-horizontal-v1",
    .filter_samples = 8,
    .saturation_limit = 30000,
    .left_reject_z = 600,
    .left_y_high = 1050,
    .down_y_both_sides = 60,
    .down_y_with_side = 300,
    .down_y_without_side = 300,
    .down_right_y_without_side = 240,
    .down_right_y_delta = 240,
    .up_right_y = -280,
    .up_right_y_delta = -300,
    .up_right_y_high = -400,
    .up_right_y_with_down = 20,
    .up_right_y_both_with_down = 80,
    .up_right_z_delta_with_down = 50,
    .up_right_y_fallback = -200,
    .up_left_z_with_down = 840,
    .baseline_right_y_min = -100,
    .baseline_right_y_max = 260,
    .baseline_right_y_max_span = 40,
};
