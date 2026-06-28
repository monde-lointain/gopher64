#version 450

//  crt-royale-xm29plus pass 7 (brightpass) vertex stage.
//  Ported from slang-shaders/.../src/crt-royale-brightpass.slang. Builds the
//  fullscreen triangle from gl_VertexIndex and forwards a single normalized
//  [0,1] sampling coord. The slang emitted scanline_tex_uv and blur3x3_tex_uv
//  (each video_uv scaled by an input's video_size/texture_size); both inputs are
//  non-padded FBOs, so they collapse to the same coord -> one varying. The
//  slang's third varying, bloom_sigma_runtime, fed get_final_bloom_sigma, which
//  ignores it unless RUNTIME_PHOSPHOR_BLOOM_SIGMA is set (baked off here), so it
//  is dropped and the brightpass sigma is computed statically in the fragment.
//  Stream stream/p6-mask-bright.

#define SIMULATE_CRT_ON_LCD

#include "compat.inc"
#include "params.inc"

//  set=0/binding=0 Params UBO, bound to BOTH stages (crt_royale_contract.md).
//  Declared in full so std140 offsets match the C++ upload and the fragment
//  stage, even though this stage needs no parameters.
layout(std140, set = 0, binding = 0) uniform Params
{
    vec4 SourceSize;
    vec4 OriginalSize;
    vec4 OutputSize;
    uint FrameCount;
} params;

layout(location = 0) out vec2 tex_uv;

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
    tex_uv = pos * 0.5 + 0.5;
}
