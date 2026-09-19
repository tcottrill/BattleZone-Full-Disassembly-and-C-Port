/*
BMFont example implementation with Kerning, for C++ and OpenGL 2.0

This is free and unencumbered software released into the public domain.

Anyone is free to copy, modify, publish, use, compile, sell, or
distribute this software, either in source code form or as a compiled
binary, for any purpose, commercial or non-commercial, and by any
means.

In jurisdictions that recognize copyright laws, the author or authors
of this software dedicate any and all copyright interest in the
software to the public domain. We make this dedication for the benefit
of the public at large and to the detriment of our heirs and
successors. We intend this dedication to be an overt act of
relinquishment in perpetuity of all present and future rights to this
software under copyright law.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
OTHER DEALINGS IN THE SOFTWARE.

For more information, please refer to <http://unlicense.org/>
*/

#pragma once

#ifndef GL_BASICS_H
#define GL_BASICS_H

#include "glew.h"
#include "wglew.h"
#include <stdbool.h>


// Renderer selection. Must be set before CreateGLContext() to have any effect.
//   0 - default. Best available compatibility context, 4.6 down to 3.3, and
//       the OpenGL 3.3 shader draw path.
//   1 - OpenGL 2.0 compatibility. Skips the version ladder entirely and runs
//       on the plain context, with every draw path forced to its legacy
//       fixed-function form. This is the same route a genuinely old machine
//       takes, so it exercises that code rather than merely selecting it.
// Read from [main] gl_compat in the ini file.
void SetGLCompatMode(int enabled);
int  GLCompatMode(void);

// Creates the highest available OpenGL compatibility context, trying 4.6 down
// to 3.3 before falling back to a legacy context. Returns 1 on success.
int CreateGLContext(void);
void DeleteGLContext(void);

// The version actually obtained, as reported by the driver. This is unaffected
// by compatibility mode -- to decide which draw path to take, ask
// GLModernPathAvailable() instead.
int GetGLVersionMajor(void);
int GetGLVersionMinor(void);
int GLVersionAtLeast(int major, int minor);

// True when the OpenGL 3.3 shader draw path should be used: 3.3 or better is
// genuinely present AND compatibility mode is off. Draw code should branch on
// this rather than on GLVersionAtLeast(3, 3), so that forcing compatibility
// mode reaches every path at once.
int GLModernPathAvailable(void);

// Routes driver debug messages into the log. Needs OpenGL 4.3.
void SetGLDebugOutput(bool enabled);

void CheckGLVersionSupport(void);

// Viewport covers the whole window and one ortho unit is one pixel.
void ViewOrtho(int width, int height);

// Keeps content authored at design_w x design_h filling the window at the
// correct aspect ratio, letterboxing or pillarboxing as needed. The ortho
// stays in design coordinates, so game code never deals in window pixels.
void ViewOrthoScaled(int window_w, int window_h, int design_w, int design_h);

// The viewport last set, needed to map window coordinates (the mouse) back
// into design coordinates.
void GetViewportRect(int *x, int *y, int *w, int *h);

void glSwap(void);

// Swap synchronization, applied after the context exists.
//   0 - free-running. Swaps whenever glSwap is called; tears.
//   1 - vsync. Swaps wait for the vertical blank; no tearing, but a frame
//       rate below the refresh quantizes (40 fps on a 60 Hz panel shows 30).
//   2 - adaptive. Vsync while the frame rate is at or above the refresh,
//       tearing below it - so a 60 fps cap is tear-free and a 40 Hz game
//       still runs at its native rate. Needs WGL_EXT_swap_control_tear.
// Falls back 2 -> 1 -> 0 as support runs out, logging each step, and
// returns the mode actually in effect.
int SetSwapMode(int mode);

GLvoid ReSizeGLScene(GLsizei width, GLsizei height);
void GLRect(int xmin, int xmax, int ymin, int ymax);
void GLPoint(float x, float y);
void GLLine(float sx, float sy, float ex, float ey);

///
/// Usage
/// [... some opengl calls]
/// check_gl_error();
///
void _check_gl_error(const char *file, int line);
#define check_gl_error() _check_gl_error(__FILE__,__LINE__)

#endif
