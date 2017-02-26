#version 330 core

in vec2 vposition;
out vec4 fragcolor;
uniform vec4 xmci; /* Chn, NChns, Instr, NInstrs */
uniform vec4 xmdata; /* log2(Freq), Vol, SecsSinceLastTrigger, unused */

vec3 color(float hue) {
	hue *= 6.28;
	return vec3(.5) + .5 * vec3(cos(hue), cos(hue + 2.0), cos(hue + 4.0));
}

void main() {
	float vol = smoothstep(0.0, 1.0, xmdata.y);
	float y = 2.0 * xmci.x / xmci.y - 1.0 - 1.0 / xmci.y;
	if(abs(vposition.y - y) > min(.05, .9 / xmci.y) * vol) discard;
	
	float x = (xmdata.x - 15.0) / 4.0;
	float beat = exp(-10.0 * xmdata.z);
	
	if(abs(x - vposition.x) > 0.05) {
		fragcolor = vec4(vec3(1.0), .02 * beat);
	} else {	
		fragcolor = vec4(mix(color(xmci.z / xmci.w), vec3(1.0), .5 * beat), .5 + .5 * vol);
	}
}
