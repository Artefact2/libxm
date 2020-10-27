/* Author: Romain "Artefact2" Dalmaso <artefact2@gmail.com> */

/* This program is free software. It comes without any warranty, to the
 * extent permitted by applicable law. You can redistribute it and/or
 * modify it under the terms of the Do What The Fuck You Want To Public
 * License, Version 2, as published by Sam Hocevar. See
 * http://sam.zoy.org/wtfpl/COPYING for more details. */

#include <alsa/asoundlib.h>

#define FATAL_ALSA_ERR(s, err) do {							\
		fprintf(stderr, "%s(%i) " s " : %s\n", __FILE__, __LINE__, snd_strerror((err))); \
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

#define CLAMP(f) (f > 1.f ? 1.f : (f < -1.f ? -1.f : f))

/* Create an ALSA device from command line arguments:
 *
 * --device default
 * --buffer-size 512
 * --period-size 256
 * --rate 48000
 * --format float|s16|s32
 *
 * Number of channels is always 2 (L,R).
 *
 * Use snd_pcm_writei() to play samples.
 *
 * @param argc number of arguments in argv
 * @param argv the program arguments
 * @param default_period_size the default period size, if not specified in argv
 * @param default_buffer_size the default buffer size, if not specified in argv
 * @param mode open mode for the ALSA device (SND_PCM_NONBLOCK, SND_PCM_ASYNC)
 *
 * @param out_device will receive the ALSA device
 * @param out_period_size will receive the chosen period size
 * @param out_rate will receive the chosen sample rate
 * @param out_format will receive the chosen sample format
 */
void init_alsa_device(int argc, char** argv,
                      size_t default_period_size,
                      size_t default_buffer_size,
                      int mode,
                      snd_pcm_t** out_device,
		      size_t* out_period_size,
		      unsigned int* out_rate,
                      snd_pcm_format_t* out_format) {
	snd_pcm_t* device;
	void* params;
	char* devicename = "default";
	size_t buffer_size = default_buffer_size;
	size_t period_size = default_period_size;
	const unsigned int channels = 2;
	unsigned int rate = 48000;
	snd_pcm_format_t format = SND_PCM_FORMAT_FLOAT;

	for(size_t i = 1; i < argc; ++i) {
		if(!strcmp(argv[i], "--")) {
			break;
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
				continue;
			}

			if(!strcmp(argv[i], "s16")) {
				format = SND_PCM_FORMAT_S16;
				continue;
			}

			if(!strcmp(argv[i], "s32")) {
				format = SND_PCM_FORMAT_S32;
				continue;
			}

			FATAL("%s: unknown format %s\n", argv[0], argv[i]);
		}

		break;
	}

	CHECK_ALSA_CALL(snd_pcm_open(&device, devicename, SND_PCM_STREAM_PLAYBACK, mode));
	CHECK_ALSA_CALL(snd_pcm_hw_params_malloc((snd_pcm_hw_params_t**)(&params)));
	CHECK_ALSA_CALL(snd_pcm_hw_params_any(device, params));
	CHECK_ALSA_CALL(snd_pcm_hw_params_set_access(device, params, SND_PCM_ACCESS_RW_INTERLEAVED));

	if(snd_pcm_hw_params_set_format(device, params, format) < 0
	   && snd_pcm_hw_params_set_format(device, params, format = SND_PCM_FORMAT_FLOAT) < 0
	   && snd_pcm_hw_params_set_format(device, params, format = SND_PCM_FORMAT_S32) < 0) {
		CHECK_ALSA_CALL(snd_pcm_hw_params_set_format(device, params, format = SND_PCM_FORMAT_S16));
	}

	if(snd_pcm_hw_params_set_rate(device, params, rate, 0) < 0
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
	CHECK_ALSA_CALL(snd_pcm_sw_params_set_start_threshold(device, params, buffer_size - period_size));
	CHECK_ALSA_CALL(snd_pcm_sw_params_set_avail_min(device, params, period_size));
	CHECK_ALSA_CALL(snd_pcm_sw_params(device, params));
	snd_pcm_sw_params_free(params);

	printf("Opened ALSA device: %s, %s, %i Hz, %lu/%lu\n",
	       devicename,
	       format == SND_PCM_FORMAT_FLOAT ? "float" : (format == SND_PCM_FORMAT_S32 ? "s32" : "s16"),
	       rate,
	       period_size,
	       buffer_size
		);

	*out_device = device;
	*out_period_size = period_size;
	*out_rate = rate;
	*out_format = format;
}

/* Play a buffer of float samples, optionally converting them and/or
 * applying preamp if needed. */
void play_floatbuffer(snd_pcm_t* device, snd_pcm_format_t format, size_t period_size, float preamp,
                   float* buffer, void* alsabuffer) {
	if(format == SND_PCM_FORMAT_FLOAT && preamp == 1.f) {
		CHECK_ALSA_CALL(snd_pcm_writei(device, buffer, period_size));
	} else {
		const unsigned int channels = 2;

		if(format == SND_PCM_FORMAT_S16) {
			for(size_t i = 0; i < period_size * channels; ++i) {
				((int16_t*)alsabuffer)[i] = (int16_t)(CLAMP((double)buffer[i] * preamp) * 32767.);
			}
		} else if(format == SND_PCM_FORMAT_S32) {
			for(size_t i = 0; i < period_size * channels; ++i) {
				((int32_t*)alsabuffer)[i] = (int32_t)(CLAMP((double)buffer[i] * preamp) * 2147483647.);
			}
		} else if(format == SND_PCM_FORMAT_FLOAT) {
			for(size_t i = 0; i < period_size * channels; ++i) {
				((float*)alsabuffer)[i] = buffer[i] * preamp;
			}
		}

		CHECK_ALSA_CALL(snd_pcm_writei(device, alsabuffer, period_size));
	}
}
