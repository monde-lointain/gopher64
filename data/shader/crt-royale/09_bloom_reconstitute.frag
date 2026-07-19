#version 450

//  crt-royale-xm29plus pass 9 (bloom-horizontal-reconstitute) fragment stage.
//  Ported from slang-shaders/.../crt-royale-bloom-horizontal-reconstitute.slang.
//  Finishes the phosphor bloom by blurring the vertically-blurred brightpass
//  horizontally, then reconstitutes the final pre-geometry image: it folds the
//  masked scanlines' mask dimpass back in, undims/amplifies, and mixes in
//  halation-driven refractive diffusion.
//
//  Gamma: intermediate sRGB-FBO pass (NOT first/last). All four sampled inputs
//  are sRGB FBOs already decoded to linear by hardware on load, so
//  tex2D_linearize is a passthrough; the render target is sRGB, so encode_output
//  is a passthrough and hardware encodes on store. See gamma.inc.
//
//  This is the last offscreen pass; the C++ side mipmaps this output for the
//  final geometry pass's trilinear sampling. Nothing special is needed here.

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

//  Inputs (LinearClamp), in the pipeline's binding order:
//  1: Source = pass 8 (bloom-vertical) output, the bloom input to blur here.
//  2: HALATION_BLUR (pass 5), 3: BRIGHTPASS (pass 7), 4: MASKED_SCANLINES (pass 6).
layout(set = 0, binding = 1) uniform sampler2D Source;
layout(set = 0, binding = 2) uniform sampler2D HALATION_BLUR;
layout(set = 0, binding = 3) uniform sampler2D BRIGHTPASS;
layout(set = 0, binding = 4) uniform sampler2D MASKED_SCANLINES;

layout(location = 0) in vec2 video_uv;
layout(location = 1) in vec2 scanline_tex_uv;
layout(location = 2) in vec2 halation_tex_uv;
layout(location = 3) in vec2 brightpass_tex_uv;
layout(location = 4) in vec2 bloom_tex_uv;
layout(location = 5) in vec2 bloom_dxdy;
layout(location = 6) in float bloom_sigma_runtime;
layout(location = 0) out vec4 FragColor;

void main()
{
    //  Blur the vertically blurred brightpass horizontally to finish the bloom.
    float bloom_sigma = get_final_bloom_sigma(bloom_sigma_runtime);
    vec3 blurred_brightpass = tex2DblurNfast(Source, bloom_tex_uv, bloom_dxdy,
        bloom_sigma);

    //  Sample the masked scanlines; reconstruct the mask dimpass by removing the
    //  brightpass, then undim (scanline auto-dim) and amplify (mask dim):
    vec3 intensity_dim = tex2D_linearize(MASKED_SCANLINES, scanline_tex_uv).rgb;
    float undim_factor = 1.0 / levels_autodim_temp;
    float mask_amplify = get_mask_amplify();
    vec3 brightpass = tex2D_linearize(BRIGHTPASS, brightpass_tex_uv).rgb;
    vec3 dimpass = intensity_dim - brightpass;
    vec3 phosphor_bloom = (dimpass + blurred_brightpass) *
        mask_amplify * undim_factor * levels_contrast;

    //  Let some light bleed into refractive diffusion. This is added late (here,
    //  not earlier passes) to avoid black crush in the diffusion colors.
    vec3 diffusion_color = levels_contrast *
        tex2D_linearize(HALATION_BLUR, halation_tex_uv).rgb;
    vec3 final_bloom = lerp(phosphor_bloom, diffusion_color, diffusion_weight);

    FragColor = encode_output(vec4(final_bloom, 1.0));
}
