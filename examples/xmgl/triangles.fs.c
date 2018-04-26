#version 330 core

in vec2 vposition;
out vec4 fragcolor;
uniform vec4 xmg; /* Time, AspectRatio, unused, unused */
uniform vec4 xmci; /* Chn, NChns, Instr, NInstrs */
uniform vec4 xmdata; /* log2(Freq), Vol, SecsSinceLastTrigger, Panning */

const float sqrt3_2 = .86602540378443864676f;
const float two_pi = 6.28318530717958647688f;

vec3 color(float hue) {
	hue *= two_pi;
	return vec3(.5) + .5 * vec3(cos(hue), cos(hue + two_pi / 3.f), cos(hue + 2.f * two_pi / 3.f));
}

void main() {
	if(xmdata.y < 0.01) discard;

	float theta = two_pi * xmdata.x / 6.f;
	float ct = cos(theta);
	float st = sin(theta);
	vec2 p = vec2(vposition.x - (xmdata.w - .5f) / 2.f, vposition.y) * 1.35f;
	p = mat2(ct, -st, st, ct) * p;

	p /= sqrt(xmdata.y);
	if(p.x < -.5) discard;
	if(sqrt3_2 * p.x + 1.5f * p.y > sqrt3_2) discard;
	if(sqrt3_2 * p.x - 1.5f * p.y > sqrt3_2) discard;

	float vol = smoothstep(0.0, 1.0, xmdata.y);
	float beat = exp(-10.0 * xmdata.z);
	fragcolor = vec4(mix(color(xmdata.x), vec3(1.0), .5 * beat), .1f + vol * .9f);
}
