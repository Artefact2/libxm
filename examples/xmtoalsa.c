/* Author: Romain "Artefact2" Dalmaso <artefact2@gmail.com> */

/* This program is free software. It comes without any warranty, to the
 * extent permitted by applicable law. You can redistribute it and/or
 * modify it under the terms of the Do What The Fuck You Want To Public
 * License, Version 2, as published by Sam Hocevar. See
 * http://sam.zoy.org/wtfpl/COPYING for more details. */

#include "testprog.h"
#include <string.h>
#include <stdbool.h>
#include <alsa/asoundlib.h>

/* NB: these headers may not be very portable, but since ALSA is
 * already being used (and is Linux-only), portability was never a
 * concern. */
#include <sys/select.h>
#include <termios.h>

#define FATAL_ALSA_ERR(s, err) do {							\
		fprintf(stderr, s ": %s\n", snd_strerror((err)));	\
		fflush(stderr);										\
		exit(1);											\
	} while(0)

#define CHECK_ALSA_CALL(call) do {							\
		int ret = (call);									\
		if(ret < 0)	{										\
			ret = snd_pcm_recover(device, ret, 0);			\
			if(ret < 0)										\
				FATAL_ALSA_ERR("ALSA internal error", ret);	\
		}													\
	} while(0)

static const size_t buffer_size = 2048; /* Average buffer size, should
										 * be enough even on slow CPUs
										 * and not too laggy */
static const unsigned int channels = 2;
static const unsigned int rate = 48000;

static struct termios customflags, previousflags;

void usage(char* progname) {
	FATAL("Usage:\n" "\t%s --help\n"
		  "\t\tShow this message.\n"
		  "\t%s [--loop N] <filenames…>\n"
		  "\t\tPlay modules in this order. Loop each module N times (0 to loop indefinitely).\n\n"
		  "Interactive controls:\n"
		  "\tspace: pause/resume playback\n"
		  "\tq: exit program\n"
		  "\tp: previous module\n"
		  "\tn: next module\n"
		  "\tl: toggle looping\n",
		  progname, progname);
}

void restoreterm(void) {
	tcsetattr(0, TCSANOW, &previousflags);
}

char get_command(void) {
	static fd_set f;
	static struct timeval t;

	if(t.tv_usec == 0) {
		t.tv_sec = 0;
		t.tv_usec = 1;
		FD_ZERO(&f);
		FD_SET(0, &f);
	}

	if(select(1, &f, NULL, NULL, &t) > 0) {
		return getchar();
	}

	return 0;
}

int main(int argc, char** argv) {
	xm_context_t* ctx;
	float buffer[buffer_size];
    void* params;
	snd_pcm_t* device;
	unsigned long loop = 1;
	unsigned long izero = 1;
	bool paused = false, jump = false;

	if(argc == 1 || !strcmp(argv[1], "-h") || !strcmp(argv[1], "--help")) {
		usage(argv[0]);
	}

	if(!strcmp(argv[1], "--loop")) {
		if(argc == 2) usage(argv[0]);
		loop = strtol(argv[2], NULL, 0);
		izero = 3;
	}

	CHECK_ALSA_CALL(snd_pcm_open(&device, "default", SND_PCM_STREAM_PLAYBACK, 0));
	CHECK_ALSA_CALL(snd_pcm_hw_params_malloc((snd_pcm_hw_params_t**)(&params)));
	CHECK_ALSA_CALL(snd_pcm_hw_params_any(device, params));
	CHECK_ALSA_CALL(snd_pcm_hw_params_set_access(device, params, SND_PCM_ACCESS_RW_INTERLEAVED));
	CHECK_ALSA_CALL(snd_pcm_hw_params_set_format(device, params, SND_PCM_FORMAT_FLOAT));
	CHECK_ALSA_CALL(snd_pcm_hw_params_set_rate(device, params, rate, 0));
	CHECK_ALSA_CALL(snd_pcm_hw_params_set_channels(device, params, channels));
	CHECK_ALSA_CALL(snd_pcm_hw_params_set_buffer_size(device, params, buffer_size << 2));
	CHECK_ALSA_CALL(snd_pcm_hw_params_set_period_size(device, params, buffer_size, 0));
	CHECK_ALSA_CALL(snd_pcm_hw_params(device, params));
	snd_pcm_hw_params_free(params);
	CHECK_ALSA_CALL(snd_pcm_sw_params_malloc((snd_pcm_sw_params_t**)(&params)));
	CHECK_ALSA_CALL(snd_pcm_sw_params_current(device, params));
	CHECK_ALSA_CALL(snd_pcm_sw_params_set_start_threshold(device, params, buffer_size));
	CHECK_ALSA_CALL(snd_pcm_sw_params(device, params));
	snd_pcm_sw_params_free(params);

	tcgetattr(0, &previousflags);
	atexit(restoreterm);

	customflags = previousflags;
	customflags.c_lflag &= ~ECHO;
	customflags.c_lflag &= ~ICANON;
	customflags.c_lflag |= ECHONL;

	tcsetattr(0, TCSANOW, &customflags);

	for(unsigned long i = izero; i < argc; ++i) {
		uint16_t num_patterns, length, bpm, tempo;
		uint64_t samples;
		uint8_t pos, pat, row;

		jump = false;
		create_context_from_file(&ctx, rate, argv[i]);
		xm_set_max_loop_count(ctx, loop);
		num_patterns = xm_get_number_of_patterns(ctx);
		length = xm_get_module_length(ctx);

		printf("==> Playing: %s\n"
			   "==> Tracker: %s\n",
			   xm_get_module_name(ctx),
			   xm_get_tracker_name(ctx));

		CHECK_ALSA_CALL(snd_pcm_prepare(device));

		while(!jump && (loop == 0 || xm_get_loop_count(ctx) < loop)) {
			switch(get_command()) {
			case ' ':
				paused = !paused;
				break;
			case 'q':
				exit(0);
				break;
			case 'p':
				if(i == izero) exit(0);
				jump = true;
				i -= 2;
				continue;
			case 'n':
				jump = true;
				continue;
			case 'l':
				loop = !loop;
				xm_set_max_loop_count(ctx, loop);
				break;
			default:
				break;
			}

			if(paused) {
				printf("\r      ----- PAUSE -----             "
					   "                                       ");
				memset(buffer, 0, sizeof(buffer));
			} else {
				xm_get_position(ctx, &pos, &pat, &row, &samples);
				xm_get_playing_speed(ctx, &bpm, &tempo);
				printf("\rSpeed[%.2X] BPM[%.2X] Pos[%.2X/%.2X]"
					   " Pat[%.2X/%.2X] Row[%.2X/%.2X] Loop[%.2X/%.2lX]"
					   " %.2i:%.2i:%.2i.%.2i ",
					   tempo, bpm,
					   pos, length,
					   pat, num_patterns,
					   row, xm_get_number_of_rows(ctx, pat),
					   xm_get_loop_count(ctx), loop,
					   (unsigned int)((float)samples / (3600 * rate)),
					   (unsigned int)((float)(samples % (3600 * rate) / (60 * rate))),
					   (unsigned int)((float)(samples % (60 * rate)) / rate),
					   (unsigned int)(100 * (float)(samples % rate) / rate)
				);
				xm_generate_samples(ctx, buffer, sizeof(buffer) / (channels * sizeof(float)));
			}

			fflush(stdout);
			CHECK_ALSA_CALL(snd_pcm_writei(device, buffer, sizeof(buffer) / (channels * sizeof(float))));
		}

		printf("\n");
		CHECK_ALSA_CALL(snd_pcm_drop(device));
		xm_free_context(ctx);
	}

	CHECK_ALSA_CALL(snd_pcm_close(device));
	return 0;
}
