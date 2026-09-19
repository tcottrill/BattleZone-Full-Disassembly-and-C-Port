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

#include <windows.h>
#include "sys_gl.h"
#include "framework.h"
#include <stdio.h>
#include <stdlib.h>
#include "log.h"



// Required OpenGL Libraries, placed here to make it easier
#pragma comment(lib, "opengl32.lib")
#pragma comment(lib, "glu32.lib")

//OpenGL Globals for context
static HDC hDC;
static HGLRC hRC;

#pragma warning (disable : 4996)

// Based on https://blog.nobel-joergensen.com/2013/01/29/debugging-opengl-using-glgeterror/
void _check_gl_error(const char *file, int line)
{
	GLenum err = glGetError();

	while (err != GL_NO_ERROR)
	{
		const char *error = "UNKNOWN_ERROR";

		switch (err) {
		case GL_INVALID_OPERATION:             error = "INVALID_OPERATION"; break;
		case GL_INVALID_ENUM:                  error = "INVALID_ENUM"; break;
		case GL_INVALID_VALUE:                 error = "INVALID_VALUE"; break;
		case GL_OUT_OF_MEMORY:                 error = "OUT_OF_MEMORY"; break;
		case GL_INVALID_FRAMEBUFFER_OPERATION: error = "INVALID_FRAMEBUFFER_OPERATION"; break;
		}

		LOG_ERROR("OpenGL Error: %s in %s at line %d", error, file, line);
		err = glGetError();
	}
}

// Version actually obtained, filled in by query_gl_version().
static int gl_major;
static int gl_minor;

// Set from [main] gl_compat before the context is created.
static int gl_compat_mode;

void SetGLCompatMode(int enabled)
{
	gl_compat_mode = enabled ? 1 : 0;
}

int GLCompatMode(void)
{
	return gl_compat_mode;
}

// Tried highest first. 3.3 is the floor that matters: the modern draw paths
// use GLSL 330 with layout(location=) qualifiers, so a machine that tops out
// below 3.3 runs the legacy fixed-function path instead. Requesting only 4.4
// and giving up meant a 3.3-capable card dropped all the way to GL 2.1.
static const struct { int major, minor; } gl_version_targets[] =
{
	{ 4, 6 }, { 4, 5 }, { 4, 4 }, { 4, 3 }, { 4, 2 }, { 4, 1 }, { 4, 0 }, { 3, 3 }
};

static HGLRC create_versioned_context(int major, int minor)
{
	GLint attribs[] =
	{
		WGL_CONTEXT_MAJOR_VERSION_ARB, major,
		WGL_CONTEXT_MINOR_VERSION_ARB, minor,
		// Compatibility, not core: the bitmap font and the legacy draw path
		// still use fixed-function calls that core profile removes.
		WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB,
		0
	};

	return wglCreateContextAttribsARB(hDC, 0, attribs);
}

// Ask the driver what we actually got rather than trusting what we asked for.
static void query_gl_version(void)
{
	const char *version;

	while (glGetError() != GL_NO_ERROR) { /* drain */ }

	gl_major = 0;
	gl_minor = 0;
	glGetIntegerv(GL_MAJOR_VERSION, &gl_major);
	glGetIntegerv(GL_MINOR_VERSION, &gl_minor);

	// GL_MAJOR_VERSION only exists in 3.0+; on anything older it raises
	// GL_INVALID_ENUM and leaves the values untouched.
	if (glGetError() != GL_NO_ERROR || gl_major == 0)
	{
		version = (const char *)glGetString(GL_VERSION);
		if (!version || sscanf(version, "%d.%d", &gl_major, &gl_minor) != 2)
		{
			gl_major = 1;
			gl_minor = 0;
		}
	}
}

int GetGLVersionMajor(void)
{
	return gl_major;
}

int GetGLVersionMinor(void)
{
	return gl_minor;
}

int GLVersionAtLeast(int major, int minor)
{
	return (gl_major > major) || (gl_major == major && gl_minor >= minor);
}

int GLModernPathAvailable(void)
{
	return !gl_compat_mode && GLVersionAtLeast(3, 3);
}

static void APIENTRY gl_debug_callback(GLenum source, GLenum type, GLuint id,
	GLenum severity, GLsizei length, const GLchar *message, const void *userParam)
{
	(void)source; (void)id; (void)length; (void)userParam;

	if (type == GL_DEBUG_TYPE_ERROR || severity == GL_DEBUG_SEVERITY_HIGH)
		LOG_ERROR("GL debug: %s", message);
	else if (severity == GL_DEBUG_SEVERITY_MEDIUM || severity == GL_DEBUG_SEVERITY_LOW)
		LOG_WARN("GL debug: %s", message);
	else
		LOG_DEBUG("GL debug: %s", message);
}

void SetGLDebugOutput(bool enabled)
{
	if (!GLEW_VERSION_4_3 || glDebugMessageCallback == NULL)
	{
		LOG_WARN("GL debug output needs OpenGL 4.3, running %d.%d", gl_major, gl_minor);
		return;
	}

	if (enabled)
	{
		glEnable(GL_DEBUG_OUTPUT);
		glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
		glDebugMessageCallback(gl_debug_callback, NULL);
		glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, NULL, GL_TRUE);
		LOG_INFO("GL debug output enabled");
	}
	else
	{
		glDisable(GL_DEBUG_OUTPUT);
		glDebugMessageCallback(NULL, NULL);
	}
}

//Enable an OpenGL 3.3+ compatibility context, falling back as far as needed
int CreateGLContext(void)
{
	PIXELFORMATDESCRIPTOR pfd;
	int iFormat;
	// Get the device context (DC)
	hDC = GetDC(win_get_window());
	if (!hDC)
	{
		LOG_ERROR("Failed to get a device context");
		return 0;
	}
	// Set the pixel format for the DC
	ZeroMemory(&pfd, sizeof(pfd));
	pfd.nSize = sizeof(pfd);
	pfd.nVersion = 1;
	pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
	pfd.iPixelType = PFD_TYPE_RGBA;
	pfd.cColorBits = 32;
	pfd.cDepthBits = 24;
	pfd.cStencilBits = 8;
	pfd.iLayerType = PFD_MAIN_PLANE;
	iFormat = ChoosePixelFormat(hDC, &pfd);
	if (iFormat == 0 || !SetPixelFormat(hDC, iFormat, &pfd))
	{
		LOG_ERROR("Failed to set a pixel format");
		return 0;
	}

	// Create and enable a basic render context (RC) first
	HGLRC tempContext = wglCreateContext(hDC);
	if (!tempContext)
	{
		LOG_ERROR("Failed to create an OpenGL context");
		return 0;
	}
	if (!wglMakeCurrent(hDC, tempContext))
	{
		LOG_ERROR("Failed to make the bootstrap OpenGL context current");
		wglDeleteContext(tempContext);
		return 0;
	}

	// Setup GLEW which loads OGL function pointers
	glewExperimental = GL_TRUE;
	GLenum err = glewInit();
	if (GLEW_OK != err)
	{
		/* Problem: glewInit failed, something is seriously wrong. */
		LOG_ERROR("Glew Init Error %s", glewGetErrorString(err));
		wglMakeCurrent(NULL, NULL);
		wglDeleteContext(tempContext);
		return 0;
	}
	LOG_INFO("Status: Using GLEW %s", glewGetString(GLEW_VERSION));

	if (gl_compat_mode)
	{
		// Deliberately skip the ladder. Keeping the plain context puts us on
		// exactly the route an OpenGL 2.0 machine takes, which is the point of
		// the mode -- asking the driver for a 2.1 context instead would not do
		// it, since ARB_create_context lets drivers return any backwards
		// compatible version for requests below 3.2.
		LOG_INFO("OpenGL 2.0 compatibility mode requested ([main] gl_compat=1)");
	}
	else if (wglewIsSupported("WGL_ARB_create_context") == 1 && wglCreateContextAttribsARB != NULL)
	{
		int count = (int)(sizeof(gl_version_targets) / sizeof(gl_version_targets[0]));

		for (int i = 0; i < count && !hRC; i++)
		{
			HGLRC candidate = create_versioned_context(gl_version_targets[i].major,
				gl_version_targets[i].minor);
			if (!candidate) continue;

			wglMakeCurrent(NULL, NULL);

			// Confirm the new context is current BEFORE destroying the
			// bootstrap one. Deleting first and failing here left the process
			// with no current context at all, turning every later GL call
			// into a silent no-op.
			if (wglMakeCurrent(hDC, candidate))
			{
				hRC = candidate;
				LOG_INFO("Requested an OpenGL %d.%d compatibility context",
					gl_version_targets[i].major, gl_version_targets[i].minor);
			}
			else
			{
				wglDeleteContext(candidate);
				if (!wglMakeCurrent(hDC, tempContext))
				{
					LOG_ERROR("Lost the bootstrap OpenGL context while probing versions");
					wglDeleteContext(tempContext);
					return 0;
				}
			}
		}
	}
	else
	{
		LOG_WARN("WGL_ARB_create_context unavailable; cannot request a 3.3+ context");
	}

	if (hRC)
	{
		wglDeleteContext(tempContext);

		// Re-resolve entry points against the context we ended up with.
		glewExperimental = GL_TRUE;
		err = glewInit();
		if (GLEW_OK != err)
			LOG_WARN("GLEW re-init on the new context reported: %s", glewGetErrorString(err));
	}
	else
	{
		// Either compatibility mode asked for this, or no 3.3+ context was
		// available. Either way the plain context still runs everything except
		// the modern draw path.
		hRC = tempContext;
		if (gl_compat_mode)
			LOG_INFO("Using the plain OpenGL context for compatibility mode");
		else
			LOG_WARN("Falling back to the legacy OpenGL context");
	}

	query_gl_version();
	CheckGLVersionSupport();
	return 1;
}


// Disable OpenGL Context
void DeleteGLContext(void)
{
	wglMakeCurrent(NULL, NULL);
	if (hRC)
	{
		wglDeleteContext(hRC);
		hRC = NULL;
	}
	if (hDC)
	{
		ReleaseDC(win_get_window(), hDC);
		hDC = NULL;
	}
}


void glSwap(void)
{
	if (hDC) SwapBuffers(hDC);
}


void CheckGLVersionSupport(void)
{
	const char *vendor = (const char *)glGetString(GL_VENDOR);
	const char *renderer = (const char *)glGetString(GL_RENDERER);
	const char *version = (const char *)glGetString(GL_VERSION);
	const char *glsl = (const char *)glGetString(GL_SHADING_LANGUAGE_VERSION);

	LOG_INFO("OpenGL %d.%d  (%s)", gl_major, gl_minor, version ? version : "?");
	LOG_INFO("  Vendor:   %s", vendor ? vendor : "?");
	LOG_INFO("  Renderer: %s", renderer ? renderer : "?");
	LOG_INFO("  GLSL:     %s", glsl ? glsl : "?");

	if (gl_compat_mode)
	{
		LOG_INFO("OpenGL 2.0 compatibility mode is on; using fixed-function drawing"
			" even though the driver reports %d.%d. Set [main] gl_compat=0 to"
			" use the OpenGL 3.3 path.", gl_major, gl_minor);
	}
	else if (GLVersionAtLeast(3, 3))
	{
		LOG_INFO("OpenGL 3.3+ available, the modern shader draw path is supported");
	}
	else
	{
		LOG_WARN("OpenGL %d.%d is below 3.3; falling back to fixed-function drawing",
			gl_major, gl_minor);
	}

	if (gl_major < 2)
	{
		MessageBox(NULL,
			L"This program may not work, your supported OpenGL version is less than 2.0",
			L"OpenGL version warning", MB_ICONERROR | MB_OK);
	}
}

int SetSwapMode(int mode)
{
	if (!WGLEW_EXT_swap_control || wglSwapIntervalEXT == NULL)
	{
		LOG_WARN("WGL_EXT_swap_control not supported; swaps stay free-running");
		return 0;
	}

	if (mode == 2)
	{
		// A negative interval is the adaptive request; only valid with the
		// tear-control extension, and some drivers reject it even then.
		if (WGLEW_EXT_swap_control_tear && wglSwapIntervalEXT(-1))
		{
			LOG_INFO("Swap mode: adaptive vsync");
			return 2;
		}
		LOG_WARN("Adaptive vsync unavailable; falling back to vsync");
		mode = 1;
	}

	if (mode == 1)
	{
		if (wglSwapIntervalEXT(1))
		{
			LOG_INFO("Swap mode: vsync");
			return 1;
		}
		LOG_WARN("vsync could not be set; swaps stay free-running");
	}

	if (!wglSwapIntervalEXT(0))
		LOG_WARN("Could not set the swap interval at all (error %lu)", GetLastError());
	LOG_INFO("Swap mode: free-running (no vsync)");
	return 0;
}


GLvoid ReSizeGLScene(GLsizei width, GLsizei height)             // Resize And Initialize The GL Window
{
	if (width <= 0) width = 1;
	if (height <= 0) height = 1;
	glViewport(0, 0, width, height);                    // Reset The Current Viewport
}


// Remembered so window coordinates can be mapped back into design space.
static int viewport_x, viewport_y, viewport_w = 1, viewport_h = 1;

void GetViewportRect(int *x, int *y, int *w, int *h)
{
	if (x) *x = viewport_x;
	if (y) *y = viewport_y;
	if (w) *w = viewport_w;
	if (h) *h = viewport_h;
}

void ViewOrthoScaled(int window_w, int window_h, int design_w, int design_h)
{
	if (window_w <= 0) window_w = 1;
	if (window_h <= 0) window_h = 1;
	if (design_w <= 0) design_w = 1;
	if (design_h <= 0) design_h = 1;

	// Fit the design rect inside the window without distorting it: match the
	// width and letterbox, unless that overflows, in which case match the
	// height and pillarbox.
	double design_aspect = (double)design_w / (double)design_h;

	int vp_w = window_w;
	int vp_h = (int)(window_w / design_aspect + 0.5);

	if (vp_h > window_h)
	{
		vp_h = window_h;
		vp_w = (int)(window_h * design_aspect + 0.5);
	}

	if (vp_w <= 0) vp_w = 1;
	if (vp_h <= 0) vp_h = 1;

	viewport_x = (window_w - vp_w) / 2;
	viewport_y = (window_h - vp_h) / 2;
	viewport_w = vp_w;
	viewport_h = vp_h;

	glViewport(viewport_x, viewport_y, viewport_w, viewport_h);
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glOrtho(0, design_w, 0, design_h, -1, 1);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
}

void ViewOrtho(int width, int height)
{
	// A minimized window reports a zero client area, and glOrtho with
	// left == right is GL_INVALID_VALUE.
	if (width <= 0) width = 1;
	if (height <= 0) height = 1;

	viewport_x = 0;
	viewport_y = 0;
	viewport_w = width;
	viewport_h = height;

	glViewport(0, 0, width, height);             // Set Up An Ortho View
	glMatrixMode(GL_PROJECTION);			  // Select Projection
	glLoadIdentity();						  // Reset The Matrix
	glOrtho(0, width, 0, height, -1, 1);	  // Select Ortho 2D Mode
	glMatrixMode(GL_MODELVIEW);				  // Select Modelview Matrix
	glLoadIdentity();						  // Reset The Matrix
}


void GLPoint(float x, float y)
{
	glDisable(GL_TEXTURE_2D);
	glColor3f(.5f, 1.0f, .5f);
	glPointSize(4.0f);
	glBegin(GL_POINTS);
	glVertex2f(x, y);
	glEnd();
}

void GLLine(float sx, float sy, float ex, float ey)
{
	glBegin(GL_LINES);
	glVertex2f(sx, sy);
	glVertex2f(ex, ey);
	glEnd();
}

void GLRect(int xmin, int xmax, int ymin, int ymax)
{
	glBegin(GL_QUADS);
	glTexCoord2i(0, 1); glVertex2i(xmin, ymin);
	glTexCoord2i(0, 0); glVertex2i(xmin, ymax);
	glTexCoord2i(1, 0); glVertex2i(xmax, ymax);
	glTexCoord2i(1, 1); glVertex2i(xmax, ymin);
	glEnd();
}
