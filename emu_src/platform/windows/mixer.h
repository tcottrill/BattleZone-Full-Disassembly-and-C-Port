/* =============================================================================
 * mixer.h -- Sample playback for Windows 11 / XAudio2 2.9
 *
 * A small, sample-only audio mixer. Each logical channel owns one XAudio2
 * source voice; XAudio2 does the mixing, pitch shifting and sample-rate
 * conversion, so there is no software mix loop, no audio thread, and nothing
 * to pump from the game loop.
 *
 * Usage:
 *     mixer_init();
 *     int snd = load_sample(NULL, "data\\explode.wav");
 *     sample_start(0, snd, 0);
 *     sample_set_volume(0, 200);
 *     ...
 *     mixer_end();
 *
 * Samples are played at their native rate and bit depth. 8- and 16-bit PCM,
 * mono or stereo, any sample rate. No load-time conversion of any kind.
 *
 * Threading: every function here must be called from one thread (the game
 * thread). XAudio2 does its own mixing on its own thread; none of the state
 * in mixer.c is shared with it.
 *
 * Copyright (C) 2022-2026  Tim Cottrill
 * SPDX-License-Identifier: GPL-3.0-or-later
 * ============================================================================= */

#pragma once

#ifndef MIXER_H
#define MIXER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MIXER_MAX_CHANNELS 20
#define MIXER_MAX_SAMPLES  256

/* Where mixer_init leaves the master. 80% sits about 2.5 dB below unity, so
   the game does not come out noticeably louder than everything else running
   on the desktop. */
#define MIXER_DEFAULT_MASTER_VOLUME 80

/* -------------------------------------------------------------------------
 * Lifecycle
 * ------------------------------------------------------------------------- */

/* Brings up XAudio2 and a stereo mastering voice. Returns 0 on success, -1 on
   failure. Safe to call twice; the second call is a no-op returning 0. */
int  mixer_init(void);

/* Stops everything, frees all samples, tears XAudio2 down. */
void mixer_end(void);

/* -------------------------------------------------------------------------
 * Sample loading
 *
 * Pass archname = NULL to read a loose file from disk, or a .zip path to pull
 * the entry out of an archive. Returns a sample number >= 0, or -1 on failure.
 * ------------------------------------------------------------------------- */
int  load_sample(const char *archname, const char *filename);

/* Frees a sample. Any channel currently playing it is stopped first. */
void sample_remove(int samplenum);

/* -------------------------------------------------------------------------
 * Playback
 * ------------------------------------------------------------------------- */

/* Starts samplenum on chanid, looping forever if loop is non-zero. Resets the
   channel's volume, pan and frequency to their defaults (255 / 128 / native),
   so set those after starting, not before. */
void sample_start(int chanid, int samplenum, int loop);

/* Stops immediately and discards the queued audio. */
void sample_stop(int chanid);

/* Leaves the loop but lets the current pass play out to its end. */
void sample_end(int chanid);

/* Non-zero while audio is still queued on the channel. */
int  sample_playing(int chanid);

void samples_stop_all(void);

/* -------------------------------------------------------------------------
 * Per-channel controls
 * ------------------------------------------------------------------------- */

void sample_set_volume(int chanid, int vol);   /* 0..255, 255 = full        */
void sample_set_pan   (int chanid, int pan);   /* 0..255, 128 = centre      */
void sample_set_freq  (int chanid, int freq);  /* Hz; native rate = normal  */

int  sample_get_volume(int chanid);            /* returns what you set      */
int  sample_get_pan   (int chanid);
int  sample_get_freq  (int chanid);

/* -------------------------------------------------------------------------
 * Master output
 * ------------------------------------------------------------------------- */

void mixer_set_master_volume(int percent);     /* 0..100 */
int  mixer_get_master_volume(void);            /* 0..100, exactly what was set */

/* Ducks the output to silence and brings it back. This is separate from the
   volume setting, so mixer_set_master_volume() still works while paused and
   takes effect on restore, and mixer_get_master_volume() keeps reporting the
   setting rather than zero. Both are idempotent. */
void pause_audio(void);
void restore_audio(void);

/* -------------------------------------------------------------------------
 * Lookup and conversion helpers
 * ------------------------------------------------------------------------- */

/* Sample name is the filename with directories and extension stripped. */
const char *numToName(int samplenum);          /* NULL if not loaded */
int         nameToNum(const char *name);       /* -1 if not found    */

int mixer_percent_to_byte(int percent);        /* 0..100 -> 0..255 */
int mixer_byte_to_percent(int vol255);         /* 0..255 -> 0..100 */

/* -------------------------------------------------------------------------
 * Streaming (a continuously-fed voice, for a synthesized source like a
 * POKEY render loop rather than a sample loaded whole from disk)
 *
 * A single mono 16-bit PCM source voice at a fixed rate, fed in small
 * blocks via stream_push -- the producer decides how many frames go in
 * each call (up to STREAM_BLOCK_FRAMES) and how often; this just queues
 * them on XAudio2, which plays queued buffers back to back with no gap.
 * There is exactly one stream (not per-channel, unlike the sample API
 * above); open it once, push forever, close it once.
 *
 * Latency: a push lands in the queue behind whatever XAudio2 has not yet
 * drained, so worst-case latency is (buffers currently queued) *
 * (frames per push) / sample_rate. At the intended cadence -- one push
 * of ~4 ms of audio per NMI tick -- steady state is a couple of queued
 * buffers, a matter of milliseconds; the full 16-slot ring is a hard
 * ceiling for a caller that falls behind, not the expected depth.
 * ------------------------------------------------------------------------- */
#define STREAM_SLOTS        16
#define STREAM_BLOCK_FRAMES 512

/* Opens the stream voice at sample_rate, 16-bit PCM, `channels` channels
   (1 or 2; the POKEY render this exists for is mono). Returns 0 on
   success, -1 on failure (including "mixer_init was never called or
   failed"). Safe to call again with the stream already open: a no-op
   returning 0. */
int  stream_open(int sample_rate, int channels);

/* Queues up to STREAM_BLOCK_FRAMES frames of PCM (frames beyond that are
   dropped, not clamped-and-copied-partially -- the caller should not be
   pushing blocks that large). Starts the voice on the first call. If the
   queue has reached STREAM_SLOTS - 1 buffers the queue is flushed first:
   XAudio2 plays straight out of the ring slots, so the queue can never
   be allowed to grow past the ring (the caller has fallen far behind;
   a short gap beats stale audio). A no-op if the stream was never
   opened. */
void stream_push(const int16_t *pcm, int frames);

/* Formats the stream's health counters since the previous call into buf
   (pushes, pushes that found the voice drained, forced flushes, queue
   depth range) and resets them. */
void stream_stats(char *buf, size_t n);

/* Stops and destroys the stream voice. Safe to call when not open. */
void stream_close(void);

/* 0..255, same curve as sample_set_volume. */
void stream_set_volume(int vol255);

/* Blocks of silence queued ahead of the first push and after a flush (the
   margin against a late producer; default 3, clamped 1..STREAM_SLOTS/2).
   Call before the first stream_push. */
void stream_set_prime_blocks(int blocks);

#ifdef __cplusplus
}
#endif

#endif /* MIXER_H */
