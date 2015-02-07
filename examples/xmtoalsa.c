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

static struct termios customflags, previousflags;

void usage(char* progname) {
	FATAL("Usage:\n" "\t%s --help\n"
		  "\t\tShow this message.\n"
	      "\t%s [--loop N] [--random] [--device default] [--buffer-size 4096] [--period-size 2048] [--rate 96000] [--format float|s16|s32] [--] <filenames…>\n"
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
	
	snd_pcm_t* device;
	void* params;

	char* devicename = "default";
	size_t buffer_size = 0;
	size_t period_size = 0;
	const unsigned int channels = 2;
	unsigned int rate = 96000;
	snd_pcm_format_t format = SND_PCM_FORMAT_FLOAT;
	size_t bps = sizeof(float);
	unsigned long loop = 1;
	unsigned long izero = 1; /* Index in argv of the first filename */
	
	bool paused = false, jump = false, random = false;

	if(argc == 1 || !strcmp(argv[1], "-h") || !strcmp(argv[1], "--help")) {
		usage(argv[0]);
	}

	for(size_t i = 1; i < argc; ++i) {
		if(!strcmp(argv[i], "--")) {
			izero = i+1;
			break;
		}
		
		if(!strcmp(argv[i], "--loop")) {
			if(argc == i+1) FATAL("%s: expected argument after %s\n", argv[0], argv[i]);
			loop = strtol(argv[i+1], NULL, 0);
			++i;
			continue;
		}

		if(!strcmp(argv[i], "--random")) {
			random = true;
			continue;
		}

		if(!strcmp(argv[i], "--device")) {
			if(argc == i+1) FATAL("%s: expected argument after %s\n", argv[0], argv[i]);
			devicename = argv[i+1];
			++i;
			continue;
		}

		if(!strcmp(argv[i], "--buffer-size")) {
			if(argc == i+1) FATAL("%s: expected argument after %s\n", argv[0], argv[i]);
			buffer_size = (size_t)strtol(argv[i+1], NULL, 0);
			++i;
			continue;
		}

		if(!strcmp(argv[i], "--period-size")) {
			if(argc == i+1) FATAL("%s: expected argument after %s\n", argv[0], argv[i]);
			period_size = (size_t)strtol(argv[i+1], NULL, 0);
			++i;
			continue;
		}

		if(!strcmp(argv[i], "--rate")) {
			if(argc == i+1) FATAL("%s: expected argument after %s\n", argv[0], argv[i]);
			rate = (unsigned int)strtol(argv[i+1], NULL, 0);
			++i;
			continue;
		}

		if(!strcmp(argv[i], "--format")) {
			if(argc == i+1) FATAL("%s: expected argument after %s\n", argv[0], argv[i]);
			++i;

			if(!strcmp(argv[i], "float")) {
				format = SND_PCM_FORMAT_FLOAT;
				bps = sizeof(float);
				continue;
			}

			if(!strcmp(argv[i], "s16")) {
				format = SND_PCM_FORMAT_S16;
				bps = 2;
				continue;
			}

			if(!strcmp(argv[i], "s32")) {
				format = SND_PCM_FORMAT_S32;
				bps = 4;
				continue;
			}

			FATAL("%s: unknown format %s\n", argv[0], argv[i]);
		}

		izero = i;
		break;
	}

	if(random) {
		char* t;
		size_t r;

		srand(time(NULL));

		/* Randomize argv[izero..] using the well-known Knuth algorithm */
		for(size_t i = argc-1; i > izero; --i) {
			r = (rand() % (i - izero + 1)) + izero;
			t = argv[i];
			argv[i] = argv[r];
			argv[r] = t;
		}
	}

	if(buffer_size == 0 && period_size == 0) period_size = 2048;
	
	if(buffer_size == 0) {
		buffer_size = period_size << 1;
	}
	if(period_size == 0) {
		period_size = buffer_size >> 1;
	}
	
	char alsabuffer[period_size * bps];
	float xmbuffer[period_size];
	const snd_pcm_uframes_t nframes = period_size / channels;

	CHECK_ALSA_CALL(snd_pcm_open(&device, devicename, SND_PCM_STREAM_PLAYBACK, 0));
	CHECK_ALSA_CALL(snd_pcm_hw_params_malloc((snd_pcm_hw_params_t**)(&params)));
	CHECK_ALSA_CALL(snd_pcm_hw_params_any(device, params));
	CHECK_ALSA_CALL(snd_pcm_hw_params_set_access(device, params, SND_PCM_ACCESS_RW_INTERLEAVED));
	if(snd_pcm_hw_params_set_format(device, params, format) < 0
	   && snd_pcm_hw_params_set_format(device, params, format = SND_PCM_FORMAT_FLOAT) < 0
	   && snd_pcm_hw_params_set_format(device, params, format = SND_PCM_FORMAT_S32) < 0) {
		CHECK_ALSA_CALL(snd_pcm_hw_params_set_format(device, params, format = SND_PCM_FORMAT_S16));
	}
	if(snd_pcm_hw_params_set_rate(device, params, rate, 0) < 0
	   && snd_pcm_hw_params_set_rate(device, params, rate = 96000, 0) < 0
	   && snd_pcm_hw_params_set_rate(device, params, rate = 48000, 0) < 0) {
		CHECK_ALSA_CALL(snd_pcm_hw_params_set_rate(device, params, rate = 44100, 0));
	}
	CHECK_ALSA_CALL(snd_pcm_hw_params_set_channels(device, params, channels));

	CHECK_ALSA_CALL(snd_pcm_hw_params_set_buffer_size_near(device, params, &buffer_size));
	CHECK_ALSA_CALL(snd_pcm_hw_params_set_period_size_near(device, params, &period_size, 0));
	CHECK_ALSA_CALL(snd_pcm_hw_params(device, params));
	snd_pcm_hw_params_free(params);
	CHECK_ALSA_CALL(snd_pcm_sw_params_malloc((snd_pcm_sw_params_t**)(&params)));
	CHECK_ALSA_CALL(snd_pcm_sw_params_current(device, params));
	CHECK_ALSA_CALL(snd_pcm_sw_params_set_start_threshold(device, params, buffer_size));
	CHECK_ALSA_CALL(snd_pcm_sw_params(device, params));
	snd_pcm_sw_params_free(params);

	printf("Opened ALSA device: %s, %s, %i Hz, %lu/%lu\n",
	       devicename,
	       format == SND_PCM_FORMAT_FLOAT ? "float" : (format == SND_PCM_FORMAT_S32 ? "s32" : "s16"),
	       rate,
	       period_size,
	       buffer_size
		);

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
				if(paused == true) {
					memset(xmbuffer, 0, sizeof(xmbuffer));
					printf("\r      ----- PAUSE -----             "
					       "                                       ");
				}
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

			if(!paused) {
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
				xm_generate_samples(ctx, xmbuffer, nframes);
			}

			fflush(stdout);

			if(format == SND_PCM_FORMAT_FLOAT) {
				CHECK_ALSA_CALL(snd_pcm_writei(device, xmbuffer, nframes));
			} else {
				if(format == SND_PCM_FORMAT_S16) {
					for(size_t i = 0; i < period_size; ++i) {
						((int16_t*)alsabuffer)[i] = (int16_t)((double)xmbuffer[i] * 32767.);
					}
				} else if(format == SND_PCM_FORMAT_S32) {
					for(size_t i = 0; i < period_size; ++i) {
						((int32_t*)alsabuffer)[i] = (int32_t)((double)xmbuffer[i] * 2147483647.);
					}
				}
				
				CHECK_ALSA_CALL(snd_pcm_writei(device, alsabuffer, nframes));
			}
		}

		printf("\n");
		CHECK_ALSA_CALL(snd_pcm_drop(device));
		xm_free_context(ctx);
	}

	CHECK_ALSA_CALL(snd_pcm_close(device));
	return 0;
}
