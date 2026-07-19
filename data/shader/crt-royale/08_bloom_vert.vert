#version 450

//  crt-royale-xm29plus pass 8 (bloom-vertical) vertex stage.
//  Ported from slang-shaders/.../src/crt-royale-bloom-vertical.slang. Builds the
//  fullscreen triangle from gl_VertexIndex (no vertex buffer, like the other
//  passes) instead of global.MVP * Position, and forwards the same varyings the
//  original vertex stage computed: the half-texel-nudged texcoord, the
//  vertical-only blur step, and the per-vertex bloom sigma.
//
//  This is an intermediate sRGB-FBO pass at 1.0 source scale, so it is neither
//  FIRST_PASS nor LAST_PASS; gamma is hardware sRGB on store/load.

#define SIMULATE_CRT_ON_LCD

#include "compat.inc"
#include "params.inc"
#include "gamma.inc"
#include "08_bloom-helpers.inc"

//  set=0/binding=0 Params UBO, bound to BOTH stages (crt_royale_contract.md).
//  Declare the full canonical block so std140 offsets match the C++ upload; this
//  stage reads SourceSize and OutputSize.
layout(std140, set = 0, binding = 0) uniform Params
{
    vec4 SourceSize;
    vec4 OriginalSize;
    vec4 OutputSize;
    uint FrameCount;
} params;

layout(location = 0) out vec2 tex_uv;
layout(location = 1) out vec2 bloom_dxdy;
layout(location = 2) out float bloom_sigma_runtime;

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
    tex_uv = (pos * 0.5 + 0.5) * 1.0001;

    //  Vertical-only blur step between output pixels (like the upstream
    //  vertex-shader-blur-fast-vertical.h): dxdy = (video/output)/texture, then
    //  zero out the horizontal offset. video_size == texture_size == SourceSize.
    vec2 dxdy_scale = params.SourceSize.xy / params.OutputSize.xy;
    vec2 dxdy = dxdy_scale / params.SourceSize.xy;
    bloom_dxdy = vec2(0.0, dxdy.y);

    //  Per-vertex bloom sigma (static under baked params; see 08_bloom-helpers.inc).
    bloom_sigma_runtime = get_bloom_sigma_runtime();
}
