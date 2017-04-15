/* Author: Romain "Artefact2" Dalmaso <artefact2@gmail.com> */

/* This program is free software. It comes without any warranty, to the
 * extent permitted by applicable law. You can redistribute it and/or
 * modify it under the terms of the Do What The Fuck You Want To Public
 * License, Version 2, as published by Sam Hocevar. See
 * http://sam.zoy.org/wtfpl/COPYING for more details. */

#define _DEFAULT_SOURCE
#define GL_GLEXT_PROTOTYPES

#include "../testprog.h"
#include <jack/jack.h>
#include <GLFW/glfw3.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <math.h>

static GLFWwindow* window;
static int interval = 0, width = 1024, height = 1024;
static bool fullscreen = false;

static bool autoconnect = true;
static bool paused = false;

static jack_client_t* client;
static jack_port_t* left;
static jack_port_t* right;
static unsigned int rate;

static xm_context_t* xmctx;
static uint8_t loop = 1;
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
	      "\t%s [--fullscreen] [--width 1024] [--height 1024] [--interval 0] [--no-autoconnect] [--paused] [--loop] [--] <filename>\n"
		  "\t\tPlay this module.\n\nKey bindings:\n\t<ESC>/q quit\n\t<Space> play/pause\n\tL toggle looping\n",
		  progname, progname);
}

int jack_process(jack_nframes_t nframes, void* arg) {
	/* XXX: be more clever with buffer size: jack_get_buffer_size(), etc. */	
	float buffer[nframes << 1];
	float* lbuf = jack_port_get_buffer(left, nframes);
	float* rbuf = jack_port_get_buffer(right, nframes);;

	if(paused) {
		memset(buffer, 0, sizeof(buffer));
	} else {
		xm_generate_samples(xmctx, buffer, nframes);
		xm_get_position(xmctx, NULL, NULL, NULL, &samples);
	}

	for(size_t i = 0; i < nframes; ++i) {
		lbuf[i] = buffer[i << 1];
		rbuf[i] = buffer[(i << 1) + 1];
	}

	return 0;
}

void keyfun(GLFWwindow* window, int key, int scancode, int action, int mods) {
	if(action != GLFW_PRESS) return;
	
	if(key == GLFW_KEY_ESCAPE) {
		glfwSetWindowShouldClose(window, GL_TRUE);
	}
}

void charfun(GLFWwindow* window, unsigned int codepoint) {
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

	client = jack_client_open("xmgl", JackNullOption, NULL);
	if(client == NULL) {
		FATAL("jack_client_open() returned NULL\n");
	}
	printf("JACK client name: %s\n", jack_get_client_name(client));
	printf("JACK buffer size: %d frames\n", jack_get_buffer_size(client));
	
	jack_set_process_callback(client, jack_process, NULL);
	rate = jack_get_sample_rate(client);
	printf("Using JACK sample rate: %d Hz\n", rate);

	left = jack_port_register(client, "Left", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput | JackPortIsTerminal, 0);
	right = jack_port_register(client, "Right", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput | JackPortIsTerminal, 0);
        
	create_context_from_file(&xmctx, rate, argv[filenameidx]);
	if(xmctx == NULL) exit(1);
	channels = xm_get_number_of_channels(xmctx);
	instruments = xm_get_number_of_instruments(xmctx);

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
}

void teardown(void) {
	jack_client_close(client);
	xm_free_context(xmctx);
	
	glfwDestroyWindow(window);
	glfwTerminate();
}

void render(void) {
	int width, height, active;

	glfwGetFramebufferSize(window, &width, &height);
	glViewport(0, 0, width, height);
	glClear(GL_COLOR_BUFFER_BIT);

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
