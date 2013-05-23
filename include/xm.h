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
int xm_create_context(xm_context_t**, char* moddata, uint32_t rate);

/** Free a XM context created by xm_create_context(). */
void xm_free_context(xm_context_t*);

/** Play the module and put the sound samples in an output buffer.
 *
 * @param output buffer of 2*numsamples elements
 * @param numsamples number of samples to generate
 */
void xm_generate_samples(xm_context_t*, float* output, size_t numsamples);

/** Get the loop count of the currently playing module. This value is
 * 0 when the module is still playing, 1 when the module has looped
 * once, etc. */
uint32_t xm_get_loop_count(xm_context_t*);

#endif
