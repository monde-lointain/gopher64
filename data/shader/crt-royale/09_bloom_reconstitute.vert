#version 450

//  crt-royale-xm29plus pass 9 (bloom-horizontal-reconstitute) vertex stage.
//  Ported from slang-shaders/.../crt-royale-bloom-horizontal-reconstitute.slang.
//  Builds the fullscreen triangle from gl_VertexIndex (no vertex buffer) and
//  forwards the per-input texcoords, the horizontal-only blur step, and the
//  per-vertex bloom sigma.
//
//  Texcoord note: upstream scales each input's coords by its own
//  video_size/texture_size to skip padding. This port has no padded textures
//  (every sampler's texture_size == video_size == its SourceSize), so all of
//  those ratios are 1.0 and every coord collapses to the normalized fullscreen
//  tex_uv. The differing input resolutions are handled by normalized sampling.
//
//  This is an intermediate sRGB-FBO pass at 1.0 source scale, so it is neither
//  FIRST_PASS nor LAST_PASS; gamma is hardware sRGB on store/load.

#define SIMULATE_CRT_ON_LCD

#include "compat.inc"
#include "params.inc"
#include "gamma.inc"
#include "08_bloom-helpers.inc"

//  set=0/binding=0 Params UBO, bound to BOTH stages (crt_royale_contract.md).
layout(std140, set = 0, binding = 0) uniform Params
{
    vec4 SourceSize;
    vec4 OriginalSize;
    vec4 OutputSize;
    uint FrameCount;
} params;

layout(location = 0) out vec2 video_uv;
layout(location = 1) out vec2 scanline_tex_uv;
layout(location = 2) out vec2 halation_tex_uv;
layout(location = 3) out vec2 brightpass_tex_uv;
layout(location = 4) out vec2 bloom_tex_uv;
layout(location = 5) out vec2 bloom_dxdy;
layout(location = 6) out float bloom_sigma_runtime;

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
    vec2 tex_uv = pos * 0.5 + 0.5;

    //  No padded textures in this port, so all input coords equal tex_uv.
    video_uv = tex_uv;
    scanline_tex_uv = tex_uv;
    halation_tex_uv = tex_uv;
    brightpass_tex_uv = tex_uv;
    bloom_tex_uv = tex_uv;

    //  Horizontally blur the bloom input (the vertically blurred brightpass);
    //  this pass must NOT resize, so step one input texel horizontally.
    bloom_dxdy = vec2(1.0 / params.SourceSize.x, 0.0);

    //  Per-vertex bloom sigma (static under baked params; see 08_bloom-helpers.inc).
    bloom_sigma_runtime = get_bloom_sigma_runtime();
}
