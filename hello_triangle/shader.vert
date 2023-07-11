#version 450

layout(location = 0) out vec3 fragColor;

vec2 positions[] = {
    vec2(0.0, -0.5),
    vec2(0.5, 0.5),
    vec2(-0.5, 0.5),
};

vec3 colors[] = {
    vec3(1.0, 0.0, 0.0),
    vec3(0.0, 1.0, 0.0),
    vec3(0.0, 0.0, 1.0),
};

void main()
{
    // `gl_vertexIndex` doesn't work, I don't know why.
    gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);
    fragColor = colors[gl_VertexIndex];
}
