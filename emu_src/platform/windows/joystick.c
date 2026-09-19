/* =============================================================================
 * joystick.c
 *
 * DirectInput 8 joystick backend. See joystick.h for the API and the axis
 * model. Ported to C from the dinput namespace of AAE's Joystick.cpp: COM is
 * driven through the C interface macros, and the C++ pointer-to-member table
 * for optional axes becomes a byte-offset table into DIJOYSTATE2.
 *
 * Copyright (C) 2025-2026  Tim Cottrill
 * SPDX-License-Identifier: GPL-3.0-or-later
 * ============================================================================= */

#define COBJMACROS
#define DIRECTINPUT_VERSION 0x0800

#include <windows.h>
#include <dinput.h>
#include <stdio.h>
#include <stddef.h>   // offsetof
#include <math.h>     // sqrtf for the radial deadzone
#include "joystick.h"
#include "framework.h"   // win_get_window
#include "ini.h"
#include "log.h"

#pragma comment(lib, "dinput8.lib")
#pragma comment(lib, "dxguid.lib")

// Digital threshold for the main stick: +-64 is the historical raw-stick feel
// for generic DirectInput devices (Ultimarc etc.).
#define STICK_THRESHOLD  64

// Default radial deadzone in stick units (of 127): 19 is ~15%, the common
// modern default for rescaled radial deadzones. (XInput's classic constant is
// ~24%, but that dates from raw thresholds without rescaling.) Drifty pad
// sticks rest a few counts off-center; without this they never read a clean 0.
#define DEF_DEADZONE     19

static int cfg_deadzone = DEF_DEADZONE;

int num_joysticks;
int _joystick_installed;
JOYSTICK_INFO joy[MAX_JOYSTICKS];

// Up to 6 optional axes (Z, Rx, Ry, Rz, 2 sliders) become extra 1-axis sticks.
#define MAX_EXTRA_AXES 6

struct di_device
{
	IDirectInputDevice8A *dev;
	GUID guid;
	char guid_str[48];
	char name[64];
	int alive;
	int num_buttons;
	int num_extra;
	size_t extra_ofs[MAX_EXTRA_AXES];   // offsets of the extras in DIJOYSTATE2
	const char *extra_name[MAX_EXTRA_AXES];   // source axis: "Z", "Rx", "S0"...
	int has_pov;
};

static IDirectInput8A *s_di;
static struct di_device s_devices[MAX_JOYSTICKS];
static int s_num_devices;

// Set by joystick_device_change(), consumed by the next poll so all device
// mutation stays on the polling thread.
static volatile int s_rescan_pending;

static int clamp_int(int v, int lo, int hi)
{
	if (v < lo) return lo;
	if (v > hi) return hi;
	return v;
}

static void guid_to_string(const GUID *g, char *out, size_t outlen)
{
	snprintf(out, outlen, "{%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}",
		g->Data1, g->Data2, g->Data3,
		g->Data4[0], g->Data4[1], g->Data4[2], g->Data4[3],
		g->Data4[4], g->Data4[5], g->Data4[6], g->Data4[7]);
}

static void set_axis_range(IDirectInputDevice8A *dev, DWORD ofs, LONG lo, LONG hi)
{
	DIPROPRANGE pr;
	HRESULT hr;

	pr.diph.dwSize = sizeof(pr);
	pr.diph.dwHeaderSize = sizeof(pr.diph);
	pr.diph.dwHow = DIPH_BYOFFSET;
	pr.diph.dwObj = ofs;
	pr.lMin = lo;
	pr.lMax = hi;
	hr = IDirectInputDevice8_SetProperty(dev, DIPROP_RANGE, &pr.diph);
	if (FAILED(hr))
		LOG_ERROR("joystick: SetProperty(DIPROP_RANGE, ofs %lu) failed (0x%08lX)",
			(unsigned long)ofs, (unsigned long)hr);
}

// Build the joy[] descriptor for registered device d: signed X/Y main stick,
// the extra unsigned 1-axis sticks, then a digital hat if the hardware has
// one. This is the shape downstream consumers see.
static void setup_descriptor(int d)
{
	JOYSTICK_INFO *j = &joy[d];
	const struct di_device *D = &s_devices[d];
	int n_stick = 0;
	int b, e;

	j->flags = JOYFLAG_DIGITAL | JOYFLAG_ANALOGUE | JOYFLAG_SIGNED;

	j->stick[n_stick].flags = JOYFLAG_DIGITAL | JOYFLAG_ANALOGUE | JOYFLAG_SIGNED;
	j->stick[n_stick].num_axis = 2;
	j->stick[n_stick].name = "stick";
	j->stick[n_stick].axis[0].name = "X";
	j->stick[n_stick].axis[1].name = "Y";
	n_stick++;

	for (e = 0; e < D->num_extra && n_stick < MAX_JOYSTICK_STICKS - 1; e++)
	{
		j->stick[n_stick].flags = JOYFLAG_DIGITAL | JOYFLAG_ANALOGUE | JOYFLAG_UNSIGNED;
		j->stick[n_stick].num_axis = 1;
		j->stick[n_stick].axis[0].name = "";
		j->stick[n_stick].name = D->extra_name[e];   // "Z", "Rx", "S0"...
		n_stick++;
	}

	if (D->has_pov && n_stick < MAX_JOYSTICK_STICKS)
	{
		j->stick[n_stick].flags = JOYFLAG_DIGITAL | JOYFLAG_SIGNED;
		j->stick[n_stick].num_axis = 2;
		j->stick[n_stick].axis[0].name = "left/right";
		j->stick[n_stick].axis[1].name = "up/down";
		j->stick[n_stick].name = "hat";
		n_stick++;
	}

	j->num_sticks = n_stick;
	j->num_buttons = D->num_buttons;
	for (b = 0; b < j->num_buttons; b++)
		j->button[b].name = "Button";
}

// EnumObjects callback: note which optional axes the device exposes.
struct axis_probe { int z, rx, ry, rz, sliders; };

static BOOL CALLBACK axis_cb(LPCDIDEVICEOBJECTINSTANCEA obj, LPVOID ref)
{
	struct axis_probe *p = (struct axis_probe *)ref;

	if (IsEqualGUID(&obj->guidType, &GUID_ZAxis))       p->z = 1;
	else if (IsEqualGUID(&obj->guidType, &GUID_RxAxis)) p->rx = 1;
	else if (IsEqualGUID(&obj->guidType, &GUID_RyAxis)) p->ry = 1;
	else if (IsEqualGUID(&obj->guidType, &GUID_RzAxis)) p->rz = 1;
	else if (IsEqualGUID(&obj->guidType, &GUID_Slider)) p->sliders++;
	return DIENUM_CONTINUE;
}

static BOOL CALLBACK enum_cb(LPCDIDEVICEINSTANCEA inst, LPVOID ref)
{
	int *added = (int *)ref;
	IDirectInputDevice8A *dev = NULL;
	DIDEVCAPS caps;
	struct axis_probe probe = { 0 };
	struct di_device *D;
	HRESULT hr;
	int i;

	if (s_num_devices >= MAX_JOYSTICKS)
		return DIENUM_STOP;

	// Already registered? (rescan: positions never move.)
	for (i = 0; i < s_num_devices; i++)
		if (IsEqualGUID(&s_devices[i].guid, &inst->guidInstance))
			return DIENUM_CONTINUE;

	hr = IDirectInput8_CreateDevice(s_di, &inst->guidInstance, &dev, NULL);
	if (FAILED(hr) || !dev)
	{
		LOG_ERROR("joystick: CreateDevice failed for %s (0x%08lX)",
			inst->tszInstanceName, (unsigned long)hr);
		return DIENUM_CONTINUE;
	}

	if (FAILED(IDirectInputDevice8_SetDataFormat(dev, &c_dfDIJoystick2)) ||
		FAILED(IDirectInputDevice8_SetCooperativeLevel(dev, win_get_window(),
			DISCL_BACKGROUND | DISCL_NONEXCLUSIVE)))
	{
		LOG_ERROR("joystick: SetDataFormat/SetCooperativeLevel failed for %s",
			inst->tszInstanceName);
		IDirectInputDevice8_Release(dev);
		return DIENUM_CONTINUE;
	}

	caps.dwSize = sizeof(caps);
	if (FAILED(IDirectInputDevice8_GetCapabilities(dev, &caps)))
	{
		LOG_ERROR("joystick: GetCapabilities failed for %s", inst->tszInstanceName);
		IDirectInputDevice8_Release(dev);
		return DIENUM_CONTINUE;
	}

	IDirectInputDevice8_EnumObjects(dev, axis_cb, &probe, DIDFT_AXIS);

	D = &s_devices[s_num_devices];
	memset(D, 0, sizeof(*D));
	D->dev = dev;
	D->guid = inst->guidInstance;
	guid_to_string(&D->guid, D->guid_str, sizeof(D->guid_str));
	snprintf(D->name, sizeof(D->name), "%s", inst->tszInstanceName);
	D->num_buttons = ((int)caps.dwButtons < MAX_JOYSTICK_BUTTONS)
		? (int)caps.dwButtons : MAX_JOYSTICK_BUTTONS;
	D->has_pov = (caps.dwPOVs > 0) ? 1 : 0;
	D->alive = 1;

	// Map optional axes to extra 1-axis sticks (0..255), each labeled with
	// its DIJOYSTATE2 source so displays can say which axis is which.
	// Sliders live in rglSlider[]; DIJOFS_SLIDER(n) addresses them for the
	// range property.
	D->num_extra = 0;
	if (probe.z)
	{
		D->extra_name[D->num_extra] = "Z";
		D->extra_ofs[D->num_extra++] = offsetof(DIJOYSTATE2, lZ);
	}
	if (probe.rx)
	{
		D->extra_name[D->num_extra] = "Rx";
		D->extra_ofs[D->num_extra++] = offsetof(DIJOYSTATE2, lRx);
	}
	if (probe.ry)
	{
		D->extra_name[D->num_extra] = "Ry";
		D->extra_ofs[D->num_extra++] = offsetof(DIJOYSTATE2, lRy);
	}
	if (probe.rz)
	{
		D->extra_name[D->num_extra] = "Rz";
		D->extra_ofs[D->num_extra++] = offsetof(DIJOYSTATE2, lRz);
	}
	for (i = 0; i < probe.sliders && i < 2 && D->num_extra < MAX_EXTRA_AXES; i++)
	{
		D->extra_name[D->num_extra] = (i == 0) ? "S0" : "S1";
		D->extra_ofs[D->num_extra++] = offsetof(DIJOYSTATE2, rglSlider[i]);
	}

	// X/Y signed -128..127; every optional axis 0..255.
	set_axis_range(dev, DIJOFS_X, -128, 127);
	set_axis_range(dev, DIJOFS_Y, -128, 127);
	if (probe.z)  set_axis_range(dev, DIJOFS_Z, 0, 255);
	if (probe.rx) set_axis_range(dev, DIJOFS_RX, 0, 255);
	if (probe.ry) set_axis_range(dev, DIJOFS_RY, 0, 255);
	if (probe.rz) set_axis_range(dev, DIJOFS_RZ, 0, 255);
	for (i = 0; i < probe.sliders && i < 2; i++)
		set_axis_range(dev, DIJOFS_SLIDER(i), 0, 255);

	IDirectInputDevice8_Acquire(dev);

	setup_descriptor(s_num_devices);
	s_num_devices++;
	*added = 1;

	LOG_INFO("joystick %d registered: %s [%s] (%d buttons, %d extra axes%s)",
		s_num_devices, D->name, D->guid_str, D->num_buttons, D->num_extra,
		D->has_pov ? ", hat" : "");

	return DIENUM_CONTINUE;
}

// Enumerate and APPEND unseen devices; existing entries never move. Also
// re-acquires anything that went dead (a re-plugged known device just needs
// acquiring again).
static void rescan(void)
{
	int added = 0;
	int i;
	HRESULT hr;

	if (!s_di) return;

	hr = IDirectInput8_EnumDevices(s_di, DI8DEVCLASS_GAMECTRL, enum_cb, &added,
		DIEDFL_ATTACHEDONLY);
	if (FAILED(hr))
		LOG_ERROR("joystick: EnumDevices failed (0x%08lX)", (unsigned long)hr);

	for (i = 0; i < s_num_devices; i++)
		if (!s_devices[i].alive && s_devices[i].dev)
			if (SUCCEEDED(IDirectInputDevice8_Acquire(s_devices[i].dev)))
				s_devices[i].alive = 1;
}

void joystick_set_deadzone(int radius)
{
	cfg_deadzone = clamp_int(radius, 0, 126);
}

// Radial deadzone with rescale: inside the radius the stick reads (0,0);
// outside it the deflection ramps from 0 at the deadzone edge to full at the
// rim, so there is no jump on the way out and no per-axis diagonal bias.
static void apply_deadzone(int *x, int *y)
{
	float m, scale;
	int dz = cfg_deadzone;

	if (dz <= 0) return;

	m = sqrtf((float)(*x * *x + *y * *y));
	if (m <= (float)dz)
	{
		*x = 0;
		*y = 0;
		return;
	}

	scale = (m - (float)dz) / (float)(127 - dz);
	if (scale > 1.0f) scale = 1.0f;
	*x = clamp_int((int)lroundf((float)*x / m * scale * 127.0f), -128, 127);
	*y = clamp_int((int)lroundf((float)*y / m * scale * 127.0f), -128, 127);
}

int install_joystick(void)
{
	HRESULT hr;

	if (_joystick_installed) return 0;

	// Tuning first; the write-back makes the key discoverable in the file.
	cfg_deadzone = clamp_int(get_config_int("joystick", "deadzone", DEF_DEADZONE), 0, 126);
	set_config_int("joystick", "deadzone", cfg_deadzone);

	if (!win_get_window())
	{
		LOG_ERROR("install_joystick called before the window exists");
		return -1;
	}

	hr = DirectInput8Create(GetModuleHandle(NULL), DIRECTINPUT_VERSION,
		&IID_IDirectInput8A, (LPVOID *)&s_di, NULL);
	if (FAILED(hr) || !s_di)
	{
		LOG_ERROR("DirectInput8Create failed (0x%08lX); no joystick support",
			(unsigned long)hr);
		s_di = NULL;
		return -1;
	}

	s_num_devices = 0;
	num_joysticks = 0;
	memset(joy, 0, sizeof(joy));
	rescan();

	_joystick_installed = 1;
	LOG_INFO("joystick: DirectInput installed, %d device(s)", s_num_devices);
	return 0;
}

void remove_joystick(void)
{
	int i;

	for (i = 0; i < s_num_devices; i++)
	{
		if (s_devices[i].dev)
		{
			IDirectInputDevice8_Unacquire(s_devices[i].dev);
			IDirectInputDevice8_Release(s_devices[i].dev);
			s_devices[i].dev = NULL;
		}
	}
	s_num_devices = 0;

	if (s_di)
	{
		IDirectInput8_Release(s_di);
		s_di = NULL;
	}

	num_joysticks = 0;
	memset(joy, 0, sizeof(joy));
	_joystick_installed = 0;
}

void joystick_device_change(void)
{
	s_rescan_pending = 1;
}

// Decode POV 0 (hundredths of degrees, 0 = up, 0xFFFF = centered) into
// signed direction flags.
static void decode_pov(DWORD pov, int *px, int *py)
{
	int deg;

	*px = *py = 0;
	if ((pov & 0xFFFF) == 0xFFFF) return;

	deg = (int)(pov / 100);
	if (deg > 337 || deg < 23)          *py = -1;
	else if (deg < 68)  { *py = -1; *px = 1; }
	else if (deg < 113)                 *px = 1;
	else if (deg < 158) { *py = 1; *px = 1; }
	else if (deg < 203)                 *py = 1;
	else if (deg < 248) { *py = 1; *px = -1; }
	else if (deg < 293)                 *px = -1;
	else                { *py = -1; *px = -1; }
}

int poll_joystick(void)
{
	int d;

	if (!_joystick_installed) return -1;

	if (s_rescan_pending)
	{
		s_rescan_pending = 0;
		rescan();
	}

	for (d = 0; d < s_num_devices; d++)
	{
		struct di_device *D = &s_devices[d];
		JOYSTICK_INFO *j = &joy[d];
		DIJOYSTATE2 st;
		HRESULT hr;
		int a, b, e, n_stick;

		hr = IDirectInputDevice8_Poll(D->dev);
		if (hr == DIERR_INPUTLOST || hr == DIERR_NOTACQUIRED)
		{
			IDirectInputDevice8_Acquire(D->dev);
			hr = IDirectInputDevice8_Poll(D->dev);
		}
		if (SUCCEEDED(hr) || hr == DI_NOEFFECT)
			hr = IDirectInputDevice8_GetDeviceState(D->dev, sizeof(st), &st);

		if (FAILED(hr))
		{
			// Unplugged: neutral state, keep the slot so players don't shift.
			int s;
			D->alive = 0;
			for (s = 0; s < j->num_sticks; s++)
				for (a = 0; a < j->stick[s].num_axis; a++)
				{
					j->stick[s].axis[a].pos = 0;
					j->stick[s].axis[a].d1 = 0;
					j->stick[s].axis[a].d2 = 0;
				}
			for (b = 0; b < j->num_buttons; b++)
				j->button[b].b = 0;
			continue;
		}
		D->alive = 1;

		// Stick 0: signed X/Y, deadzoned, then digital thresholds - drift can
		// never hold a direction on because d1/d2 see the deadzoned value.
		{
			int x = clamp_int((int)st.lX, -128, 127);
			int y = clamp_int((int)st.lY, -128, 127);
			apply_deadzone(&x, &y);
			j->stick[0].axis[0].pos = x;
			j->stick[0].axis[1].pos = y;
		}
		for (a = 0; a < 2; a++)
		{
			j->stick[0].axis[a].d1 = (j->stick[0].axis[a].pos < -STICK_THRESHOLD) ? 1 : 0;
			j->stick[0].axis[a].d2 = (j->stick[0].axis[a].pos > STICK_THRESHOLD) ? 1 : 0;
		}

		// Extra unsigned axes.
		n_stick = 1;
		for (e = 0; e < D->num_extra && n_stick < j->num_sticks; e++, n_stick++)
		{
			int p = clamp_int((int)*(const LONG *)((const char *)&st + D->extra_ofs[e]), 0, 255);
			j->stick[n_stick].axis[0].pos = p;
			j->stick[n_stick].axis[0].d1 = (p < 64) ? 1 : 0;
			j->stick[n_stick].axis[0].d2 = (p > 192) ? 1 : 0;
		}

		// Hat.
		if (D->has_pov && n_stick < j->num_sticks)
		{
			int px, py;
			decode_pov(st.rgdwPOV[0], &px, &py);
			j->stick[n_stick].axis[0].pos = px * 128;
			j->stick[n_stick].axis[0].d1 = (px < 0) ? 1 : 0;
			j->stick[n_stick].axis[0].d2 = (px > 0) ? 1 : 0;
			j->stick[n_stick].axis[1].pos = py * 128;
			j->stick[n_stick].axis[1].d1 = (py < 0) ? 1 : 0;
			j->stick[n_stick].axis[1].d2 = (py > 0) ? 1 : 0;
		}

		for (b = 0; b < j->num_buttons; b++)
			j->button[b].b = (st.rgbButtons[b] & 0x80) ? 1 : 0;
	}

	num_joysticks = s_num_devices;
	return 0;
}

int joystick_device_count(void)
{
	return s_num_devices;
}

const char *joystick_get_display_name(int index)
{
	if (index < 0 || index >= s_num_devices) return "";
	return s_devices[index].name;
}

int joystick_is_connected(int index)
{
	if (index < 0 || index >= s_num_devices) return 0;
	return s_devices[index].alive;
}
