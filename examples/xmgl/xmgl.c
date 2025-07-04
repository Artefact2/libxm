/* Author: Romain "Artefact2" Dalmaso <artefact2@gmail.com> */

/* This program is free software. It comes without any warranty, to the
 * extent permitted by applicable law. You can redistribute it and/or
 * modify it under the terms of the Do What The Fuck You Want To Public
 * License, Version 2, as published by Sam Hocevar. See
 * http://sam.zoy.org/wtfpl/COPYING for more details. */

#define _DEFAULT_SOURCE
#define GL_GLEXT_PROTOTYPES

#include <xm.h>
#include <jack/jack.h>
#include <GLFW/glfw3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <assert.h>
#include <limits.h>

#define MAX_CHANNELS 32
#define NUM_TIMING 32  /* keep this many channel timing infos for latency
                          compensation */
#define TIMING_FRAME_SIZE 256 /* record channel timing info every X generated
                                 audio frames */
#define GL_FRAMES_AVG_COUNT 60 /* measure display latency over this many video
                                  frames (running average) */
#define AOT_TIMING_FRAMES 4 /* pre-generate this many timing frames ahead of
                               time, for latency compensation */

static const char hlines_vs[] = {
#embed "hlines.vs.c"
};
static const char hlines_fs[] = {
#embed "hlines.fs.c"
};

static const char triangles_vs[] = {
#embed "triangles.vs.c"
};
static const char triangles_fs[] = {
#embed "triangles.fs.c"
};

#define CREATE_SHADER(RET, TYPE, SRC) do {                              \
		GLint status; \
		const char* src = SRC; \
		(RET) = glCreateShader(TYPE); \
		glShaderSource((RET), 1, &src, NULL); \
		glCompileShader(RET); \
		glGetShaderiv((RET), GL_COMPILE_STATUS, &status); \
		if(status != GL_TRUE) { \
			char msg[1024]; \
			glGetShaderInfoLog((RET), 1024, NULL, msg); \
			fprintf(stderr, "%d:%s", TYPE, msg); \
			exit(2); \
		} \
	} while(0)

#define CREATE_PROGRAM(RET, NAME) do {                                  \
		GLuint vsn, fsn; \
		GLint status; \
		CREATE_SHADER(vsn, GL_VERTEX_SHADER, NAME ## _vs); \
		CREATE_SHADER(fsn, GL_FRAGMENT_SHADER, NAME ## _fs); \
		(RET) = glCreateProgram(); \
		glAttachShader((RET), vsn); \
		glAttachShader((RET), fsn); \
		glLinkProgram(RET); \
		glGetProgramiv((RET), GL_LINK_STATUS, &status); \
		if(status != GL_TRUE) { \
			char msg[1024]; \
			glGetProgramInfoLog((RET), 1024, NULL, msg); \
			fprintf(stderr, "prog:%s\n", msg); \
			exit(3); \
		} \
	} while(0)

static GLFWwindow* window;
static int interval = 1, width = 1024, height = 1024;
static bool fullscreen = false;

static bool autoconnect = true;
static bool paused = false;

static jack_client_t* client;
static jack_port_t* left;
static jack_port_t* right;
static jack_nframes_t rate;
static jack_nframes_t time_basis = 0; /* absolute time (from jack) at which we
                                         started rendering the first audio
                                         frames from libxm */
static jack_nframes_t jack_latency = 0;

static xm_context_t* xmctx;
static uint8_t loop = 1;

static uint8_t channels, instruments;

static GLuint vertexn, elementn, programs[2], curprog = 0, varrayn;
static GLint xmgu, xmdatau, xmciu;

static const GLfloat vertices[] = {
	-1.f, -1.f,
	1.f, -1.f,
	1.f, 1.f,
	-1.f, 1.f,
};
static const GLbyte indices[] = {
	0, 1, 2,
	2, 3, 0
};

struct channel_timing_info {
	uint32_t latest_trigger; // in audio frames
	float frequency;
	float volume;
	float panning;
	uint8_t instrument;
	bool active;
	char __pad[2];
};

struct module_timing_info {
	/* ring buffer */
	struct channel_timing_info channels[NUM_TIMING][MAX_CHANNELS];
	/* timestamps in audio frames of each cti */
	uint32_t audio_frames[NUM_TIMING];
	size_t latest_cti_idx;
};

static struct module_timing_info mti;
static float aot_left[AOT_TIMING_FRAMES][TIMING_FRAME_SIZE];
static float aot_right[AOT_TIMING_FRAMES][TIMING_FRAME_SIZE];
static size_t num_aot_timing_frames = 0;
static size_t first_aot_timing_frame_idx = 0;

static jack_nframes_t glf_total = 0;
static jack_nframes_t glf_times[GL_FRAMES_AVG_COUNT];
static jack_nframes_t glf_last = 0;
static short unsigned int gl_framecount = 0;
static jack_nframes_t gl_latency = 0;

static void usage(const char* progname) {
	fprintf(stderr,
	        "Usage:\n" "\t%s\n"
	        "\t\tShow this message.\n"
	        "\t%s [--fullscreen] [--width 1024] [--height 1024]"
	        " [--interval 1] [--no-autoconnect] [--paused] "
	        "[--loop] [--program 0] [--] <filename>\n"
	        "\t\tPlay this module.\n\nKey bindings:\n"
	        "\t<ESC>/q quit\n"
	        "\t<Space> play/pause\n"
	        "\tL toggle looping\n"
	        "\t</> change programs\n",
	        progname, progname);
}

static void generate_timing_frame(float* lbuf, float* rbuf, jack_nframes_t tframes) {
	mti.latest_cti_idx = (mti.latest_cti_idx + 1) % NUM_TIMING;
	assert(tframes <= UINT16_MAX);
	xm_generate_samples_noninterleaved(xmctx, lbuf, rbuf, (uint16_t)tframes);
	xm_get_position(xmctx, NULL, NULL, NULL, &(mti.audio_frames[mti.latest_cti_idx]));
	for(uint8_t k = 1; k <= channels; ++k) {
		struct channel_timing_info* chn = &(mti.channels[mti.latest_cti_idx][k-1]);
		chn->active = xm_is_channel_active(xmctx, k);
		chn->instrument = xm_get_instrument_of_channel(xmctx, k);
		chn->frequency = xm_get_frequency_of_channel(xmctx, k);
		chn->volume = xm_get_volume_of_channel(xmctx, k);
		chn->panning = xm_get_panning_of_channel(xmctx, k);
		chn->latest_trigger = xm_get_latest_trigger_of_channel(xmctx, k);
	}
}

static int jack_process_callback(jack_nframes_t nframes, __attribute__((unused)) void* arg) {
	float* lbuf = jack_port_get_buffer(left, nframes);
	float* rbuf = jack_port_get_buffer(right, nframes);

	if(time_basis == 0) {
		time_basis = jack_time_to_frames(client, jack_get_time());
	}

	if(paused) {
		memset(lbuf, 0, nframes * sizeof(float));
		memset(rbuf, 0, nframes * sizeof(float));
		time_basis += nframes;
		return 0;
	}

	jack_nframes_t tframes;
	if(nframes >= TIMING_FRAME_SIZE) {
		assert(nframes % TIMING_FRAME_SIZE == 0);
		tframes = TIMING_FRAME_SIZE;
	} else {
		tframes = nframes;
	}

	for(size_t off = 0; off < nframes; off += tframes) {
		if(num_aot_timing_frames > 0) {
			memcpy(lbuf, aot_left[first_aot_timing_frame_idx], tframes * sizeof(float));
			memcpy(rbuf, aot_right[first_aot_timing_frame_idx], tframes * sizeof(float));
			num_aot_timing_frames -= 1;
			first_aot_timing_frame_idx = (first_aot_timing_frame_idx + 1) % AOT_TIMING_FRAMES;
		} else {
			generate_timing_frame(lbuf, rbuf, tframes);
		}

		lbuf = &(lbuf[tframes]);
		rbuf = &(rbuf[tframes]);
	}

	while(num_aot_timing_frames < AOT_TIMING_FRAMES) {
		generate_timing_frame(aot_left[(first_aot_timing_frame_idx + num_aot_timing_frames) % AOT_TIMING_FRAMES],
		                      aot_right[(first_aot_timing_frame_idx + num_aot_timing_frames) % AOT_TIMING_FRAMES],
		                      tframes);
		num_aot_timing_frames += 1;
	}

	return 0;
}

static void jack_latency_callback(jack_latency_callback_mode_t mode, __attribute__((unused)) void* arg) {
	if(mode == JackCaptureLatency) return;

	jack_latency_range_t range;
	jack_port_get_latency_range(left, mode, &range);
	if(jack_latency == range.max) return;

	printf("JACK output latency: %d/%d frames\n", range.min, range.max);
	jack_latency = range.max;
}

static void switch_program(void) {
	xmgu = glGetUniformLocation(programs[curprog], "xmg");
	xmdatau = glGetUniformLocation(programs[curprog], "xmdata");
	xmciu = glGetUniformLocation(programs[curprog], "xmci");
	glUseProgram(programs[curprog]);
}

static void keyfun(GLFWwindow* window, int key, __attribute__((unused)) int scancode, int action, __attribute__((unused)) int mods) {
	if(action != GLFW_PRESS) return;

	if(key == GLFW_KEY_ESCAPE) {
		glfwSetWindowShouldClose(window, GL_TRUE);
	}
}

static void charfun(GLFWwindow* window, unsigned int codepoint) {
	if(codepoint == 'q' || codepoint == 'Q') {
		glfwSetWindowShouldClose(window, GL_TRUE);
	} else if(codepoint == ' ') {
		paused = !paused;
	} else if(codepoint == 'l' || codepoint == 'L') {
		if(loop > 0) {
			printf("Looping: ON\n");
			loop = 0;
		} else {
			printf("Looping: OFF\n");
			loop = xm_get_loop_count(xmctx) + 1;
		}
	} else if(codepoint == '<') {
		curprog = (sizeof(programs) / sizeof(programs[0]) + curprog - 1) % (sizeof(programs) / sizeof(programs[0]));
		switch_program();
	} else if(codepoint == '>') {
		curprog = (curprog + 1) % (sizeof(programs) / sizeof(programs[0]));
		switch_program();
	}
}

static xm_context_t* create_context_from_file(const char* filename) {
	xm_context_t* ctx = NULL;
	char* xm_data = NULL;
	FILE* xmfile = NULL;
	char* prescan_data;
	long size;

	xmfile = fopen(filename, "rb");
	if(xmfile == NULL) {
		perror("fopen");
		goto end;
	}

	if(fseek(xmfile, 0, SEEK_END)) {
		perror("fseek");
		goto end;
	}

	size = ftell(xmfile);
	if(size < 0) {
		perror("ftell");
		goto end;
	}
	static_assert(SIZE_MAX >= UINT32_MAX);
	if(size > UINT32_MAX) {
		fprintf(stderr, "module file too large\n");
		goto end;
	}
	xm_data = malloc((size_t)size);
	if(xm_data == NULL) goto end;

	rewind(xmfile);
	if(!fread(xm_data, (size_t)size, 1, xmfile)) {
		perror("fread");
		goto end;
	}

	prescan_data = alloca(XM_PRESCAN_DATA_SIZE);
	xm_prescan_data_t* p = (xm_prescan_data_t*)prescan_data;
	if(xm_prescan_module(xm_data, (uint32_t)size, p) == false) {
		fprintf(stderr, "xm_prescan_module() failed\n");
		goto end;
	}

	uint32_t ctx_size = xm_size_for_context(p);
	char* ctx_data = malloc(ctx_size);
	if(ctx_data == NULL) goto end;
	ctx = xm_create_context(ctx_data, p, xm_data, (uint32_t)size);

 end:
	if(xm_data) free(xm_data);
	if(xmfile) fclose(xmfile);
	return ctx;
}

static int parse_int(char* s) {
	char* end;
	long a = strtol(s, &end, 0);
	static_assert(LONG_MAX >= INT_MAX);
	if(*s == 0 || *end != 0 || a >= INT_MAX || a <= INT_MIN) {
		fprintf(stderr, "error parsing integer: '%s'\n", s);
		exit(1);
	}
	return (int)a;
}

static void setup(int argc, char** argv) {
	int filenameidx = 1;

	for(int i = 1; i < argc; ++i) {
		if(!strcmp(argv[i], "--")) {
			filenameidx = i+1;
			break;
		}

		if(!strcmp(argv[i], "--width")) {
			if(argc == i+1) {
				fprintf(stderr, "%s: expected argument after %s\n", argv[0], argv[i]);
				exit(1);
			}
			width = parse_int(argv[i+1]);
			++i;
			continue;
		}

		if(!strcmp(argv[i], "--height")) {
			if(argc == i+1) {
				fprintf(stderr, "%s: expected argument after %s\n", argv[0], argv[i]);
				exit(1);
			}
			height = parse_int(argv[i+1]);
			++i;
			continue;
		}

		if(!strcmp(argv[i], "--interval")) {
			if(argc == i+1) {
				fprintf(stderr, "%s: expected argument after %s\n", argv[0], argv[i]);
				exit(1);
			}
			interval = parse_int(argv[i+1]);
			++i;
			continue;
		}

		if(!strcmp(argv[i], "--fullscreen")) {
			fullscreen = true;
			continue;
		}

		if(!strcmp(argv[i], "--no-autoconnect")) {
			autoconnect = false;
			continue;
		}

		if(!strcmp(argv[i], "--paused")) {
			paused = true;
			continue;
		}

		if(!strcmp(argv[i], "--loop")) {
			loop = 0;
			continue;
		}

		if(!strcmp(argv[i], "--program")) {
			if(argc == i+1) {
				fprintf(stderr, "%s: expected argument after %s\n", argv[0], argv[i]);
				exit(1);
			}
			int x = parse_int(argv[i+1]);
			curprog = x >= 0 ? (unsigned int)x : 0;
			curprog %= sizeof(programs) / sizeof(programs[0]);
			++i;
			continue;
		}

		filenameidx = i;
		break;
	}

	if(filenameidx+1 != argc) {
		usage(argv[0]);
		exit(1);
	}

	glfwInit();
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
	window = glfwCreateWindow(width, height, argv[0], fullscreen ? glfwGetPrimaryMonitor() : NULL, NULL);
	glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_HIDDEN);
	glfwSetInputMode(window, GLFW_STICKY_KEYS, GL_TRUE);
	glfwSetKeyCallback(window, keyfun);
	glfwSetCharCallback(window, charfun);
	glfwMakeContextCurrent(window);
	glfwSwapInterval(interval);

	printf("Using GL renderer: %s %s\n", glGetString(GL_VENDOR), glGetString(GL_RENDERER));

	glGenBuffers(1, &vertexn);
	glBindBuffer(GL_ARRAY_BUFFER, vertexn);
	glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

	glGenBuffers(1, &elementn);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, elementn);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

	glGenVertexArrays(1, &varrayn);
	glBindVertexArray(varrayn);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);

	/* XXX: wtf? */
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, elementn);

	CREATE_PROGRAM(programs[0], hlines);
	CREATE_PROGRAM(programs[1], triangles);
	switch_program();

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE);

	client = jack_client_open("xmgl", JackNullOption, NULL);
	if(client == NULL) {
		fprintf(stderr, "jack_client_open() returned NULL\n");
		exit(1);
	}
	printf("JACK client name: %s\n", jack_get_client_name(client));
	printf("JACK buffer size: %d frames\n", jack_get_buffer_size(client));

	jack_set_process_callback(client, jack_process_callback, NULL);
	jack_set_latency_callback(client, jack_latency_callback, NULL);
	rate = jack_get_sample_rate(client);
	if(rate > UINT16_MAX) {
		fprintf(stderr, "unsupported sample rate (%u > %u)\n",
		        rate, UINT16_MAX);
		exit(1);
	}
	printf("Using JACK sample rate: %u Hz\n", rate);

	left = jack_port_register(client, "Left", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput | JackPortIsTerminal, 0);
	right = jack_port_register(client, "Right", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput | JackPortIsTerminal, 0);

	xmctx = create_context_from_file(argv[filenameidx]);
	if(xmctx == NULL) exit(1);
	xm_set_sample_rate(xmctx, (uint16_t)rate);
	channels = xm_get_number_of_channels(xmctx);
	if(channels > MAX_CHANNELS) {
		fprintf(stderr, "too many channels (%u > %u)\n",
		        channels, MAX_CHANNELS);
		exit(1);
	}
	instruments = xm_get_number_of_instruments(xmctx);
	mti.latest_cti_idx = NUM_TIMING - 1;

	jack_activate(client);

	if(autoconnect) {
		const char** ports = jack_get_ports(client, NULL, NULL, JackPortIsPhysical | JackPortIsInput | JackPortIsTerminal);
		if(ports != NULL) {
			if(ports[0] != NULL && ports[1] != NULL) {
				char ipname[16];

				snprintf(ipname, 16, "%s:Left", jack_get_client_name(client));
				printf("Autoconnecting %s -> %s\n", ipname, ports[0]);
				jack_connect(client, "xmgl:Left", ports[0]);

				snprintf(ipname, 16, "%s:Right", jack_get_client_name(client));
				printf("Autoconnecting %s -> %s\n", ipname, ports[1]);
				jack_connect(client, "xmgl:Right", ports[1]);
			}

			jack_free(ports);
		}
	}

	memset(glf_times, 0, GL_FRAMES_AVG_COUNT * sizeof(jack_nframes_t));
}

static void teardown(void) {
	jack_client_close(client);
	glfwDestroyWindow(window);
	glfwTerminate();
}

static void render(void) {
	int width, height, active;

	glfwGetFramebufferSize(window, &width, &height);
	glViewport(0, 0, width, height);
	glClear(GL_COLOR_BUFFER_BIT);

	struct channel_timing_info* chns = NULL;
	uint64_t frames;
	jack_nframes_t now = jack_time_to_frames(client, jack_get_time());

	/* when jack_latency > gl_latency, we display mtis from the past */
	/* when jack_latency < gl_latency, we display mtis from the future  */
	jack_nframes_t target = now - time_basis - jack_latency + gl_latency;

	/* find channel timing infos with a timestamp closest to target */
	size_t j = mti.latest_cti_idx;

	while(mti.audio_frames[j] > target) {
		if(j == 0) {
			j = NUM_TIMING - 1;
		} else {
			j -= 1;
		}
		if(j == mti.latest_cti_idx) {
			break;
		}
	}
	chns = &(mti.channels[j][0]);
	frames = mti.audio_frames[j];

	glUniform4f(xmgu, (float)frames / (float)rate, (float)width / (float)height, 0.f, 0.f);

	for(uint16_t i = 0; i < channels; ++i) {
		active = chns[i].active;
		glUniform4f(xmciu,
		            (float)i,
		            (float)channels,
		            active ? (float)chns[i].instrument : 0.f,
		            (float)instruments
			);
		glUniform4f(xmdatau,
		            active ? log2f(chns[i].frequency) : 0.f,
		            active ? chns[i].volume : 0.f,
		            (float)(frames - chns[i].latest_trigger) / (float)rate,
		            active ? chns[i].panning : .5f
			);
		glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_BYTE, 0);
	}

	if(glf_last == 0) {
		glf_last = now;
	}

	glf_total -= glf_times[gl_framecount];
	glf_times[gl_framecount] = now - glf_last;
	glf_total += now - glf_last;
	glf_last = now;
	gl_latency = glf_total / GL_FRAMES_AVG_COUNT; // assume 1 display frame latency on average
	gl_framecount += 1;
	gl_framecount %= GL_FRAMES_AVG_COUNT;
}

int main(int argc, char** argv) {
	setup(argc, argv);

	while(!glfwWindowShouldClose(window)) {
		render();

		glfwSwapBuffers(window);
		glfwPollEvents();

		if(loop > 0 && xm_get_loop_count(xmctx) >= loop) {
			glfwSetWindowShouldClose(window, GL_TRUE);
		}
	}

	teardown();
	return 0;
}
