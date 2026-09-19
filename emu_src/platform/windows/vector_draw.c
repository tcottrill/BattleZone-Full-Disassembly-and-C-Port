/* =============================================================================
 * vector_draw.c
 *
 * Shader-based vector beam renderer. See vector_draw.h for the API and
 * ownership rules. Ported to C from AAE's vector_draw.cpp; the shaders and
 * the instance layouts are carried over unchanged, the C++ containers are
 * replaced with growable arrays and a sort+scan pass for join connectivity.
 *
 * Copyright (C) 2025-2026  Tim Cottrill
 * SPDX-License-Identifier: GPL-3.0-or-later
 * ============================================================================= */

#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>   // offsetof
#include "vector_draw.h"
#include "sys_gl.h"
#include "shader_util.h"
#include "ini.h"
#include "log.h"

// ----------------------------- tuning ---------------------------------------

// Defaults for the [vector] config section, written back on first run.
#define DEF_LINEWIDTH        2.0f
#define DEF_GAIN             0
#define DEF_LINE_SMOOTHING   1.5f
#define DEF_CORNER_STRENGTH  1.0f
#define DEF_FIRE_POINT_SIZE  10.0f

// End-cap scale for TRUE line terminations (vertices touched by a single
// segment, e.g. the tips of an "I"): round cap radius as a multiple of the
// beam half-width. ~1.0 matches the legacy GL_POINTS tip length.
#define ENDCAP_STRENGTH      1.0f

static float cfg_linewidth = DEF_LINEWIDTH;
static int   cfg_gain = DEF_GAIN;
static float cfg_smoothing = DEF_LINE_SMOOTHING;
static float cfg_corner = DEF_CORNER_STRENGTH;
static float cfg_fire_size = DEF_FIRE_POINT_SIZE;

static int g_additive = 1;   // 1 = additive color, 0 = sorted B/W alpha-over
static int g_ready = 0;      // set by a fully successful beam_init()

// ----------------------------- batches --------------------------------------

// Instance layouts, uploaded to the VBOs verbatim - the attribute setup in
// beam_init() points at these fields with offsetof, so field order matters.
struct beam_line
{
	float p0x, p0y;
	float p1x, p1y;
	float half;      // half-width, design units
	rgb_t color;     // packed RGBA (a = 0xff); coverage supplies edge alpha
};

struct beam_join
{
	float cx, cy;
	float half;      // disc radius, already scaled by corner/endcap strength
	rgb_t color;
};

struct beam_shot
{
	float x, y;
	float size;      // half-size: the shader extrudes +-size around the center
	rgb_t color;     // unit hue in rgb, linear intensity in alpha
};

// One recorded segment endpoint. Joins are found by sorting these by key and
// scanning equal-key runs: 1 endpoint at a vertex is a true termination, 2+
// is a corner.
struct vert_entry
{
	int64_t key;     // quantized position
	float x, y;
	float half;
	rgb_t color;
};

static struct beam_line *g_lines;   static int g_nlines, g_caplines;
static struct beam_join *g_joins;   static int g_njoins, g_capjoins;
static struct beam_shot *g_shots;   static int g_nshots, g_capshots;
static struct vert_entry *g_verts;  static int g_nverts, g_capverts;

// Grows *arr (element size esz) to hold at least need elements. Returns 0 and
// leaves the array alone if the allocation fails, so the caller can drop the
// element rather than crash.
static int grow(void **arr, int *cap, int need, size_t esz)
{
	void *p;
	int newcap;

	if (need <= *cap) return 1;

	newcap = (*cap < 256) ? 256 : *cap * 2;
	while (newcap < need) newcap *= 2;

	p = realloc(*arr, (size_t)newcap * esz);
	if (!p)
	{
		LOG_ERROR("vector_draw out of memory growing a batch to %d elements", newcap);
		return 0;
	}
	*arr = p;
	*cap = newcap;
	return 1;
}

// ----------------------------- GL objects -----------------------------------

static GLuint prog_line, prog_join, prog_shot;
static GLuint vao_line, vbo_line;
static GLuint vao_join, vbo_join;
static GLuint vao_shot, vbo_shot;

static GLint line_uproj = -1, line_uaa = -1;
static GLint join_uproj = -1, join_uaa = -1, join_ustrength = -1, join_upremult = -1;
static GLint shot_uproj = -1;

// ----------------------------- shaders --------------------------------------

static const char *const vs_line =
"#version 330 core\n"
"layout(location=0) in vec2  inP0;\n"
"layout(location=1) in vec2  inP1;\n"
"layout(location=2) in float inHalf;\n"
"layout(location=3) in vec4  inColor;\n"
"uniform mat4  uProj;\n"
"uniform float uAA;\n"
"out vec2  vLocal;\n"   // x = longitudinal [0..len], y = perpendicular
"out float vLen;\n"
"out float vHalf;\n"
"out vec4  vColor;\n"
"const vec2 kQuad[4] = vec2[](vec2(0,-1), vec2(1,-1), vec2(0,1), vec2(1,1));\n"
"void main() {\n"
"    vec2  d   = inP1 - inP0;\n"
"    float len = length(d);\n"
"    vec2  dir = (len > 0.0001) ? d/len : vec2(1.0,0.0);\n"
"    vec2  nrm = vec2(-dir.y, dir.x);\n"
"    vec2  q   = kQuad[gl_VertexID];\n"
"    float along = q.x * (len + 2.0*uAA) - uAA;\n"   // butt cap + feather past ends
"    float perp  = q.y * (inHalf + uAA);\n"
"    vec2  pos   = inP0 + dir*along + nrm*perp;\n"
"    gl_Position = uProj * vec4(pos, 0.0, 1.0);\n"
"    vLocal = vec2(along, perp);\n"
"    vLen = len; vHalf = inHalf; vColor = inColor;\n"
"}\n";

static const char *const fs_line =
"#version 330 core\n"
"in vec2  vLocal;\n"
"in float vLen;\n"
"in float vHalf;\n"
"in vec4  vColor;\n"
"uniform float uAA;\n"
"out vec4 frag;\n"
"void main() {\n"
// Half-coverage exactly at the geometric edge -> width matches GL_LINES.
"    float covPerp = clamp((vHalf - abs(vLocal.y))/uAA + 0.5, 0.0, 1.0);\n"
"    float covEnd0 = clamp((vLocal.x)/uAA + 0.5, 0.0, 1.0);\n"
"    float covEnd1 = clamp((vLen - vLocal.x)/uAA + 0.5, 0.0, 1.0);\n"
"    float cov = covPerp * covEnd0 * covEnd1;\n"
"    if (cov <= 0.0) discard;\n"
"    frag = vec4(vColor.rgb, vColor.a * cov);\n"   // no pow(): linear coverage
"}\n";

// Round join: a disc centred on the shared vertex; always round, never squared.
static const char *const vs_join =
"#version 330 core\n"
"layout(location=0) in vec2  inCenter;\n"
"layout(location=1) in float inHalf;\n"
"layout(location=2) in vec4  inColor;\n"
"uniform mat4  uProj;\n"
"uniform float uAA;\n"
"uniform float uStrength;\n"
"out vec2  vLocal;\n"
"out float vRad;\n"
"out vec4  vColor;\n"
"const vec2 kQuad[4] = vec2[](vec2(-1,-1), vec2(1,-1), vec2(-1,1), vec2(1,1));\n"
"void main() {\n"
"    float r = inHalf * uStrength;\n"
"    vec2 q = kQuad[gl_VertexID];\n"
"    vec2 ext = q * (r + uAA);\n"
"    gl_Position = uProj * vec4(inCenter + ext, 0.0, 1.0);\n"
"    vLocal = ext; vRad = r; vColor = inColor;\n"
"}\n";

static const char *const fs_join =
"#version 330 core\n"
"in vec2  vLocal;\n"
"in float vRad;\n"
"in vec4  vColor;\n"
"uniform float uAA;\n"
"uniform float uPremult;\n"   // 1 = premultiplied output for the GL_MAX (additive) path
"out vec4 frag;\n"
"void main() {\n"
"    float cov = clamp((vRad - length(vLocal))/uAA + 0.5, 0.0, 1.0);\n"
"    if (cov <= 0.0) discard;\n"
"    if (uPremult > 0.5)\n"
"        frag = vec4(vColor.rgb * cov, cov);\n"        // GL_MAX: fills gaps, never sums over lines
"    else\n"
"        frag = vec4(vColor.rgb, vColor.a * cov);\n"   // straight alpha-over (B/W)
"}\n";

static const char *const vs_shot =
"#version 330 core\n"
"layout(location=0) in vec2  inCenter;\n"
"layout(location=1) in float inSize;\n"
"layout(location=2) in vec4  inColor;\n"
"uniform mat4 uProj;\n"
"out vec2 vUV;\n"
"out vec4 vColor;\n"
"const vec2 kQuad[4] = vec2[](vec2(-1,-1), vec2(1,-1), vec2(-1,1), vec2(1,1));\n"
"void main() {\n"
"    vec2 q = kQuad[gl_VertexID];\n"
"    gl_Position = uProj * vec4(inCenter + q*inSize, 0.0, 1.0);\n"
"    vUV = q*0.5 + 0.5;\n"
"    vColor = inColor;\n"
"}\n";

static const char *const fs_shot =
"#version 330 core\n"
"in vec2 vUV;\n"
"in vec4 vColor;\n"
"out vec4 frag;\n"
"uniform float uCorePower;\n"
"uniform float uBloomPower;\n"
"uniform float uBloomIntensity;\n"
"uniform float uOverdrive;\n"
"void main() {\n"
"    float d = distance(vUV, vec2(0.5));\n"
"    float g = clamp(1.0 - d*2.0, 0.0, 1.0);\n"
"    float profile = pow(g, uCorePower) + pow(g, uBloomPower) * uBloomIntensity;\n"
"    float z = vColor.a;\n"   // clean linear intensity (no gain floor)
// GL_SRC_ALPHA, GL_ONE: the alpha re-multiplies profile, squaring it -> a sharp
// core (not a fuzzy ball); the z factor dims the shot linearly toward nothing.
"    frag = vec4(vColor.rgb * uOverdrive * profile, profile * z);\n"
"}\n";

// ----------------------------- helpers --------------------------------------

static int clip255(int v)
{
	if (v < 0) return 0;
	if (v > 255) return 255;
	return v;
}

// Applies the beam intensity and the configured gain to a color, matching the
// AAE emulator's modulation exactly (including the bitwise AND with the
// intensity, which the DVG sims rely on). Black in = black out.
static rgb_t modulate_color(rgb_t col, int intensity, int gain)
{
	int r, g, b;

	if ((col & 0x00FFFFFFu) == 0) return 0;

	r = clip255(((col >> 0) & 0xFF & intensity) + gain);
	g = clip255(((col >> 8) & 0xFF & intensity) + gain);
	b = clip255(((col >> 16) & 0xFF & intensity) + gain);

	return MAKE_RGBA(r, g, b, 255);
}

static int beam_brightness(rgb_t c)
{
	int r = c & 0xFF, g = (c >> 8) & 0xFF, b = (c >> 16) & 0xFF;
	return (r > g) ? (r > b ? r : b) : (g > b ? g : b);   // max channel
}

static int64_t beam_vkey(float x, float y)
{
	// Quantize to 0.1 design units so coincident endpoints merge despite
	// float jitter in the generator.
	int64_t qx = (int64_t)(x * 10.0f + 0.5f);
	int64_t qy = (int64_t)(y * 10.0f + 0.5f);
	return (qx << 32) ^ (qy & 0xffffffffLL);
}

static void beam_record_vert(int64_t key, float x, float y, rgb_t c, float half)
{
	struct vert_entry *v;

	if (!grow((void **)&g_verts, &g_capverts, g_nverts + 1, sizeof(*g_verts)))
		return;

	v = &g_verts[g_nverts++];
	v->key = key;
	v->x = x;
	v->y = y;
	v->half = half;
	v->color = c;
}

// ----------------------------- lifecycle ------------------------------------

static void release_gl(void)
{
	if (vao_line) { glDeleteVertexArrays(1, &vao_line); vao_line = 0; }
	if (vao_join) { glDeleteVertexArrays(1, &vao_join); vao_join = 0; }
	if (vao_shot) { glDeleteVertexArrays(1, &vao_shot); vao_shot = 0; }
	if (vbo_line) { glDeleteBuffers(1, &vbo_line); vbo_line = 0; }
	if (vbo_join) { glDeleteBuffers(1, &vbo_join); vbo_join = 0; }
	if (vbo_shot) { glDeleteBuffers(1, &vbo_shot); vbo_shot = 0; }
	if (prog_line) { glDeleteProgram(prog_line); prog_line = 0; }
	if (prog_join) { glDeleteProgram(prog_join); prog_join = 0; }
	if (prog_shot) { glDeleteProgram(prog_shot); prog_shot = 0; }
	line_uproj = line_uaa = -1;
	join_uproj = join_uaa = join_ustrength = join_upremult = -1;
	shot_uproj = -1;
}

static GLuint link_prog(const char *vs, const char *fs, const char *label)
{
	return LinkShaderProgram(CompileShader(GL_VERTEX_SHADER, vs, label),
		CompileShader(GL_FRAGMENT_SHADER, fs, label));
}

// Creates one instanced VAO/VBO pair: every attribute advances per instance
// (the 4 strip corners come from gl_VertexID, so there are no per-vertex
// attributes at all). Returns 0 if GL hands back a zero name.
static int make_instanced_vao(GLuint *vao, GLuint *vbo, const char *label)
{
	glGenVertexArrays(1, vao);
	glGenBuffers(1, vbo);
	if (!*vao || !*vbo)
	{
		LOG_ERROR("vector_draw could not allocate a VAO/VBO for %s", label);
		return 0;
	}
	glBindVertexArray(*vao);
	glBindBuffer(GL_ARRAY_BUFFER, *vbo);
	return 1;
}

int beam_init(void)
{
	// Tuning first: it needs no GL, and the write-back makes the keys show up
	// in the config file even if the GL setup below fails.
	cfg_linewidth = get_config_float("vector", "linewidth", DEF_LINEWIDTH);
	cfg_gain = get_config_int("vector", "gain", DEF_GAIN);
	cfg_smoothing = get_config_float("vector", "line_smoothing", DEF_LINE_SMOOTHING);
	cfg_corner = get_config_float("vector", "corner_strength", DEF_CORNER_STRENGTH);
	cfg_fire_size = get_config_float("vector", "fire_point_size", DEF_FIRE_POINT_SIZE);
	set_config_float("vector", "linewidth", cfg_linewidth);
	set_config_int("vector", "gain", cfg_gain);
	set_config_float("vector", "line_smoothing", cfg_smoothing);
	set_config_float("vector", "corner_strength", cfg_corner);
	set_config_float("vector", "fire_point_size", cfg_fire_size);

	if (g_ready) return 1;

	if (!GLModernPathAvailable() || glGenVertexArrays == NULL
		|| glCreateShader == NULL || glVertexAttribDivisor == NULL)
	{
		LOG_ERROR("vector_draw needs the OpenGL 3.3 path; vectors will not be drawn");
		return 0;
	}

	prog_line = link_prog(vs_line, fs_line, "beam_line");
	prog_join = link_prog(vs_join, fs_join, "beam_join");
	prog_shot = link_prog(vs_shot, fs_shot, "beam_shot");
	if (!prog_line || !prog_join || !prog_shot)
	{
		release_gl();
		return 0;
	}

	line_uproj = glGetUniformLocation(prog_line, "uProj");
	line_uaa = glGetUniformLocation(prog_line, "uAA");
	join_uproj = glGetUniformLocation(prog_join, "uProj");
	join_uaa = glGetUniformLocation(prog_join, "uAA");
	join_ustrength = glGetUniformLocation(prog_join, "uStrength");
	join_upremult = glGetUniformLocation(prog_join, "uPremult");
	shot_uproj = glGetUniformLocation(prog_shot, "uProj");

	// These shaders are fixed strings, so a missing uniform means the driver
	// mangled something; drawing with glUniform(-1, ...) silently doing
	// nothing would be far harder to spot than failing here.
	if (line_uproj < 0 || line_uaa < 0 || join_uproj < 0 || join_uaa < 0
		|| join_ustrength < 0 || join_upremult < 0 || shot_uproj < 0)
	{
		LOG_ERROR("vector_draw shader is missing a required uniform");
		release_gl();
		return 0;
	}

	// The shot profile constants never change; set them once.
	glUseProgram(prog_shot);
	glUniform1f(glGetUniformLocation(prog_shot, "uCorePower"), 6.0f);
	glUniform1f(glGetUniformLocation(prog_shot, "uBloomPower"), 2.5f);
	glUniform1f(glGetUniformLocation(prog_shot, "uBloomIntensity"), 0.3f);
	glUniform1f(glGetUniformLocation(prog_shot, "uOverdrive"), 1.5f);
	glUseProgram(0);

	// Lines: one instance per segment.
	if (!make_instanced_vao(&vao_line, &vbo_line, "lines")) { release_gl(); return 0; }
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(struct beam_line), (const void *)offsetof(struct beam_line, p0x));
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(struct beam_line), (const void *)offsetof(struct beam_line, p1x));
	glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(struct beam_line), (const void *)offsetof(struct beam_line, half));
	glVertexAttribPointer(3, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(struct beam_line), (const void *)offsetof(struct beam_line, color));
	glEnableVertexAttribArray(0); glVertexAttribDivisor(0, 1);
	glEnableVertexAttribArray(1); glVertexAttribDivisor(1, 1);
	glEnableVertexAttribArray(2); glVertexAttribDivisor(2, 1);
	glEnableVertexAttribArray(3); glVertexAttribDivisor(3, 1);

	// Joins.
	if (!make_instanced_vao(&vao_join, &vbo_join, "joins")) { release_gl(); return 0; }
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(struct beam_join), (const void *)offsetof(struct beam_join, cx));
	glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, sizeof(struct beam_join), (const void *)offsetof(struct beam_join, half));
	glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(struct beam_join), (const void *)offsetof(struct beam_join, color));
	glEnableVertexAttribArray(0); glVertexAttribDivisor(0, 1);
	glEnableVertexAttribArray(1); glVertexAttribDivisor(1, 1);
	glEnableVertexAttribArray(2); glVertexAttribDivisor(2, 1);

	// Shots.
	if (!make_instanced_vao(&vao_shot, &vbo_shot, "shots")) { release_gl(); return 0; }
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(struct beam_shot), (const void *)offsetof(struct beam_shot, x));
	glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, sizeof(struct beam_shot), (const void *)offsetof(struct beam_shot, size));
	glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(struct beam_shot), (const void *)offsetof(struct beam_shot, color));
	glEnableVertexAttribArray(0); glVertexAttribDivisor(0, 1);
	glEnableVertexAttribArray(1); glVertexAttribDivisor(1, 1);
	glEnableVertexAttribArray(2); glVertexAttribDivisor(2, 1);

	glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);

	// Anything the calls above quietly rejected surfaces here.
	if (glGetError() != GL_NO_ERROR)
	{
		LOG_ERROR("vector_draw setup raised a GL error; vectors will not be drawn");
		release_gl();
		return 0;
	}

	g_ready = 1;
	LOG_INFO("vector_draw ready (linewidth %.2f, smoothing %.2f, gain %d)",
		cfg_linewidth, cfg_smoothing, cfg_gain);
	return 1;
}

void beam_shutdown(void)
{
	release_gl();
	free(g_lines); g_lines = NULL; g_nlines = g_caplines = 0;
	free(g_joins); g_joins = NULL; g_njoins = g_capjoins = 0;
	free(g_shots); g_shots = NULL; g_nshots = g_capshots = 0;
	free(g_verts); g_verts = NULL; g_nverts = g_capverts = 0;
	g_ready = 0;
}

// ----------------------------- settings -------------------------------------

void beam_set_color_mode(int additive) { g_additive = additive ? 1 : 0; }
void beam_set_linewidth(float w) { cfg_linewidth = w; }
void beam_set_gain(int gain) { cfg_gain = gain; }
void beam_set_smoothing(float feather) { cfg_smoothing = feather; }
void beam_set_corner_strength(float s) { cfg_corner = s; }
void beam_set_fire_point_size(float s) { cfg_fire_size = s; }

// ----------------------------- producer -------------------------------------

void beam_add_line_w(float sx, float sy, float ex, float ey, int intensity, rgb_t col,
	float half)
{
	struct beam_line *l;
	int64_t k0, k1;
	rgb_t c = modulate_color(col, intensity, cfg_gain);

	// Invisible (pen-up move): draw nothing, contribute no join vertex.
	if ((c & 0x00FFFFFFu) == 0) return;

	if (!grow((void **)&g_lines, &g_caplines, g_nlines + 1, sizeof(*g_lines)))
		return;

	l = &g_lines[g_nlines++];
	l->p0x = sx; l->p0y = sy;
	l->p1x = ex; l->p1y = ey;
	l->half = half;
	l->color = c;

	// Record both endpoints; a vertex shared by 2+ segments becomes a join.
	k0 = beam_vkey(sx, sy);
	k1 = beam_vkey(ex, ey);
	beam_record_vert(k0, sx, sy, c, half);
	if (k1 != k0)
		beam_record_vert(k1, ex, ey, c, half);
}

void beam_add_line(float sx, float sy, float ex, float ey, int intensity, rgb_t col)
{
	beam_add_line_w(sx, sy, ex, ey, intensity, col, cfg_linewidth * 0.5f);
}

void beam_add_shot(float ex, float ey, int intensity, rgb_t col)
{
	struct beam_shot *s;
	// Separate hue from brightness: normalize the color to a unit hue and pass
	// the intensity straight through (in alpha) so shot brightness scales
	// linearly, unflattened by the line gain.
	int r = col & 0xFF, g = (col >> 8) & 0xFF, b = (col >> 16) & 0xFF;
	int mx = (r > g) ? (r > b ? r : b) : (g > b ? g : b);
	int z = clip255(intensity);

	if (mx <= 0 || z <= 0) return;
	r = (r * 255) / mx;
	g = (g * 255) / mx;
	b = (b * 255) / mx;

	if (!grow((void **)&g_shots, &g_capshots, g_nshots + 1, sizeof(*g_shots)))
		return;

	s = &g_shots[g_nshots++];
	s->x = ex;
	s->y = ey;
	s->size = cfg_fire_size;
	s->color = MAKE_RGBA(r, g, b, z);
}

void beam_clear(void)
{
	// Counts only; capacity is kept for the next frame.
	g_nlines = 0;
	g_njoins = 0;
	g_nshots = 0;
	g_nverts = 0;
}

// ----------------------------- draw -----------------------------------------

static int cmp_vert_key(const void *a, const void *b)
{
	const struct vert_entry *va = a, *vb = b;
	if (va->key < vb->key) return -1;
	if (va->key > vb->key) return 1;
	return 0;
}

// Painter's sort for the B/W path: darkest first, so brighter beams occlude
// darker ones under alpha-over.
static int cmp_line_color(const void *a, const void *b)
{
	rgb_t ca = ((const struct beam_line *)a)->color;
	rgb_t cb = ((const struct beam_line *)b)->color;
	return (ca < cb) ? -1 : (ca > cb) ? 1 : 0;
}

static int cmp_join_color(const void *a, const void *b)
{
	rgb_t ca = ((const struct beam_join *)a)->color;
	rgb_t cb = ((const struct beam_join *)b)->color;
	return (ca < cb) ? -1 : (ca > cb) ? 1 : 0;
}

// Rebuilds the join/cap discs from the recorded endpoints: sort by quantized
// key, then each run of equal keys is one vertex. A run of length 1 is a true
// line termination (endcap radius), 2+ a corner (corner strength radius); the
// disc takes the brightest color and widest half-width seen in the run.
// Rebuilt every draw so config changes take effect immediately.
static void build_joins(void)
{
	int i;

	g_njoins = 0;
	if (g_nverts == 0) return;

	qsort(g_verts, (size_t)g_nverts, sizeof(*g_verts), cmp_vert_key);

	i = 0;
	while (i < g_nverts)
	{
		const struct vert_entry *first = &g_verts[i];
		rgb_t color = first->color;
		float half = first->half;
		float r;
		int count = 1;

		while (i + count < g_nverts && g_verts[i + count].key == first->key)
		{
			const struct vert_entry *v = &g_verts[i + count];
			if (beam_brightness(v->color) > beam_brightness(color)) color = v->color;
			if (v->half > half) half = v->half;
			count++;
		}
		i += count;

		r = half * ((count >= 2) ? cfg_corner : ENDCAP_STRENGTH);
		if (r > 0.01f && grow((void **)&g_joins, &g_capjoins, g_njoins + 1, sizeof(*g_joins)))
		{
			struct beam_join *j = &g_joins[g_njoins++];
			j->cx = first->x;
			j->cy = first->y;
			j->half = r;
			j->color = color;
		}
	}
}

void beam_draw_all(const float *proj16)
{
	if (!g_ready || !proj16) return;

	build_joins();

	if (g_nlines == 0 && g_njoins == 0 && g_nshots == 0) return;

	glEnable(GL_BLEND);

	if (!g_additive)
	{
		qsort(g_lines, (size_t)g_nlines, sizeof(*g_lines), cmp_line_color);
		qsort(g_joins, (size_t)g_njoins, sizeof(*g_joins), cmp_join_color);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	}
	else
	{
		glBlendFunc(GL_SRC_ALPHA, GL_ONE);   // color: additive
	}

	// Lines.
	if (g_nlines > 0)
	{
		glUseProgram(prog_line);
		glUniformMatrix4fv(line_uproj, 1, GL_FALSE, proj16);
		glUniform1f(line_uaa, cfg_smoothing);
		glBindBuffer(GL_ARRAY_BUFFER, vbo_line);
		glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)g_nlines * sizeof(*g_lines), g_lines, GL_STREAM_DRAW);
		glBindVertexArray(vao_line);
		glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, g_nlines);
	}

	// Corner joins + end-caps. Under additive (color) they draw with GL_MAX
	// and premultiplied coverage so they FILL gaps at the line's own
	// brightness but never sum on top of the lines (which otherwise makes
	// corners brighter than the beam). Under B/W they use the same sorted
	// alpha-over as the lines, where same-color overlap already cannot double.
	if (g_njoins > 0)
	{
		glUseProgram(prog_join);
		glUniformMatrix4fv(join_uproj, 1, GL_FALSE, proj16);
		glUniform1f(join_uaa, cfg_smoothing);
		glUniform1f(join_ustrength, 1.0f);   // radius already baked per-disc
		glUniform1f(join_upremult, g_additive ? 1.0f : 0.0f);
		if (g_additive) glBlendEquation(GL_MAX);
		glBindBuffer(GL_ARRAY_BUFFER, vbo_join);
		glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)g_njoins * sizeof(*g_joins), g_joins, GL_STREAM_DRAW);
		glBindVertexArray(vao_join);
		glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, g_njoins);
		if (g_additive) glBlendEquation(GL_FUNC_ADD);
	}

	// Shots: procedural radial core + halo, always additive.
	if (g_nshots > 0)
	{
		glUseProgram(prog_shot);
		glUniformMatrix4fv(shot_uproj, 1, GL_FALSE, proj16);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE);
		glBindBuffer(GL_ARRAY_BUFFER, vbo_shot);
		glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)g_nshots * sizeof(*g_shots), g_shots, GL_STREAM_DRAW);
		glBindVertexArray(vao_shot);
		glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, g_nshots);
	}

	glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glUseProgram(0);

	// The rest of the frame (textures, font) expects standard alpha blending.
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}
