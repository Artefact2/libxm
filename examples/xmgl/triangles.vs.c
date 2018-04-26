#version 330 core

in vec2 position;
out vec2 vposition;
uniform vec4 xmg;
uniform vec4 xmci;
uniform vec4 xmdata;

void main() {
	vposition = position;
	float xr = 1.f / max(xmg.y, 1.f);
	float yr = 1.f / max(1.f / xmg.y, 1.f);

	float s = 1.f;
	while(floor(s / xr) * floor(s / yr) < xmci.w) s *= 1.1f;
	float nx = floor(s / xr);
	float ny = floor(s / yr);

	gl_Position = vec4(
		-1.f + 2.f * xr / s * (mod(s / xr, 1.f) / 2.f + (position.x + 1.f) / 2.f + mod(xmci.z - 1.f, nx)),
		-1.f + 2.f * yr / s * (mod(s / yr, 1.f) / 2.f + (position.y + 1.f) / 2.f + floor((xmci.z - 1.f) / nx)),
		xmci.x / xmci.y,
		1.f);
}
