#version 330 core

in vec2 vposition;
out vec4 fragcolor;
uniform vec4 xmdata; /* Chn/NChns, Instr/NInstrs, log2(Freq), Vol */

/* Stolen from Sam Hocevar. See:
 * https://gamedev.stackexchange.com/a/59808 */
vec3 hsv2rgb(vec3 c) {
	vec4 K = vec4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
	vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
	return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
}

void main() {
	float r = length(vposition);
	float oct = xmdata.z / 20.f;
	vec2 target = vec2(r * cos(6.28318530717958647688f * xmdata.z), r *sin(6.28318530717958647688f * xmdata.z));
	float dist = length(vposition - target);

	if(dist > .2 * r) discard;
	if(abs(r - oct) > 0.02) discard;
	
	fragcolor = vec4(
		hsv2rgb(vec3(xmdata.y * 1445.683229480096030348f, 1.f, .5f)),
		(xmdata.w / 2.f)
		);
}
