#version 330 core

in vec2 position;
out vec2 vposition;
uniform vec4 xmdata;

void main() {
	vposition = position;
	gl_Position = vec4(position, xmdata.x, 1.f);
}
