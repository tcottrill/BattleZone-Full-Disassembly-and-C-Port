#pragma once

#ifndef MAT4_H
#define MAT4_H

// Column-major 4x4 matrices, laid out the way glUniformMatrix4fv expects with
// transpose = GL_FALSE. Core-profile shaders have no fixed-function matrix
// stack, so the projection has to be built here and passed as a uniform.

#ifdef __cplusplus
extern "C" {
#endif

// Builds an orthographic projection into out[16].
void mat4_ortho(float *out, float left, float right, float bottom, float top,
	float znear, float zfar);

#ifdef __cplusplus
}
#endif

#endif
