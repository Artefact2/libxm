/* Author: Romain "Artefact2" Dalmaso <artefact2@gmail.com> */

/* This program is free software. It comes without any warranty, to the
 * extent permitted by applicable law. You can redistribute it and/or
 * modify it under the terms of the Do What The Fuck You Want To Public
 * License, Version 2, as published by Sam Hocevar. See
 * http://sam.zoy.org/wtfpl/COPYING for more details. */

#define _DEFAULT_SOURCE
#define GL_GLEXT_PROTOTYPES

#include "../testprog.h"
#include "../alsa_common.h"
#include <GLFW/glfw3.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <math.h>

static GLFWwindow* window;
static int interval = 0, width = 1024, height = 1024;
static bool fullscreen = false;

static snd_pcm_t* device;
static size_t period_size;
static unsigned int rate;
static snd_pcm_format_t format;

static xm_context_t* xmctx;
static uint8_t loop = 0;
static uint64_t samples = 0;

static uint16_t channels, instruments;

static GLuint vertexn, elementn, progn, varrayn, xmdatau, xmciu;
const GLfloat vertices[] = {
	-1.f, -1.f,
	1.f, -1.f,
	1.f, 1.f,
	-1.f, 1.f,
};
const GLbyte indices[] = {
	0, 1, 2,
	2, 3, 0
};

void usage(char* progname) {
	FATAL("Usage:\n" "\t%s\n"
		  "\t\tShow this message.\n"
	      "\t%s [--preamp 1.0] [--device default] [--buffer-size 2048] [--period-size 64] [--rate 48000] [--format float|s16|s32] [--fullscreen] [--width 1024] [--height 1024] [--interval 0] [--] <filename>\n"
		  "\t\tPlay this module.\n",
		  progname, progname);
}

void play_audio(float* xmbuffer, float* alsabuffer) {
	snd_pcm_sframes_t avail;

	while((avail = snd_pcm_avail_update(device)) >= period_size) {
		xm_generate_samples(xmctx, xmbuffer, period_size >> 1);
		xm_get_position(xmctx, NULL, NULL, NULL, &samples);
		play_floatbuffer(device, format, period_size, 1.f, xmbuffer, alsabuffer);
	}
}

void setup(int argc, char** argv) {
	size_t filenameidx = 1;
	
	for(size_t i = 1; i < argc; ++i) {
		if(!strcmp(argv[i], "--")) {
			filenameidx = i+1;
			break;
		}
		
		if(!strcmp(argv[i], "--width")) {
			if(argc == i+1) FATAL("%s: expected argument after %s\n", argv[0], argv[i]);
			width = strtol(argv[i+1], NULL, 0);
			++i;
			continue;
		}
		
		if(!strcmp(argv[i], "--height")) {
			if(argc == i+1) FATAL("%s: expected argument after %s\n", argv[0], argv[i]);
			height = strtol(argv[i+1], NULL, 0);
			++i;
			continue;
		}
		
		if(!strcmp(argv[i], "--interval")) {
			if(argc == i+1) FATAL("%s: expected argument after %s\n", argv[0], argv[i]);
			interval = strtol(argv[i+1], NULL, 0);
			++i;
			continue;
		}

		if(!strcmp(argv[i], "--fullscreen")) {
			fullscreen = true;
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
        
	GLuint vsn, fsn;
	const char* src;
	GLint status;
	progn = glCreateProgram();
	
	#include "vs.h"
	src = vs;
	vsn = glCreateShader(GL_VERTEX_SHADER);
	glAttachShader(progn, vsn);
	glShaderSource(vsn, 1, &src, NULL);
	glCompileShader(vsn);
	glGetShaderiv(vsn, GL_COMPILE_STATUS, &status);
	if(status != GL_TRUE) {
		char msg[1024];
		glGetShaderInfoLog(vsn, 1024, NULL, msg);
		fprintf(stderr, "VS:%s", msg);
		exit(1);
	}

	#include "fs.h"
	src = fs;
	fsn = glCreateShader(GL_FRAGMENT_SHADER);
	glAttachShader(progn, fsn);
	glShaderSource(fsn, 1, &src, NULL);
	glCompileShader(fsn);
	glGetShaderiv(fsn, GL_COMPILE_STATUS, &status);
	if(status != GL_TRUE) {
		char msg[1024];
		glGetShaderInfoLog(fsn, 1024, NULL, msg);
		fprintf(stderr, "FS:%s", msg);
		exit(1);
	}
	
	glLinkProgram(progn);
	glGetProgramiv(progn, GL_LINK_STATUS, &status);
	if(status != GL_TRUE) {
		char msg[1024];
		glGetProgramInfoLog(progn, 1024, NULL, msg);
		fprintf(stderr, "PROG:%s", msg);
		exit(1);
	}

	xmdatau = glGetUniformLocation(progn, "xmdata");
	xmciu = glGetUniformLocation(progn, "xmci");
	glUseProgram(progn);

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE);
	
	init_alsa_device(argc, argv, 64, 2048, SND_PCM_NONBLOCK, &device, &period_size, &rate, &format);
	create_context_from_file(&xmctx, rate, argv[filenameidx]);
	if(xmctx == NULL) exit(1);
	xm_set_max_loop_count(xmctx, 1);
	channels = xm_get_number_of_channels(xmctx);
	instruments = xm_get_number_of_instruments(xmctx);
	CHECK_ALSA_CALL(snd_pcm_prepare(device));
}

void teardown(void) {
	snd_pcm_drop(device);
	snd_pcm_close(device);
	xm_free_context(xmctx);
	
	glfwDestroyWindow(window);
	glfwTerminate();
}

void render(void) {
	int width, height, active;

	glfwGetFramebufferSize(window, &width, &height);
	glViewport(0, 0, width, height);
	glClear(GL_COLOR_BUFFER_BIT);
	
	loop = xm_get_loop_count(xmctx);

	for(uint16_t i = 1; i <= channels; ++i) {
		active = xm_is_channel_active(xmctx, i);
		glUniform4f(xmciu,
		            (float)i,
		            (float)channels,
		            active ? (float)xm_get_instrument_of_channel(xmctx, i) : 0.f,
		            (float)instruments
			);
		glUniform4f(xmdatau,
		            active ? log2f(xm_get_frequency_of_channel(xmctx, i)) : 0.f,
		            active ? xm_get_volume_of_channel(xmctx, i) : 0.f,
		            (float)(samples - xm_get_latest_trigger_of_channel(xmctx, i)) / (float)rate,
		            0.f
			);
		glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_BYTE, 0);
	}
}

int main(int argc, char** argv) {	
	setup(argc, argv);
	
	float xmbuffer[period_size];
	float alsabuffer[period_size];
	
	while(!glfwWindowShouldClose(window)) {
		play_audio(xmbuffer, alsabuffer);
		render();

		glfwSwapBuffers(window);
		glfwPollEvents();

		if(glfwGetKey(window, GLFW_KEY_ESCAPE) || loop > 0) {
			glfwSetWindowShouldClose(window, GL_TRUE);
		}
	}

	teardown();
	return 0;
}
