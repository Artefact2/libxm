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
#include <pthread.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <math.h>

static GLFWwindow* window;

static snd_pcm_t* device;
static size_t period_size;
static unsigned int rate;
static snd_pcm_format_t format;

static xm_context_t* xmctx;
static uint8_t loop = 0;

static pthread_t audiothread;
static pthread_mutex_t xmsem;
static uint16_t channels, instruments;

static GLuint vertexn, elementn, progn, varrayn, xmdatau;
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
	FATAL("Usage:\n" "\t%s --help\n"
		  "\t\tShow this message.\n"
	      "\t%s [--preamp 1.0] [--device default] [--buffer-size 4096] [--period-size 2048] [--rate 48000] [--format float|s16|s32] [--] <filename>\n"
		  "\t\tPlay this module.\n",
		  progname, progname);
}

void* play_audio(void* p) {
	float xmbuffer[period_size];
	float alsabuffer[period_size];
	
	while(true) {
		pthread_mutex_lock(&xmsem);
		xm_generate_samples(xmctx, xmbuffer, period_size >> 1);
		pthread_mutex_unlock(&xmsem);
		play_floatbuffer(device, format, period_size, 1.f, xmbuffer, alsabuffer);
	}

	return p;
}

void setup(int argc, char** argv) {
	glfwInit();
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
	window = glfwCreateWindow(800, 800, argv[0], NULL, NULL);
	glfwMakeContextCurrent(window);
	glfwSwapInterval(1);

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
	glUseProgram(progn);

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE);
	
	init_alsa_device(argc, argv, &device, &period_size, &rate, &format);
	create_context_from_file(&xmctx, rate, argv[argc - 1]);
	xm_set_max_loop_count(xmctx, 1);
	channels = xm_get_number_of_channels(xmctx);
	instruments = xm_get_number_of_instruments(xmctx);
	CHECK_ALSA_CALL(snd_pcm_prepare(device));
	pthread_mutex_init(&xmsem, NULL);
	pthread_create(&audiothread, NULL, play_audio, NULL);
}

void teardown(void) {
	void* retval;

	snd_pcm_pause(device, 1);
	pthread_cancel(audiothread);
	pthread_join(audiothread, &retval);
	pthread_mutex_destroy(&xmsem);
	snd_pcm_drop(device);
	snd_pcm_close(device);
	xm_free_context(xmctx);
	
	glfwDestroyWindow(window);
	glfwTerminate();
}

void render(void) {
	int width, height, active;
	float ratio;

	glfwGetFramebufferSize(window, &width, &height);
	ratio = (float)width / (float)height;

	glViewport(0, 0, width, height);
	glClear(GL_COLOR_BUFFER_BIT);
	
	pthread_mutex_lock(&xmsem);
	loop = xm_get_loop_count(xmctx);

	for(uint16_t i = 1; i <= channels; ++i) {
		active = xm_is_channel_active(xmctx, i);
		glUniform4f(xmdatau,
		            (float)i / (float)channels,
		            active ? (float)xm_get_instrument_of_channel(xmctx, i) / (float)instruments : 0.f,
		            active ? log2f(xm_get_frequency_of_channel(xmctx, i)) : 0.f,
		            active ? xm_get_volume_of_channel(xmctx, i) : 0.f
			);
		glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_BYTE, 0);
	}
	pthread_mutex_unlock(&xmsem);
}

int main(int argc, char** argv) {
	if(argc < 2) {
		usage(argv[0]);
		exit(1);
	}
	
	setup(argc, argv);
	
	while(!glfwWindowShouldClose(window)) {
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
