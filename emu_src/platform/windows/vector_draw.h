/* =============================================================================
 * vector_draw.h
 *
 * Shader-based vector beam renderer, ported to C from AAE's vector_draw.cpp.
 * Butt-capped, coverage-AA'd instanced beams + round corner joins / end-caps
 * + procedural shot (fire point) sprites. OpenGL 3.3 only: the whole point of
 * this renderer is the analytic edge AA, so there is no fixed-function
 * fallback - if the shaders cannot be built, beam_init() logs why and returns
 * 0, and every other entry point goes inert.
 *
 * Batches are retained: the caller owns the clear. A sim emits its vector
 * frame with beam_add_line / beam_add_shot after beam_clear(), and the
 * renderer may call beam_draw_all() any number of times in between (render
 * frames between sim frames redraw the same batch).
 *
 * Tuning (line width, gain, AA smoothing, corner strength, fire point size)
 * is read from the [vector] section of the config file at beam_init() and
 * written back so the keys are discoverable; the setters below override at
 * runtime.
 *
 * Copyright (C) 2025-2026  Tim Cottrill
 * SPDX-License-Identifier: GPL-3.0-or-later
 * ============================================================================= */

#pragma once

#ifndef VECTOR_DRAW_H
#define VECTOR_DRAW_H

#include "colordefs.h"

#ifdef __cplusplus
extern "C" {
#endif

// Call once after the GL context exists (and after set_config_file). Builds
// the three shader programs and their instanced VAOs. Returns 0 on any
// failure, after logging and releasing everything it allocated.
int beam_init(void);
void beam_shutdown(void);

// 1 = additive blending, for color vector games (the default).
// 0 = sorted alpha-over, for B/W games where brighter beams occlude darker.
void beam_set_color_mode(int additive);

// Queue one visible beam segment. Intensity 0..255 and the configured gain
// modulate the color; a segment that modulates to black (a pen-up move) is
// dropped. Round joins appear automatically wherever two or more segment
// endpoints coincide, end-caps where exactly one terminates.
void beam_add_line(float sx, float sy, float ex, float ey, int intensity, rgb_t col);

// Same, with an explicit half-width in design units instead of the configured
// [vector] linewidth. AAE's vector fonts use this to keep menu text thinner
// than the game beam.
void beam_add_line_w(float sx, float sy, float ex, float ey, int intensity, rgb_t col,
	float half);

// Queue a procedural shot/fire point (radial core + halo). Intensity scales
// its brightness linearly; the color supplies only the hue.
void beam_add_shot(float ex, float ey, int intensity, rgb_t col);

void beam_clear(void);

// Draw everything queued, into the currently bound target, under an explicit
// column-major projection (mat4_ortho builds one). Leaves blending enabled
// with the standard GL_SRC_ALPHA / GL_ONE_MINUS_SRC_ALPHA func restored.
void beam_draw_all(const float *proj16);

// Runtime overrides for the [vector] config values.
void beam_set_linewidth(float w);        // full beam width, design units
void beam_set_gain(int gain);            // 0..255 additive brightness lift
void beam_set_smoothing(float feather);  // AA feather, design units
void beam_set_corner_strength(float s);  // join disc radius, x half-width
void beam_set_fire_point_size(float s);  // shot half-size, design units

#ifdef __cplusplus
}
#endif

#endif // VECTOR_DRAW_H
