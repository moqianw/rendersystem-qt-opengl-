#version 450 core

in vec3 vColor;

out vec4 fragColor;

uniform vec3 uColorTint;

void main() {
    fragColor = vec4(vColor * uColorTint, 1.0);
}
