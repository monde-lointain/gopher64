#version 450

//  crt-royale-xm29plus pass 8 (bloom-vertical) fragment stage.
//  Ported from slang-shaders/.../src/crt-royale-bloom-vertical.slang. Blurs the
//  brightpass (pass 7) vertically with a 9/17/25/43x separable Gaussian, the
//  first half of the phosphor bloom (the horizontal half is pass 9).
//
//  Gamma: intermediate sRGB-FBO pass (NOT first/last). The brightpass input is
//  an sRGB FBO already decoded to linear by hardware on load, so tex2D_linearize
//  is a passthrough; the render target is sRGB, so encode_output is a passthrough
//  too and hardware encodes on store. See gamma.inc for the full contract.

#define SIMULATE_CRT_ON_LCD

#include "compat.inc"
#include "params.inc"
#include "gamma.inc"
#include "08_bloom-helpers.inc"

//  set=0/binding=0 Params UBO, bound to BOTH stages (crt_royale_contract.md).
//  This stage uses none of the size fields directly, but declares the full
//  canonical block so std140 offsets match the C++ upload and the vertex stage.
layout(std140, set = 0, binding = 0) uniform Params
{
    vec4 SourceSize;
    vec4 OriginalSize;
    vec4 OutputSize;
    uint FrameCount;
} params;

//  set=0/binding=1: Source = BRIGHTPASS (pass 7) output (LinearClamp).
layout(set = 0, binding = 1) uniform sampler2D Source;

layout(location = 0) in vec2 tex_uv;
layout(location = 1) in vec2 bloom_dxdy;
layout(location = 2) in float bloom_sigma_runtime;
layout(location = 0) out vec4 FragColor;

void main()
{
    //  Blur the brightpass vertically. get_final_bloom_sigma constant-folds to
    //  the static optimistic sigma (RUNTIME_PHOSPHOR_BLOOM_SIGMA is off), so the
    //  branch in tex2DblurNfast resolves to a single blur size at compile time.
    float bloom_sigma = get_final_bloom_sigma(bloom_sigma_runtime);
    vec3 color = tex2DblurNfast(Source, tex_uv, bloom_dxdy, bloom_sigma);
    FragColor = encode_output(vec4(color, 1.0));
}
