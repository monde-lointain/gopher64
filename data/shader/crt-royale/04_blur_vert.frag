#version 450

//  crt-royale-xm29plus pass 4 (blur-vertical) fragment stage.
//  Ported from blurs/shaders/royale/blur5fast-vertical.slang, which calls
//  tex2Dblur5fast from include/blur-functions.h.  That 1D 5x Gaussian blur (1
//  nearest-neighbour + 2 linear taps) is inlined here with its baked standard
//  deviation, so the whole 1900-line blur-functions.h is not pulled in.
//
//  Gamma: middle sRGB-FBO pass (NOT first/last).  tex2D_linearize and
//  encode_output are passthroughs (hardware does sRGB decode/encode).  Stream P3.

#include "compat.inc"
#include "params.inc"
#include "gamma.inc"

//  set=0/binding=0 Params UBO, bound to BOTH stages (crt_royale_contract.md).
layout(std140, set = 0, binding = 0) uniform Params
{
    vec4 SourceSize;
    vec4 OriginalSize;
    vec4 OutputSize;
    uint FrameCount;
} params;

//  binding 1: BLOOM_APPROX (the pass-3 output) -- the image being blurred.
layout(set = 0, binding = 1) uniform sampler2D Source;

layout(location = 0) in vec2 tex_uv;
layout(location = 1) in vec2 blur_dxdy;
layout(location = 0) out vec4 FragColor;

//  Default (non-binomial) blur5_std_dev from blur-functions.h, in dxdy strides.
//  The largest sigma keeping the largest unused 5-tap term <= 1.0/256.0.
float blur5_std_dev = 0.9845703125;

vec3 tex2Dblur5fast(sampler2D tex, vec2 uv, vec2 dxdy)
{
    //  1D 5x Gaussian blur via 1 nearest + 2 linear taps (blur-functions.h).
    float denom_inv = 0.5 / (blur5_std_dev * blur5_std_dev);
    float w0 = 1.0;
    float w1 = exp(-1.0 * denom_inv);
    float w2 = exp(-4.0 * denom_inv);
    float weight_sum_inv = 1.0 / (w0 + 2.0 * (w1 + w2));
    //  Combine each texel pair into one linear tap between them.
    float w12 = w1 + w2;
    float w12_ratio = w2 / w12;
    vec3 sum = vec3(0.0);
    sum += w12 * tex2D_linearize(tex, uv - (1.0 + w12_ratio) * dxdy).rgb;
    sum += w0 * tex2D_linearize(tex, uv).rgb;
    sum += w12 * tex2D_linearize(tex, uv + (1.0 + w12_ratio) * dxdy).rgb;
    return sum * weight_sum_inv;
}

void main()
{
    vec3 color = tex2Dblur5fast(Source, tex_uv, blur_dxdy);
    FragColor = encode_output(vec4(color, 1.0));
}
