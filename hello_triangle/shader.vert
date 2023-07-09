#version 450

layout(location = 0) out vec3 fragColor;

vec2 positions[3] = vec2[](
    vec2(0.0, -0.5),
    vec2(0.5, 0.5),
    vec2(-0.5, 0.5));

vec3 colors[3] = vec3[](
    vec3(1.0, 0.0, 0.0),
    vec3(0.0, 1.0, 0.0),
    vec3(0.0, 0.0, 1.0));

void main()
{
    // `gl_vertexIndex` doesn't work, I don't know why.
    gl_Position = vec4(positions[0], 0.0, 1.0);
    fragColor = colors[0];
}
