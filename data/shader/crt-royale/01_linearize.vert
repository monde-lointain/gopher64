#version 450

//  crt-royale-xm29plus pass 1 (linearize) vertex stage.
//  Ported from slang-shaders/.../crt-royale-first-pass-linearize-crt-gamma-bob-
//  fields.slang. Builds the fullscreen triangle from gl_VertexIndex (no vertex
//  buffer, like 00_lut.vert / data/shader/main.vert) instead of global.MVP *
//  Position, and forwards the same varyings the original vertex stage computed:
//  the half-texel-nudged texcoord, the vertical uv step, and the interlace flag.
//  Stream B.0.

//  Pass context for the shared includes: this is the first pass that manually
//  linearizes its (UNORM, gamma-encoded) LUT-output input.
#define FIRST_PASS
#define SIMULATE_CRT_ON_LCD

#include "compat.inc"
#include "params.inc"
#include "gamma.inc"
#include "scanline-functions.inc"

//  set=0/binding=0 Params UBO, bound to BOTH stages (crt_royale_contract.md).
//  Declare the full canonical block so std140 offsets match the C++ upload and
//  the fragment stage; this stage only reads SourceSize.
layout(std140, set = 0, binding = 0) uniform Params
{
    vec4 SourceSize;
    vec4 OriginalSize;
    vec4 OutputSize;
    uint FrameCount;
} params;

layout(location = 0) out vec2 tex_uv;
layout(location = 1) out vec2 uv_step;
layout(location = 2) out float interlaced;

void main()
{
    vec2 pos;
    if (gl_VertexIndex == 0)
        pos = vec2(-1.0, -1.0);
    else if (gl_VertexIndex == 1)
        pos = vec2(-1.0, 3.0);
    else
        pos = vec2(3.0, -1.0);

    gl_Position = vec4(pos, 0.0, 1.0);

    //  Map clip-space triangle to [0,1] texcoords with the original half-texel
    //  nudge, then derive a one-texel vertical step for bobbing.
    vec2 tex = pos * 0.5 + 0.5;
    tex_uv = tex * 1.00001;
    uv_step = vec2(1.0) / params.SourceSize.xy;

    //  Detect interlacing once per primitive (constant across the triangle):
    //  1.0 = interlaced, 0.0 = progressive.
    interlaced = float(is_interlaced(params.SourceSize.y));
}
