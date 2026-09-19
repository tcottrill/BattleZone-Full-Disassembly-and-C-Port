/* =============================================================================
 * joystick.h
 *
 * DirectInput-only joystick input, ported to C from AAE's unified joystick
 * layer (system/input/Joystick.cpp, dinput backend). Rudimentary by design:
 * no XInput, no WinMM fallback, no rumble, no button combos. Generic HID
 * sticks and gamepads are read through DirectInput 8 with their native axis
 * and button layout.
 *
 * The public data structures are Allegro-4 compatible, so legacy code that
 * reads joy[], joy_x, joy_b1, etc. works without changes.
 *
 * Usage:
 *   install_joystick();       // once, after the window exists
 *   poll_joystick();          // once per logic tick; fills joy[]/num_joysticks
 *   if (joy_left || joy_b1) { ... }
 *   remove_joystick();        // at shutdown
 *
 * Devices are enumerated at install and re-scanned after a WM_DEVICECHANGE
 * (call joystick_device_change() from the window procedure; the rescan runs
 * on the next poll so all device mutation stays on the polling thread).
 * Slots are stable: new devices append, existing ones never move, and an
 * unplugged device reads neutral until it is re-acquired.
 *
 * Axis model (per device):
 *   stick[0]      = main stick, signed X/Y: -128..127, digital at +-64
 *   stick[1..N-1] = extra axes (Z/Rx/Ry/Rz), unsigned 0..255, one per stick
 *   stick[N]      = POV hat if present, digital only
 * Y axis convention: negative = up, positive = down (screen-space).
 *
 * Copyright (C) 2025-2026  Tim Cottrill
 * SPDX-License-Identifier: GPL-3.0-or-later
 * ============================================================================= */

#pragma once

#ifndef JOYSTICK_H
#define JOYSTICK_H

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_JOYSTICKS           8
#define MAX_JOYSTICK_AXIS       3
#define MAX_JOYSTICK_STICKS     5
#define MAX_JOYSTICK_BUTTONS    32

// Joystick status flags
#define JOYFLAG_DIGITAL         1
#define JOYFLAG_ANALOGUE        2
#define JOYFLAG_SIGNED          32
#define JOYFLAG_UNSIGNED        64
#define JOYFLAG_ANALOG          JOYFLAG_ANALOGUE

// One axis of one stick.
typedef struct JOYSTICK_AXIS_INFO
{
	int pos;            // -128..127 (signed sticks) or 0..255 (unsigned extras)
	int d1;             // digital: negative direction (left / up)
	int d2;             // digital: positive direction (right / down)
	const char *name;
} JOYSTICK_AXIS_INFO;

// One stick, slider, or hat.
typedef struct JOYSTICK_STICK_INFO
{
	int flags;
	int num_axis;
	JOYSTICK_AXIS_INFO axis[MAX_JOYSTICK_AXIS];
	const char *name;
} JOYSTICK_STICK_INFO;

typedef struct JOYSTICK_BUTTON_INFO
{
	int b;              // 0 or 1
	const char *name;
} JOYSTICK_BUTTON_INFO;

typedef struct JOYSTICK_INFO
{
	int flags;
	int num_sticks;
	int num_buttons;
	JOYSTICK_STICK_INFO stick[MAX_JOYSTICK_STICKS];
	JOYSTICK_BUTTON_INFO button[MAX_JOYSTICK_BUTTONS];
} JOYSTICK_INFO;

// Global state, valid after poll_joystick().
extern int num_joysticks;
extern int _joystick_installed;
extern JOYSTICK_INFO joy[MAX_JOYSTICKS];

// Allegro-style convenience macros, players 1 and 2.
#define joy_x           (joy[0].stick[0].axis[0].pos)
#define joy_y           (joy[0].stick[0].axis[1].pos)
#define joy_left        (joy[0].stick[0].axis[0].d1)
#define joy_right       (joy[0].stick[0].axis[0].d2)
#define joy_up          (joy[0].stick[0].axis[1].d1)
#define joy_down        (joy[0].stick[0].axis[1].d2)
#define joy_b1          (joy[0].button[0].b)
#define joy_b2          (joy[0].button[1].b)
#define joy_b3          (joy[0].button[2].b)
#define joy_b4          (joy[0].button[3].b)
#define joy_b5          (joy[0].button[4].b)
#define joy_b6          (joy[0].button[5].b)
#define joy_b7          (joy[0].button[6].b)
#define joy_b8          (joy[0].button[7].b)
// Xbox-style pads via DirectInput put the stick clicks here (L3 = 9, R3 = 10).
#define joy_b9          (joy[0].button[8].b)
#define joy_b10         (joy[0].button[9].b)

#define joy2_x          (joy[1].stick[0].axis[0].pos)
#define joy2_y          (joy[1].stick[0].axis[1].pos)
#define joy2_left       (joy[1].stick[0].axis[0].d1)
#define joy2_right      (joy[1].stick[0].axis[0].d2)
#define joy2_up         (joy[1].stick[0].axis[1].d1)
#define joy2_down       (joy[1].stick[0].axis[1].d2)
#define joy2_b1         (joy[1].button[0].b)
#define joy2_b2         (joy[1].button[1].b)
#define joy2_b3         (joy[1].button[2].b)
#define joy2_b4         (joy[1].button[3].b)
#define joy2_b5         (joy[1].button[4].b)
#define joy2_b6         (joy[1].button[5].b)
#define joy2_b7         (joy[1].button[6].b)
#define joy2_b8         (joy[1].button[7].b)
#define joy2_b9         (joy[1].button[8].b)
#define joy2_b10        (joy[1].button[9].b)

// Initialize DirectInput and enumerate attached sticks. Returns 0 on success
// (even with zero devices connected - one can arrive later via device-change),
// -1 if DirectInput itself is unavailable, after logging why. Safe to call
// again once installed (no-op).
int install_joystick(void);

// Shut down and release every device. Safe to call when not installed.
void remove_joystick(void);

// Poll current state into joy[] / num_joysticks. Returns 0 on success, -1 if
// joystick support is not installed. Runs a deferred device rescan first if
// joystick_device_change() was called since the last poll.
int poll_joystick(void);

// Call from the window procedure on WM_DEVICECHANGE. Sets a flag; the next
// poll_joystick() re-enumerates (new devices append, slots never move) and
// re-acquires anything that was unplugged.
void joystick_device_change(void);

// Radial deadzone for the main stick, in stick units (0..126, 0 = off).
// Inside the radius both axes read 0; outside it the deflection rescales
// smoothly from 0 at the edge to full at the rim. Read from [joystick]
// deadzone at install (default 19, ~15% - the usual figure for rescaled
// radial deadzones); this overrides at runtime.
void joystick_set_deadzone(int radius);

// Device display info: count of registered slots, a human-readable name, and
// whether slot i currently has live hardware behind it.
int joystick_device_count(void);
const char *joystick_get_display_name(int index);
int joystick_is_connected(int index);

#ifdef __cplusplus
}
#endif

#endif // JOYSTICK_H
