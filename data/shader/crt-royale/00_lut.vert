#version 450

// crt-royale-xm29plus pass 0 (LUT) vertex stage.
// Generates a fullscreen triangle from gl_VertexIndex (no vertex buffer), like
// data/shader/main.vert, and forwards the texcoord with the source LUT.slang's
// half-texel nudge. Stream B.1.

layout(location = 0) out vec2 vTexCoord;

void main() {
    vec2 pos;
    if (gl_VertexIndex == 0) {
        pos = vec2(-1.0, -1.0);
    } else if (gl_VertexIndex == 1) {
        pos = vec2(-1.0, 3.0);
    } else {
        pos = vec2(3.0, -1.0);
    }
    gl_Position = vec4(pos, 0.0, 1.0);
    vTexCoord = (pos * 0.5 + 0.5) * 1.0001;
}
