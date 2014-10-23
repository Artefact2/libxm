/* Author: Romain "Artefact2" Dalmaso <artefact2@gmail.com> */

/* This program is free software. It comes without any warranty, to the
 * extent permitted by applicable law. You can redistribute it and/or
 * modify it under the terms of the Do What The Fuck You Want To Public
 * License, Version 2, as published by Sam Hocevar. See
 * http://sam.zoy.org/wtfpl/COPYING for more details. */

#pragma once
#ifndef __has_xm_h
#define __has_xm_h

#include <stdlib.h>
#include <stdint.h>

struct xm_context_s;
typedef struct xm_context_s xm_context_t;

/** Create a XM context.
 *
 * @param moddata the contents of the module
 * @param rate play rate in Hz, recommended value of 48000
 *
 * @returns 0 on success
 * @returns 1 if module data is not sane
 * @returns 2 if memory allocation failed
 */
int xm_create_context(xm_context_t**, const char* moddata, uint32_t rate);

/** Free a XM context created by xm_create_context(). */
void xm_free_context(xm_context_t*);

/** Play the module and put the sound samples in an output buffer.
 *
 * @param output buffer of 2*numsamples elements
 * @param numsamples number of samples to generate
 */
void xm_generate_samples(xm_context_t*, float* output, size_t numsamples);



/** Set the maximum number of times a module can loop. After the
 * specified number of loops, calls to xm_generate_samples will only
 * generate silence. You can control the current number of loops with
 * xm_get_loop_count().
 *
 * @param loopcnt maximum number of loops. Use 0 to loop
 * indefinitely. */
void xm_set_max_loop_count(xm_context_t*, uint8_t loopcnt);

/** Get the loop count of the currently playing module. This value is
 * 0 when the module is still playing, 1 when the module has looped
 * once, etc. */
uint8_t xm_get_loop_count(xm_context_t*);



/** Get the module name as a NUL-terminated string. */
const char* xm_get_module_name(xm_context_t*);

/** Get the tracker name as a NUL-terminated string. */
const char* xm_get_tracker_name(xm_context_t*);



/** Get the module length (in patterns). */
uint16_t xm_get_module_length(xm_context_t*);

/** Get the number of patterns. */
uint16_t xm_get_number_of_patterns(xm_context_t*);

/** Get the number of rows of a pattern. */
uint16_t xm_get_number_of_rows(xm_context_t*, uint16_t);

/** Get the number of instruments. */
uint16_t xm_get_number_of_instruments(xm_context_t*);

/** Get the number of samples of an instrument. */
uint16_t xm_get_number_of_samples(xm_context_t*, uint16_t);



/** Get the current module speed.
 *
 * @param bpm will receive the current BPM
 * @param tempo will receive the current tempo (ticks per line)
 */
void xm_get_playing_speed(xm_context_t*, uint16_t* bpm, uint16_t* tempo);

/** Get the current position in the module being played.
 *
 * @param pattern_index will receive the current pattern index in the
 * POT (pattern order table)
 * @param pattern will receive the current pattern number
 * @param row will receive the current row
 * @param samples will receive the total number of generated samples
 * (divide by sample rate to get seconds of generated audio)
 */
void xm_get_position(xm_context_t*, uint8_t* pattern_index, uint8_t* pattern, uint8_t* row, uint64_t* samples);

#endif
