/* Author: Romain "Artefact2" Dalmaso <artefact2@gmail.com> */

/* This program is free software. It comes without any warranty, to the
 * extent permitted by applicable law. You can redistribute it and/or
 * modify it under the terms of the Do What The Fuck You Want To Public
 * License, Version 2, as published by Sam Hocevar. See
 * http://sam.zoy.org/wtfpl/COPYING for more details. */

#define _DEFAULT_SOURCE

#include "testprog.h"
#include "alsa_common.h"
#include <string.h>
#include <stdbool.h>
#include <unistd.h>

/* NB: these headers may not be very portable, but since ALSA is
 * already being used (and is Linux-only), portability was never a
 * concern. */
#include <sys/select.h>
#include <termios.h>

static struct termios customflags, previousflags;

static void usage(char* progname) {
	FATAL("Usage:\n" "\t%s --help\n"
	      "\t\tShow this message.\n"
	      "\t%s [--loop N] [--random] [--preamp 1.0] [--device default] [--buffer-size 4096] [--period-size 2048] [--rate 48000] [--format float|s16|s32] [--] <filenames…>\n"
	      "\t\tPlay modules in this order. Loop each module N times (0 to loop indefinitely).\n\n"
	      "Interactive controls:\n"
	      "\tspace: pause/resume playback\n"
	      "\tq: exit program\n"
	      "\tp: previous module\n"
	      "\tn: next module\n"
	      "\tl: toggle looping\n"
		  "\t>: jump to next pattern in table\n"
		  "\t<: jump to previous pattern in table (will enable looping)\n"
	      "\t0...9A...V: toggle channel mute\n",
	      progname, progname);
}

static void restoreterm(void) {
	tcsetattr(0, TCSANOW, &previousflags);
}

static char get_command(void) {
	static fd_set f;
	static struct timeval t;

	FD_ZERO(&f);
	FD_SET(fileno(stdin), &f);
	t.tv_sec = 0;
	t.tv_usec = 0;

	if(select(1, &f, NULL, NULL, &t) > 0) {
		return getchar();
	}

	return 0;
}

int main(int argc, char** argv) {
	xm_context_t* ctx;
	snd_pcm_t* device;

	const unsigned int channels = 2;
	size_t period_size;
	unsigned int rate;
	snd_pcm_format_t format;
	float preamp = 1.f;
	unsigned long loop = 1;
	unsigned long izero = 1; /* Index in argv of the first filename */

	bool paused = false, waspaused = false, jump = false, random = false;
	uint64_t samples, channel_map_until = 0;

	if(argc == 1 || !strcmp(argv[1], "-h") || !strcmp(argv[1], "--help")) {
		usage(argv[0]);
	}

	for(size_t i = 1; i < argc; ++i) {
		if(!strcmp(argv[i], "--")) {
			izero = i+1;
			break;
		}

		if(!strcmp(argv[i], "--preamp")) {
			if(argc == i+1) FATAL("%s: expected argument after %s\n", argv[0], argv[i]);
			preamp = strtof(argv[i+1], NULL);
			++i;
			continue;
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

		if(!strcmp(argv[i], "--device")
		   || !strcmp(argv[i], "--buffer-size")
		   || !strcmp(argv[i], "--period-size")
		   || !strcmp(argv[i], "--rate")
		   || !strcmp(argv[i], "--format")) {
			++i;
			continue;
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

	init_alsa_device(argc, argv, 128, 1024, 0, &device, &period_size, &rate, &format);

	float xmbuffer[period_size * channels];
	float alsabuffer[period_size * channels];

	tcgetattr(0, &previousflags);
	atexit(restoreterm);

	customflags = previousflags;
	customflags.c_lflag &= ~ECHO;
	customflags.c_lflag &= ~ICANON;
	customflags.c_lflag |= ECHONL;

	tcsetattr(0, TCSANOW, &customflags);

	for(unsigned long i = izero; i < argc; ++i) {
		uint16_t num_patterns, num_channels, length, bpm, tempo;
		uint8_t pos, pat, row;
		char command;

		jump = false;
		create_context_from_file(&ctx, rate, argv[i]);

		if(ctx == NULL) {
			DEBUG("module file %s failed to load, skipping\n", argv[i]);
			continue;
		}

		xm_set_max_loop_count(ctx, loop);
		num_patterns = xm_get_number_of_patterns(ctx);
		num_channels = xm_get_number_of_channels(ctx);
		length = xm_get_module_length(ctx);

		const char* module_name = xm_get_module_name(ctx);
		const char* tracker_name = xm_get_tracker_name(ctx);
		if(module_name != NULL) printf("==> Playing: %s\n", module_name);
		if(tracker_name != NULL) printf("==> Tracker: %s\n", tracker_name);

		CHECK_ALSA_CALL(snd_pcm_prepare(device));

		while(!jump && (loop == 0 || xm_get_loop_count(ctx) < loop)) {
			switch(command = get_command()) {
			case ' ':
				paused = !paused;
				if(paused) {
					snd_pcm_pause(device, 1);
					fflush(stdout);
				} else {
					waspaused = true;
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
			case '<':
				if(pos > 0) {
					if(loop) {
						loop = !loop;
						xm_set_max_loop_count(ctx, loop); /* XXX */
					}
					xm_seek(ctx, pos - 1, 0, 0);
				}
				break;
			case '>':
				if(pos < length) {
					xm_seek(ctx, pos + 1, 0, 0);
				}
				break;
			default:
				if(command >= '0' && command <= '9') {
					uint16_t ch = 1 + (command - '0');
					if(ch > num_channels) break;
					xm_mute_channel(ctx, ch, !xm_mute_channel(ctx, ch, true));
					channel_map_until = samples + rate;
				} else if(command >= 'A' && command <= 'V') {
					uint16_t ch = 11 + (command - 'A');
					if(ch > num_channels) break;
					xm_mute_channel(ctx, ch, !xm_mute_channel(ctx, ch, true));
					channel_map_until = samples + rate;
				}
				break;
			}

			if(paused) {
				usleep(10000);
				continue;
			}

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
			xm_generate_samples(ctx, xmbuffer, period_size);
			play_floatbuffer(device, format, period_size, preamp, xmbuffer, alsabuffer);

			if(waspaused) {
				waspaused = false;
				snd_pcm_pause(device, 0);
			}

			fflush(stdout);
		}

		printf("\n");
		CHECK_ALSA_CALL(snd_pcm_drop(device));
		xm_free_context(ctx);
	}

	CHECK_ALSA_CALL(snd_pcm_close(device));
	return 0;
}
