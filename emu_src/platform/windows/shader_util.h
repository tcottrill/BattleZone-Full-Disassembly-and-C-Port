/* =============================================================================
 * shader_util.h
 *
 * General-purpose OpenGL shader utility for compiling and linking GLSL
 * shaders. Header-only; ported to C from Game Engine Alpha / AAE.
 *
 *   - Compiling individual shaders (vertex, fragment, etc.)
 *   - Linking shader programs
 *   - Logging success and failure via LOG_INFO and LOG_ERROR
 *
 * Usage:
 *   GLuint vs   = CompileShader(GL_VERTEX_SHADER,   vs_src, "draw_tex VS");
 *   GLuint fs   = CompileShader(GL_FRAGMENT_SHADER, fs_src, "draw_tex FS");
 *   GLuint prog = LinkShaderProgram(vs, fs);
 *
 * LinkShaderProgram consumes both shader objects -- they are deleted whether
 * the link succeeds or not, so callers never release them.
 *
 * Both functions return 0 on failure, after logging the driver's info log and
 * releasing anything they allocated. Callers are expected to check: draw code
 * uses that to fall back to its fixed-function path, so returning a live-
 * looking handle for a shader that never compiled would strand it on a draw
 * path that silently renders nothing.
 *
 * Copyright (C) 2022-2026  Tim Cottrill
 * SPDX-License-Identifier: GPL-3.0-or-later
 * ============================================================================= */

#pragma once

#ifndef SHADER_UTIL_H
#define SHADER_UTIL_H

#include "sys_gl.h"
#include "log.h"

/* -----------------------------------------------------------------------------
 * Compiles a GLSL shader and logs success or failure.
 * 'label' appears in the log and may be NULL.
 * -------------------------------------------------------------------------- */
static inline GLuint CompileShader(GLenum type, const char *src, const char *label)
{
	GLuint shader;
	GLint success = 0;

	if (!label) label = "unnamed";

	shader = glCreateShader(type);
	if (!shader)
	{
		/* 0 also comes back when there is no current context, which is worth
		   telling apart from a compile error. */
		LOG_ERROR("glCreateShader failed (%s)", label);
		return 0;
	}

	glShaderSource(shader, 1, &src, NULL);
	glCompileShader(shader);
	glGetShaderiv(shader, GL_COMPILE_STATUS, &success);

	if (!success)
	{
		char info[1024];
		glGetShaderInfoLog(shader, sizeof(info), NULL, info);
		LOG_ERROR("CompileShader FAILED (%s):\n%s", label, info);
		glDeleteShader(shader);
		return 0;
	}

	LOG_INFO("CompileShader OK (%s)", label);
	return shader;
}

/* -----------------------------------------------------------------------------
 * Links a GLSL program from a vertex and a fragment shader, then deletes both
 * shader objects regardless of outcome.
 * -------------------------------------------------------------------------- */
static inline GLuint LinkShaderProgram(GLuint vertexShader, GLuint fragmentShader)
{
	GLuint program;
	GLint success = 0;

	if (!vertexShader || !fragmentShader)
	{
		/* One stage failed to compile. Release whichever one did not, so a
		   failed compile does not leak the good half. */
		if (vertexShader)   glDeleteShader(vertexShader);
		if (fragmentShader) glDeleteShader(fragmentShader);
		return 0;
	}

	program = glCreateProgram();
	if (!program)
	{
		LOG_ERROR("glCreateProgram failed");
		glDeleteShader(vertexShader);
		glDeleteShader(fragmentShader);
		return 0;
	}

	glAttachShader(program, vertexShader);
	glAttachShader(program, fragmentShader);
	glLinkProgram(program);
	glGetProgramiv(program, GL_LINK_STATUS, &success);

	/* Detach before deleting so the objects are released now rather than
	   lingering until the program itself is deleted. */
	glDetachShader(program, vertexShader);
	glDetachShader(program, fragmentShader);
	glDeleteShader(vertexShader);
	glDeleteShader(fragmentShader);

	if (!success)
	{
		char info[1024];
		glGetProgramInfoLog(program, sizeof(info), NULL, info);
		LOG_ERROR("LinkShaderProgram FAILED:\n%s", info);
		glDeleteProgram(program);
		return 0;
	}

	LOG_INFO("LinkShaderProgram OK");
	return program;
}

#endif /* SHADER_UTIL_H */
