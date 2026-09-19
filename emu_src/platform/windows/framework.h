#pragma once

#include <Windows.h>
#include "glew.h"
#include "log.h"

extern HWND win_get_window(void);

// Borderless fullscreen, also bound to ALT+ENTER in the window procedure.
extern void ToggleFullscreen(void);
extern int IsFullscreen(void);
extern int KeyCheck(int keynum);
extern void ViewOrtho(int width, int height);
// Window client area, in physical pixels. Follows the window as it resizes.
extern int SCREEN_W;
extern int SCREEN_H;

// The fixed coordinate space all drawing is authored in. Independent of the
// window size: the viewport scales this to fit, preserving aspect ratio.
extern int DESIGN_W;
extern int DESIGN_H;
