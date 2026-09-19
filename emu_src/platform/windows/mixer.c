/* =============================================================================
 * mixer.c -- Sample playback for Windows 11 / XAudio2 2.9
 *
 * One XAudio2 source voice per channel. XAudio2 mixes, resamples and pitch
 * shifts for us, so there is no software mix loop, no worker thread and no
 * per-frame pump. Samples are handed to the API exactly as they came off
 * disk: 8- or 16-bit PCM, mono or stereo, at whatever rate they were authored.
 *
 * XAudio2Create is an inline LoadLibrary shim on Windows 10 and later, so
 * there is nothing to link for XAudio2 itself -- only ole32 for COM startup,
 * which CreateMasteringVoice requires (it returns CO_E_NOTINITIALIZED without
 * it).
 *
 * Copyright (C) 2022-2026  Tim Cottrill
 * SPDX-License-Identifier: GPL-3.0-or-later
 * ============================================================================= */

#include <windows.h>
#include <xaudio2.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "mixer.h"
#include "fileio.h"
#include "log.h"

#pragma comment(lib, "ole32.lib")

/* Every XAudio2 control call returns an HRESULT, including the ones whose
   result we do not otherwise branch on. Discarding them hides driver-level
   failures -- a voice that silently refuses to change volume looks like a bug
   in the game -- so each one goes through here and lands in the log. */
#define XA2_CHECK(expr, what)                                            \
	do {                                                                 \
		HRESULT _xahr = (expr);                                          \
		if (FAILED(_xahr))                                               \
			LOG_ERROR("%s failed: 0x%08lX", (what), (unsigned long)_xahr); \
	} while (0)

/* Headroom for sample_set_freq. Costs a little memory per voice, so it is not
   set higher than needed -- 8x is what the Star Castle drone wants. */
#define MIXER_MAX_FREQ_RATIO 8.0f

/* The mastering voice is pinned to stereo rather than taking the device
   default. Panning writes a source x 2 output matrix, and that is only
   honestly true if the destination really has two channels. Windows upmixes
   to 5.1 / 7.1 downstream. */
#define MIXER_OUT_CHANNELS 2

typedef struct SAMPLE
{
	WAVEFORMATEX   fx;      /* native format, straight from the file */
	unsigned char *data;    /* owned PCM, exactly as XAudio2 wants it */
	uint32_t       size;    /* bytes */
	char           name[64];
	int            in_use;
} SAMPLE;

typedef struct CHANNEL
{
	IXAudio2SourceVoice *voice;
	WAVEFORMATEX         voice_fx;  /* format the voice was created with */
	int sample_num;                 /* -1 when nothing is loaded  */
	int base_freq;                  /* sample's native rate, for the ratio */
	int freq;                       /* current, in Hz */
	int volume;                     /* 0..255, as set */
	int pan;                        /* 0..255, 128 = centre */
	/* Set by sample_start, cleared by sample_stop. XAudio2 keeps BuffersQueued
	   on its own audio thread and only catches up about one engine pass later
	   (~10ms), so a caller that stops a channel and immediately asks whether
	   it is playing would otherwise still be told yes. */
	int active;
} CHANNEL;

static IXAudio2               *g_xa2;
static IXAudio2MasteringVoice *g_master;
static CHANNEL                 g_channel[MIXER_MAX_CHANNELS];
static SAMPLE                  g_sample[MIXER_MAX_SAMPLES];

/* The user's setting, kept exactly as given. Pause is a separate duck rather
   than a second stored volume, so the setting can be changed while paused --
   a volume slider in a pause menu is the obvious case -- and takes effect on
   restore. */
static int g_master_percent = MIXER_DEFAULT_MASTER_VOLUME;
static int g_paused;
static int g_com_init;

static void master_apply(void);

/* -----------------------------------------------------------------------------
 * Volume
 *
 * One curve, used by every volume path. Byte and percent are both normalised
 * to 0..1 and fed through it, so 0..255 and 0..100 callers agree instead of
 * the byte path quantising itself down to 101 steps on the way in.
 *
 *   0.00 -> silence      0.05 -> -12 dB      0.50 -> ~-6 dB      1.00 -> 0 dB
 * -------------------------------------------------------------------------- */
static float volume_norm_to_linear(float x)
{
	float dB, y;

	if (x <= 0.0f) return 0.0f;   /* exact silence, not -80 dB */
	if (x >= 1.0f) return 1.0f;

	if (x <= 0.05f) {
		/* Quadratic in dB across the bottom, which makes the very quiet end
		   controllable instead of collapsing to nothing in two pixels. */
		y  = x / 0.05f;
		dB = -80.0f + 68.0f * (y * y);
	} else {
		y  = (x - 0.05f) / 0.95f;
		dB = -12.0f + 12.0f * y;
	}
	return powf(10.0f, dB / 20.0f);
}

static float volume_byte_to_linear(int vol255)
{
	if (vol255 < 0)   vol255 = 0;
	if (vol255 > 255) vol255 = 255;
	return volume_norm_to_linear((float)vol255 / 255.0f);
}

static float volume_percent_to_linear(int percent)
{
	if (percent < 0)   percent = 0;
	if (percent > 100) percent = 100;
	return volume_norm_to_linear((float)percent / 100.0f);
}

int mixer_percent_to_byte(int percent)
{
	if (percent < 0)   percent = 0;
	if (percent > 100) percent = 100;
	return (percent * 255 + 50) / 100;
}

int mixer_byte_to_percent(int vol255)
{
	if (vol255 < 0)   vol255 = 0;
	if (vol255 > 255) vol255 = 255;
	return (vol255 * 100 + 127) / 255;
}

/* -----------------------------------------------------------------------------
 * Panning
 *
 * Linear balance: centre leaves both sides at unity and each edge fades the
 * opposite channel out. Constant-power would avoid the mild bulge through the
 * middle of a sweep, but it also drops every centred sound by 3 dB, and in a
 * game nearly everything is centred.
 * -------------------------------------------------------------------------- */
static void pan_gains(int pan, float *gainL, float *gainR)
{
	float p;

	if (pan < 0)   pan = 0;
	if (pan > 255) pan = 255;
	p = (float)pan / 255.0f;

	if (p <= 0.5f) { *gainL = 1.0f;              *gainR = p * 2.0f; }
	else           { *gainL = (1.0f - p) * 2.0f; *gainR = 1.0f;     }
}

static void channel_apply_pan(CHANNEL *ch)
{
	float gainL, gainR, matrix[4];

	if (!ch->voice) return;
	pan_gains(ch->pan, &gainL, &gainR);

	if (ch->voice_fx.nChannels == 1) {
		matrix[0] = gainL;
		matrix[1] = gainR;
		XA2_CHECK(IXAudio2SourceVoice_SetOutputMatrix(ch->voice, NULL, 1,
		              MIXER_OUT_CHANNELS, matrix, XAUDIO2_COMMIT_NOW),
		          "SetOutputMatrix (mono pan)");
	} else {
		/* Stereo balance: attenuate each side, never cross-feed. */
		matrix[0] = gainL; matrix[1] = 0.0f;
		matrix[2] = 0.0f;  matrix[3] = gainR;
		XA2_CHECK(IXAudio2SourceVoice_SetOutputMatrix(ch->voice, NULL, 2,
		              MIXER_OUT_CHANNELS, matrix, XAUDIO2_COMMIT_NOW),
		          "SetOutputMatrix (stereo balance)");
	}
}

/* -----------------------------------------------------------------------------
 * WAV parsing
 * -------------------------------------------------------------------------- */

/* Walks the RIFF chunk list rather than assuming fmt and data sit at fixed
   offsets, so LIST/INFO/fact chunks written by editors are skipped harmlessly.
   Returns 0 on success. */
static int wav_parse(const unsigned char *buf, size_t size, SAMPLE *s)
{
	size_t pos = 12;
	int have_fmt = 0;

	if (size < 12 || memcmp(buf, "RIFF", 4) != 0 || memcmp(buf + 8, "WAVE", 4) != 0) {
		LOG_ERROR("Not a RIFF/WAVE file");
		return -1;
	}

	while (pos + 8 <= size) {
		char     id[4];
		uint32_t csize;

		memcpy(id, buf + pos, 4);
		memcpy(&csize, buf + pos + 4, sizeof csize);
		pos += 8;

		/* pos <= size - 8 here, so this cannot wrap. */
		if (csize > size - pos) {
			LOG_ERROR("WAV chunk '%.4s' claims %u bytes but only %zu remain",
			          id, csize, size - pos);
			return -1;
		}

		if (memcmp(id, "fmt ", 4) == 0) {
			if (csize < 16) {
				LOG_ERROR("WAV fmt chunk is only %u bytes", csize);
				return -1;
			}
			memcpy(&s->fx.wFormatTag,      buf + pos +  0, 2);
			memcpy(&s->fx.nChannels,       buf + pos +  2, 2);
			memcpy(&s->fx.nSamplesPerSec,  buf + pos +  4, 4);
			memcpy(&s->fx.nAvgBytesPerSec, buf + pos +  8, 4);
			memcpy(&s->fx.nBlockAlign,     buf + pos + 12, 2);
			memcpy(&s->fx.wBitsPerSample,  buf + pos + 14, 2);
			s->fx.cbSize = 0;
			have_fmt = 1;
		}
		else if (memcmp(id, "data", 4) == 0) {
			if (!have_fmt) {
				LOG_ERROR("WAV data chunk precedes fmt");
				return -1;
			}
			if (csize == 0) {
				LOG_ERROR("WAV data chunk is empty");
				return -1;
			}

			/* Editors often write WAVE_FORMAT_EXTENSIBLE for plain PCM. The
			   bit depth check below is what actually rejects float and 24-bit,
			   so it is safe to treat this as PCM. */
			if (s->fx.wFormatTag == WAVE_FORMAT_EXTENSIBLE)
				s->fx.wFormatTag = WAVE_FORMAT_PCM;

			if (s->fx.wFormatTag != WAVE_FORMAT_PCM) {
				LOG_ERROR("WAV is not PCM (format tag %u)", s->fx.wFormatTag);
				return -1;
			}
			if (s->fx.nChannels != 1 && s->fx.nChannels != 2) {
				LOG_ERROR("WAV has %u channels; only mono and stereo are supported",
				          s->fx.nChannels);
				return -1;
			}
			/* 24- and 32-bit pass CreateSourceVoice and only fail later at
			   SubmitSourceBuffer, so they have to be caught here. */
			if (s->fx.wBitsPerSample != 8 && s->fx.wBitsPerSample != 16) {
				LOG_ERROR("WAV is %u-bit; only 8 and 16 are supported",
				          s->fx.wBitsPerSample);
				return -1;
			}
			if (s->fx.nSamplesPerSec == 0) {
				LOG_ERROR("WAV sample rate is zero");
				return -1;
			}

			/* Derive these rather than trusting the file. XAudio2 validates
			   them and rejects the voice if they disagree with the rest. */
			s->fx.nBlockAlign     = (WORD)(s->fx.nChannels * s->fx.wBitsPerSample / 8);
			s->fx.nAvgBytesPerSec = s->fx.nSamplesPerSec * s->fx.nBlockAlign;

			s->data = (unsigned char *)malloc(csize);
			if (!s->data) {
				LOG_ERROR("Out of memory for %u bytes of sample data", csize);
				return -1;
			}
			memcpy(s->data, buf + pos, csize);
			s->size = csize;
			return 0;
		}

		pos += csize;
		if (pos & 1) pos++;   /* RIFF pads odd-sized chunks to even */
	}

	LOG_ERROR("WAV has no data chunk");
	return -1;
}

/* Filename with directories and extension stripped. */
static void sample_name_from_path(const char *path, char *out, size_t outsz)
{
	const char *base = path;
	const char *p;
	size_t len;

	for (p = path; *p; p++)
		if (*p == '\\' || *p == '/') base = p + 1;

	len = strlen(base);
	for (p = base + len; p > base; p--)
		if (*(p - 1) == '.') { len = (size_t)(p - 1 - base); break; }

	if (len >= outsz) len = outsz - 1;
	memcpy(out, base, len);
	out[len] = '\0';
}

/* -----------------------------------------------------------------------------
 * Sample loading
 * -------------------------------------------------------------------------- */
int load_sample(const char *archname, const char *filename)
{
	unsigned char *filebuf;
	size_t         filesize;
	int            num = -1;
	int            i;

	if (!filename) return -1;

	for (i = 0; i < MIXER_MAX_SAMPLES; i++)
		if (!g_sample[i].in_use) { num = i; break; }

	if (num < 0) {
		LOG_ERROR("No free sample slots (max %d)", MIXER_MAX_SAMPLES);
		return -1;
	}

	if (archname) {
		filebuf  = loadGenericZip(archname, filename);
		filesize = getlastZsize();
	} else {
		filebuf  = load_file(filename);
		filesize = (size_t)getLastFileSize();
	}

	if (!filebuf) {
		LOG_ERROR("Could not read %s%s%s", archname ? archname : "",
		          archname ? " : " : "", filename);
		return -1;
	}

	memset(&g_sample[num], 0, sizeof g_sample[num]);

	if (wav_parse(filebuf, filesize, &g_sample[num]) != 0) {
		LOG_ERROR("Failed to load sample %s", filename);
		free(filebuf);
		memset(&g_sample[num], 0, sizeof g_sample[num]);
		return -1;
	}
	free(filebuf);

	sample_name_from_path(filename, g_sample[num].name, sizeof g_sample[num].name);
	g_sample[num].in_use = 1;

	LOG_INFO("Loaded sample #%d '%s': %u-bit %u-ch @ %u Hz, %u bytes",
	         num, g_sample[num].name, g_sample[num].fx.wBitsPerSample,
	         g_sample[num].fx.nChannels, g_sample[num].fx.nSamplesPerSec,
	         g_sample[num].size);

	return num;
}

/* -----------------------------------------------------------------------------
 * Channel plumbing
 * -------------------------------------------------------------------------- */
static CHANNEL *chan(int chanid)
{
	if (!g_xa2 || chanid < 0 || chanid >= MIXER_MAX_CHANNELS) return NULL;
	return &g_channel[chanid];
}

static SAMPLE *samp(int samplenum)
{
	if (samplenum < 0 || samplenum >= MIXER_MAX_SAMPLES) return NULL;
	if (!g_sample[samplenum].in_use) return NULL;
	return &g_sample[samplenum];
}

static void channel_destroy_voice(CHANNEL *ch)
{
	ch->active = 0;
	if (!ch->voice) return;
	XA2_CHECK(IXAudio2SourceVoice_Stop(ch->voice, 0, XAUDIO2_COMMIT_NOW),
	          "Stop (destroying voice)");
	XA2_CHECK(IXAudio2SourceVoice_FlushSourceBuffers(ch->voice),
	          "FlushSourceBuffers (destroying voice)");
	IXAudio2SourceVoice_DestroyVoice(ch->voice);
	ch->voice = NULL;
	memset(&ch->voice_fx, 0, sizeof ch->voice_fx);
}

/* Voice creation is not cheap and a game fires the same handful of sounds over
   and over, so an existing voice is reused whenever the format still matches.
   Returns 0 on success. */
static int channel_prepare_voice(CHANNEL *ch, const SAMPLE *s)
{
	HRESULT hr;

	if (ch->voice) {
		if (ch->voice_fx.nChannels      == s->fx.nChannels &&
		    ch->voice_fx.nSamplesPerSec == s->fx.nSamplesPerSec &&
		    ch->voice_fx.wBitsPerSample == s->fx.wBitsPerSample) {
			XA2_CHECK(IXAudio2SourceVoice_Stop(ch->voice, 0, XAUDIO2_COMMIT_NOW),
			          "Stop (reusing voice)");
			XA2_CHECK(IXAudio2SourceVoice_FlushSourceBuffers(ch->voice),
			          "FlushSourceBuffers (reusing voice)");
			return 0;
		}
		channel_destroy_voice(ch);
	}

	hr = IXAudio2_CreateSourceVoice(g_xa2, &ch->voice, &s->fx, 0,
	                                MIXER_MAX_FREQ_RATIO, NULL, NULL, NULL);
	if (FAILED(hr)) {
		LOG_ERROR("CreateSourceVoice failed for sample '%s': 0x%08lX",
		          s->name, (unsigned long)hr);
		ch->voice = NULL;
		return -1;
	}
	ch->voice_fx = s->fx;
	return 0;
}

/* -----------------------------------------------------------------------------
 * Lifecycle
 * -------------------------------------------------------------------------- */
int mixer_init(void)
{
	HRESULT hr;
	int i;

	if (g_xa2) return 0;

	/* CreateMasteringVoice returns CO_E_NOTINITIALIZED without this. S_FALSE
	   means COM was already up on this thread, but the reference is ours
	   either way and has to be released in mixer_end. */
	hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
	if (hr == S_OK || hr == S_FALSE) {
		g_com_init = 1;
	} else if (hr == RPC_E_CHANGED_MODE) {
		/* Something already put this thread into an apartment-threaded model.
		   XAudio2 still works, but we hold no reference, so we must not
		   uninitialize on the way out. */
		LOG_WARN("COM already initialized apartment-threaded; continuing without"
		         " our own reference");
	} else {
		LOG_ERROR("CoInitializeEx failed: 0x%08lX", (unsigned long)hr);
	}

	hr = XAudio2Create(&g_xa2, 0, XAUDIO2_DEFAULT_PROCESSOR);
	if (FAILED(hr)) {
		LOG_ERROR("XAudio2Create failed: 0x%08lX", (unsigned long)hr);
		g_xa2 = NULL;
		if (g_com_init) { CoUninitialize(); g_com_init = 0; }
		return -1;
	}

	hr = IXAudio2_CreateMasteringVoice(g_xa2, &g_master, MIXER_OUT_CHANNELS,
	                                   XAUDIO2_DEFAULT_SAMPLERATE, 0, NULL, NULL,
	                                   AudioCategory_GameEffects);
	if (FAILED(hr)) {
		LOG_ERROR("CreateMasteringVoice failed: 0x%08lX", (unsigned long)hr);
		IXAudio2_Release(g_xa2);
		g_xa2 = NULL;
		if (g_com_init) { CoUninitialize(); g_com_init = 0; }
		return -1;
	}

	for (i = 0; i < MIXER_MAX_CHANNELS; i++) {
		memset(&g_channel[i], 0, sizeof g_channel[i]);
		g_channel[i].sample_num = -1;
		g_channel[i].volume     = 255;
		g_channel[i].pan        = 128;
	}

	g_master_percent = MIXER_DEFAULT_MASTER_VOLUME;
	g_paused         = 0;
	master_apply();

	LOG_INFO("Mixer init: XAudio2 2.9, %d-channel mastering voice, %d channels",
	         MIXER_OUT_CHANNELS, MIXER_MAX_CHANNELS);
	return 0;
}

void mixer_end(void)
{
	int i;

	if (!g_xa2) return;

	/* Belt and braces: a caller that forgot plat_audio_close (or never
	   had one to call) should not leak the stream voice here. */
	stream_close();

	/* Voices first -- they hold pointers into sample data. */
	for (i = 0; i < MIXER_MAX_CHANNELS; i++) {
		channel_destroy_voice(&g_channel[i]);
		g_channel[i].sample_num = -1;
	}

	for (i = 0; i < MIXER_MAX_SAMPLES; i++) {
		if (!g_sample[i].in_use) continue;
		free(g_sample[i].data);
		memset(&g_sample[i], 0, sizeof g_sample[i]);
	}

	if (g_master) { IXAudio2MasteringVoice_DestroyVoice(g_master); g_master = NULL; }
	IXAudio2_Release(g_xa2);
	g_xa2 = NULL;

	if (g_com_init) { CoUninitialize(); g_com_init = 0; }
	LOG_INFO("Mixer shut down");
}

void sample_remove(int samplenum)
{
	SAMPLE *s = samp(samplenum);
	int i;

	if (!s) return;

	/* Stop anything playing it before the memory goes away. */
	for (i = 0; i < MIXER_MAX_CHANNELS; i++) {
		if (g_channel[i].sample_num != samplenum) continue;
		channel_destroy_voice(&g_channel[i]);
		g_channel[i].sample_num = -1;
	}

	free(s->data);
	memset(s, 0, sizeof *s);
}

/* -----------------------------------------------------------------------------
 * Playback
 * -------------------------------------------------------------------------- */
void sample_start(int chanid, int samplenum, int loop)
{
	CHANNEL       *ch = chan(chanid);
	SAMPLE        *s  = samp(samplenum);
	XAUDIO2_BUFFER buf;
	HRESULT        hr;

	if (!ch) {
		LOG_ERROR("sample_start: bad channel %d", chanid);
		return;
	}
	if (!s) {
		LOG_ERROR("sample_start: sample %d is not loaded", samplenum);
		return;
	}
	ch->active = 0;
	if (channel_prepare_voice(ch, s) != 0) return;

	ch->sample_num = samplenum;
	ch->base_freq  = (int)s->fx.nSamplesPerSec;
	ch->freq       = ch->base_freq;
	ch->volume     = 255;
	ch->pan        = 128;

	memset(&buf, 0, sizeof buf);
	buf.Flags      = XAUDIO2_END_OF_STREAM;
	buf.AudioBytes = s->size;
	buf.pAudioData = s->data;
	buf.LoopCount  = loop ? XAUDIO2_LOOP_INFINITE : 0;

	hr = IXAudio2SourceVoice_SubmitSourceBuffer(ch->voice, &buf, NULL);
	if (FAILED(hr)) {
		LOG_ERROR("SubmitSourceBuffer failed for '%s': 0x%08lX",
		          s->name, (unsigned long)hr);
		return;
	}

	/* A reused voice can still be carrying the last sample's settings. */
	XA2_CHECK(IXAudio2SourceVoice_SetVolume(ch->voice,
	              volume_byte_to_linear(ch->volume), XAUDIO2_COMMIT_NOW),
	          "SetVolume (sample_start)");
	XA2_CHECK(IXAudio2SourceVoice_SetFrequencyRatio(ch->voice, 1.0f,
	              XAUDIO2_COMMIT_NOW),
	          "SetFrequencyRatio (sample_start)");
	channel_apply_pan(ch);

	hr = IXAudio2SourceVoice_Start(ch->voice, 0, XAUDIO2_COMMIT_NOW);
	if (FAILED(hr)) {
		LOG_ERROR("Voice start failed for '%s': 0x%08lX", s->name, (unsigned long)hr);
		return;
	}
	ch->active = 1;
}

void sample_stop(int chanid)
{
	CHANNEL *ch = chan(chanid);

	if (!ch) return;
	ch->active = 0;
	if (!ch->voice) return;
	XA2_CHECK(IXAudio2SourceVoice_Stop(ch->voice, 0, XAUDIO2_COMMIT_NOW),
	          "Stop (sample_stop)");
	XA2_CHECK(IXAudio2SourceVoice_FlushSourceBuffers(ch->voice),
	          "FlushSourceBuffers (sample_stop)");
}

/* Leaves the loop region so the current pass finishes and the voice runs dry.
   Clearing a flag would not do it -- the buffer was queued with
   XAUDIO2_LOOP_INFINITE and only ExitLoop retires that. */
void sample_end(int chanid)
{
	CHANNEL *ch = chan(chanid);

	if (!ch || !ch->voice) return;
	XA2_CHECK(IXAudio2SourceVoice_ExitLoop(ch->voice, XAUDIO2_COMMIT_NOW),
	          "ExitLoop (sample_end)");
}

int sample_playing(int chanid)
{
	CHANNEL            *ch = chan(chanid);
	XAUDIO2_VOICE_STATE state;

	if (!ch || !ch->voice || !ch->active) return 0;

	/* Still flagged active, so ask the engine whether it has run dry on its
	   own -- that is how a one-shot ending, or sample_end retiring a loop,
	   gets noticed. */
	IXAudio2SourceVoice_GetState(ch->voice, &state, XAUDIO2_VOICE_NOSAMPLESPLAYED);
	if (state.BuffersQueued > 0) return 1;

	ch->active = 0;
	return 0;
}

void samples_stop_all(void)
{
	int i;

	if (!g_xa2) return;
	for (i = 0; i < MIXER_MAX_CHANNELS; i++)
		sample_stop(i);
}

/* -----------------------------------------------------------------------------
 * Per-channel controls
 * -------------------------------------------------------------------------- */
void sample_set_volume(int chanid, int vol)
{
	CHANNEL *ch = chan(chanid);

	if (!ch) return;
	if (vol < 0)   vol = 0;
	if (vol > 255) vol = 255;

	ch->volume = vol;
	if (ch->voice)
		XA2_CHECK(IXAudio2SourceVoice_SetVolume(ch->voice,
		              volume_byte_to_linear(vol), XAUDIO2_COMMIT_NOW),
		          "SetVolume (sample_set_volume)");
}

int sample_get_volume(int chanid)
{
	CHANNEL *ch = chan(chanid);
	return ch ? ch->volume : 0;
}

void sample_set_pan(int chanid, int pan)
{
	CHANNEL *ch = chan(chanid);

	if (!ch) return;
	if (pan < 0)   pan = 0;
	if (pan > 255) pan = 255;

	ch->pan = pan;
	channel_apply_pan(ch);
}

int sample_get_pan(int chanid)
{
	CHANNEL *ch = chan(chanid);
	return ch ? ch->pan : 128;
}

void sample_set_freq(int chanid, int freq)
{
	CHANNEL *ch = chan(chanid);
	float ratio;

	if (!ch || !ch->voice || ch->base_freq <= 0 || freq <= 0) return;

	ratio = (float)freq / (float)ch->base_freq;
	if (ratio < XAUDIO2_MIN_FREQ_RATIO)  ratio = XAUDIO2_MIN_FREQ_RATIO;
	if (ratio > MIXER_MAX_FREQ_RATIO)    ratio = MIXER_MAX_FREQ_RATIO;

	ch->freq = (int)(ratio * (float)ch->base_freq);
	XA2_CHECK(IXAudio2SourceVoice_SetFrequencyRatio(ch->voice, ratio,
	              XAUDIO2_COMMIT_NOW),
	          "SetFrequencyRatio (sample_set_freq)");
}

int sample_get_freq(int chanid)
{
	CHANNEL *ch = chan(chanid);
	return ch ? ch->freq : 0;
}

/* -----------------------------------------------------------------------------
 * Master output
 * -------------------------------------------------------------------------- */
static void master_apply(void)
{
	if (!g_master) return;
	XA2_CHECK(IXAudio2MasteringVoice_SetVolume(
	              g_master,
	              g_paused ? 0.0f : volume_percent_to_linear(g_master_percent),
	              XAUDIO2_COMMIT_NOW),
	          "SetVolume (master)");
}

void mixer_set_master_volume(int percent)
{
	if (percent < 0)   percent = 0;
	if (percent > 100) percent = 100;

	g_master_percent = percent;
	master_apply();
}

/* Returns the percent that was set, not a gain read back off the voice. The
   curve is not linear, so round-tripping through the gain would drift the
   value every time -- badly at the quiet end. Reports the setting even while
   paused, so a slider bound to it does not snap to zero. */
int mixer_get_master_volume(void)
{
	return g_master_percent;
}

void pause_audio(void)
{
	if (!g_xa2) return;
	g_paused = 1;
	master_apply();
}

void restore_audio(void)
{
	if (!g_xa2) return;
	g_paused = 0;
	master_apply();
}

/* -----------------------------------------------------------------------------
 * Lookup
 * -------------------------------------------------------------------------- */
const char *numToName(int samplenum)
{
	SAMPLE *s = samp(samplenum);
	return s ? s->name : NULL;
}

int nameToNum(const char *name)
{
	int i;

	if (!name) return -1;
	for (i = 0; i < MIXER_MAX_SAMPLES; i++)
		if (g_sample[i].in_use && strcmp(g_sample[i].name, name) == 0)
			return i;
	return -1;
}

/* -----------------------------------------------------------------------------
 * Streaming (a continuously-fed voice; see mixer.h for the contract)
 *
 * Ring of STREAM_SLOTS buffers, static storage -- no malloc per push, same
 * as the rest of this file avoids per-call allocation. Sized for up to
 * stereo (2 channels) even though the one caller today (the POKEY render)
 * is mono; the extra memory is trivial (16 * 512 * 2ch * 2 bytes = 32 KB).
 * -------------------------------------------------------------------------- */
typedef struct STREAM
{
	IXAudio2SourceVoice *voice;
	int      channels;
	int16_t  ring[STREAM_SLOTS][STREAM_BLOCK_FRAMES * 2];
	int      next_slot;
	int      started;
} STREAM;

static STREAM g_stream;

/* Stream health counters, read and reset by stream_stats(): pushes that
 * found the voice already drained (a gap was just heard), forced flushes,
 * and the queue depth seen at push time. */
static struct {
	unsigned pushes, starved, flushes, depth_min, depth_max, depth_sum;
} g_sstat;

void stream_stats(char *buf, size_t n)
{
	snprintf(buf, n, "stream: %u pushes, %u starved, %u flushed, depth %u..%u avg %.1f",
	         g_sstat.pushes, g_sstat.starved, g_sstat.flushes,
	         g_sstat.pushes ? g_sstat.depth_min : 0, g_sstat.depth_max,
	         g_sstat.pushes ? (double)g_sstat.depth_sum / g_sstat.pushes : 0.0);
	memset(&g_sstat, 0, sizeof g_sstat);
}

int stream_open(int sample_rate, int channels)
{
	WAVEFORMATEX fx;
	HRESULT hr;

	if (!g_xa2) {
		LOG_ERROR("stream_open: mixer not initialized");
		return -1;
	}
	if (g_stream.voice) return 0;   /* already open */
	if (channels != 1 && channels != 2) {
		LOG_ERROR("stream_open: %d channels not supported (1 or 2 only)", channels);
		return -1;
	}
	if (sample_rate <= 0) {
		LOG_ERROR("stream_open: bad sample rate %d", sample_rate);
		return -1;
	}

	memset(&fx, 0, sizeof fx);
	fx.wFormatTag      = WAVE_FORMAT_PCM;
	fx.nChannels       = (WORD)channels;
	fx.nSamplesPerSec  = (DWORD)sample_rate;
	fx.wBitsPerSample  = 16;
	fx.nBlockAlign     = (WORD)(channels * 2);
	fx.nAvgBytesPerSec = fx.nSamplesPerSec * fx.nBlockAlign;
	fx.cbSize          = 0;

	hr = IXAudio2_CreateSourceVoice(g_xa2, &g_stream.voice, &fx, 0,
	                                 1.0f, NULL, NULL, NULL);
	if (FAILED(hr)) {
		LOG_ERROR("CreateSourceVoice (stream) failed: 0x%08lX", (unsigned long)hr);
		g_stream.voice = NULL;
		return -1;
	}

	g_stream.channels  = channels;
	g_stream.next_slot = 0;
	g_stream.started   = 0;
	LOG_INFO("Stream voice open: %d Hz, %d channel(s)", sample_rate, channels);
	return 0;
}

/* Queue STREAM_PRIME_BLOCKS blocks of silence, each `frames` long, ahead
 * of the caller's audio.  The producer feeds one ~4 ms block per IRQ tick
 * and XAudio2 drains at exactly that rate, so the queue depth never grows
 * on its own: whatever is queued when the voice starts is the whole margin
 * against a late push, and one starved quantum is an audible gap.  Three
 * blocks (~12 ms) cover the frame-length hitches seen in practice at the
 * cost of 12 ms of latency; done at voice start and again after a flush,
 * which empties the queue the same way. */
#define STREAM_PRIME_BLOCKS 3

/* Tempest (M8 part 2): settable, because the Tempest backend presents on a
 * vsynced swap from the game thread, which can hold the producer up to one
 * display refresh (~17 ms, 4 IRQ ticks); see stream_set_prime_blocks. */
static int stream_prime_blocks = STREAM_PRIME_BLOCKS;

void stream_set_prime_blocks(int blocks)
{
	if (blocks < 1) blocks = 1;
	if (blocks > STREAM_SLOTS / 2) blocks = STREAM_SLOTS / 2;
	stream_prime_blocks = blocks;
}

static void stream_prime(int frames)
{
	int i;
	for (i = 0; i < stream_prime_blocks; i++) {
		XAUDIO2_BUFFER buf;
		int16_t *slot = g_stream.ring[g_stream.next_slot];
		g_stream.next_slot = (g_stream.next_slot + 1) % STREAM_SLOTS;
		memset(slot, 0, (size_t)frames * g_stream.channels * sizeof(int16_t));
		memset(&buf, 0, sizeof buf);
		buf.AudioBytes = (UINT32)(frames * g_stream.channels * (int)sizeof(int16_t));
		buf.pAudioData = (const BYTE *)slot;
		XA2_CHECK(IXAudio2SourceVoice_SubmitSourceBuffer(g_stream.voice, &buf, NULL),
		          "SubmitSourceBuffer (stream prime)");
	}
}

void stream_push(const int16_t *pcm, int frames)
{
	XAUDIO2_VOICE_STATE state;
	XAUDIO2_BUFFER       buf;
	int16_t             *slot;
	HRESULT              hr;

	if (!g_stream.voice || !pcm || frames <= 0) return;
	if (frames > STREAM_BLOCK_FRAMES) frames = STREAM_BLOCK_FRAMES;

	IXAudio2SourceVoice_GetState(g_stream.voice, &state, XAUDIO2_VOICE_NOSAMPLESPLAYED);
	if (g_sstat.pushes == 0 || state.BuffersQueued < g_sstat.depth_min)
		g_sstat.depth_min = state.BuffersQueued;
	if (state.BuffersQueued > g_sstat.depth_max) g_sstat.depth_max = state.BuffersQueued;
	g_sstat.depth_sum += state.BuffersQueued;
	g_sstat.pushes++;
	if (g_stream.started && state.BuffersQueued == 0) g_sstat.starved++;
	if (state.BuffersQueued >= STREAM_SLOTS - 1) {
		g_sstat.flushes++;
		/* The caller has fallen behind. XAudio2 reads each queued buffer
		   from the ring slot it was submitted from, so a queue deeper than
		   the ring would have the next push overwrite a slot XAudio2 still
		   owns -- the bound is the ring, not XAUDIO2_MAX_QUEUED_BUFFERS.
		   There is no "drop just the oldest" call in the API, so the whole
		   backlog goes; a short gap beats a stretch of stale audio. */
		XA2_CHECK(IXAudio2SourceVoice_FlushSourceBuffers(g_stream.voice),
		          "FlushSourceBuffers (stream overflow)");
		stream_prime(frames);
	}
	if (!g_stream.started)
		stream_prime(frames);

	slot = g_stream.ring[g_stream.next_slot];
	g_stream.next_slot = (g_stream.next_slot + 1) % STREAM_SLOTS;
	memcpy(slot, pcm, (size_t)frames * g_stream.channels * sizeof(int16_t));

	memset(&buf, 0, sizeof buf);
	buf.AudioBytes = (UINT32)(frames * g_stream.channels * (int)sizeof(int16_t));
	buf.pAudioData = (const BYTE *)slot;

	hr = IXAudio2SourceVoice_SubmitSourceBuffer(g_stream.voice, &buf, NULL);
	if (FAILED(hr)) {
		LOG_ERROR("SubmitSourceBuffer (stream) failed: 0x%08lX", (unsigned long)hr);
		return;
	}

	if (!g_stream.started) {
		hr = IXAudio2SourceVoice_Start(g_stream.voice, 0, XAUDIO2_COMMIT_NOW);
		if (FAILED(hr))
			LOG_ERROR("Voice start (stream) failed: 0x%08lX", (unsigned long)hr);
		else
			g_stream.started = 1;
	}
}

void stream_close(void)
{
	if (!g_stream.voice) return;
	XA2_CHECK(IXAudio2SourceVoice_Stop(g_stream.voice, 0, XAUDIO2_COMMIT_NOW),
	          "Stop (stream)");
	XA2_CHECK(IXAudio2SourceVoice_FlushSourceBuffers(g_stream.voice),
	          "FlushSourceBuffers (stream)");
	IXAudio2SourceVoice_DestroyVoice(g_stream.voice);
	memset(&g_stream, 0, sizeof g_stream);
	LOG_INFO("Stream voice closed");
}

void stream_set_volume(int vol255)
{
	if (!g_stream.voice) return;
	XA2_CHECK(IXAudio2SourceVoice_SetVolume(g_stream.voice,
	              volume_byte_to_linear(vol255), XAUDIO2_COMMIT_NOW),
	          "SetVolume (stream)");
}
