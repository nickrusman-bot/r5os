#version 450
layout(location = 0) in vec3 vNormal;
layout(location = 0) out vec4 outColor;
void main() {
	vec3 n = normalize(vNormal);
	float light = clamp(abs(dot(n, normalize(vec3(0.4, 0.8, 0.5)))), 0.2, 1.0);
	outColor = vec4(vec3(0.72, 0.32, 0.12) * light, 1.0);
}
