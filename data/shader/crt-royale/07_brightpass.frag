#version 450

//  crt-royale-xm29plus pass 7 (brightpass) fragment stage.
//  Ported from slang-shaders/.../src/crt-royale-brightpass.slang (+ the bits of
//  bloom-functions.h it needs). Computes a brightpass image for the later bloom
//  passes: it undims the masked scanlines, estimates how much each phosphor would
//  bloom (from the BLOOM_APPROX blur), and blends the dim scanlines toward that
//  estimate by a per-channel blur ratio.
//
//  Baked codepath: RUNTIME_* off, so the bloom sigma is the static "optimistic"
//  sigma for the desired triad size; BRIGHTPASS_AREA_BASED off (simple blur
//  ratio); PHOSPHOR_BLOOM_FAKE off (real bloom is passes 8-9).
//
//  Gamma: intermediate sRGB-FBO pass (neither FIRST nor LAST), so tex2D_linearize
//  and encode_output are pass-throughs. See gamma.inc.
//
//  Bindings (must match crt_royale.cpp InputBinding order): 1 MASKED_SCANLINES,
//  2 BLOOM_APPROX.
//  Stream stream/p6-mask-bright.

#define SIMULATE_CRT_ON_LCD

#include "compat.inc"
#include "params.inc"
#include "gamma.inc"

//  set=0/binding=0 Params UBO, bound to BOTH stages (crt_royale_contract.md).
//  Full canonical block for std140 offset parity with the C++ upload.
layout(std140, set = 0, binding = 0) uniform Params
{
    vec4 SourceSize;
    vec4 OriginalSize;
    vec4 OutputSize;
    uint FrameCount;
} params;

layout(set = 0, binding = 1) uniform sampler2D MASKED_SCANLINES;
layout(set = 0, binding = 2) uniform sampler2D BLOOM_APPROX;

layout(location = 0) in vec2 tex_uv;
layout(location = 0) out vec4 FragColor;

//  Max desired pixel difference within a blurred triad (bloom-functions.h).
float bloom_diff_thresh = 1.0 / 256.0;

float get_min_sigma_to_blur_triad(float triad_size, float thresh)
{
    //  Closed-form (curve-fit) minimum Gaussian sigma that blurs a phosphor triad
    //  flat to within thresh. From bloom-functions.h.
    return -0.05168 + 0.6113 * triad_size -
        1.122 * triad_size * sqrt(0.000416 + thresh);
}

float get_bloom_center_weight(float sigma)
{
    //  Center-texel weight of the separable Gaussian the bloom passes apply, for
    //  a static (compile-time) sigma. 17-tap window: matches the preset's
    //  PHOSPHOR_BLOOM_TRIADS_LARGER_THAN_3_PIXELS setting (triads up to ~6px).
    //  From bloom-functions.h get_center_weight (non-runtime-sigma path).
    float denom_inv = 0.5 / (sigma * sigma);
    float w0 = 1.0;
    float w1 = exp(-1.0 * denom_inv);
    float w2 = exp(-4.0 * denom_inv);
    float w3 = exp(-9.0 * denom_inv);
    float w4 = exp(-16.0 * denom_inv);
    float w5 = exp(-25.0 * denom_inv);
    float w6 = exp(-36.0 * denom_inv);
    float w7 = exp(-49.0 * denom_inv);
    float w8 = exp(-64.0 * denom_inv);
    float weight_sum_inv =
        1.0 / (w0 + 2.0 * (w1 + w2 + w3 + w4 + w5 + w6 + w7 + w8));
    return weight_sum_inv * weight_sum_inv;
}

void main()
{
    //  Auto-dimmed, masked scanline color from pass 6:
    vec3 intensity_dim = tex2D_linearize(MASKED_SCANLINES, tex_uv).rgb;
    //  Recover full intensity: undo the inter-pass auto-dim and the mask dimming,
    //  and apply the final contrast.
    float auto_dim_factor = levels_autodim_temp;
    float undim_factor = 1.0 / auto_dim_factor;
    float mask_amplify = get_mask_amplify();
    vec3 intensity =
        intensity_dim * undim_factor * mask_amplify * levels_contrast;

    //  BLOOM_APPROX approximates a straight blur of the masked scanlines, used to
    //  estimate the energy this texel will receive from blooming neighbors:
    vec3 phosphor_blur_approx =
        levels_contrast * tex2D_linearize(BLOOM_APPROX, tex_uv).rgb;

    //  Center-texel blur weight and the max energy expected from neighbors:
    float bloom_sigma =
        get_min_sigma_to_blur_triad(mask_triad_size_desired, bloom_diff_thresh);
    float center_weight = get_bloom_center_weight(bloom_sigma);
    vec3 max_area_contribution_approx =
        max(vec3(0.0), phosphor_blur_approx - center_weight * intensity);
    //  Underestimate intensities by the user factor (assume neighbors blur 100%):
    vec3 area_contrib_underestimate =
        bloom_underestimate_levels * max_area_contribution_approx;
    vec3 intensity_underestimate = bloom_underestimate_levels * intensity;
    //  Ratio of intensity we want to blur (BRIGHTPASS_AREA_BASED off):
    vec3 blur_ratio_temp =
        ((vec3(1.0) - area_contrib_underestimate) / intensity_underestimate -
        vec3(1.0)) / (center_weight - 1.0);
    vec3 blur_ratio = clamp(blur_ratio_temp, 0.0, 1.0);
    //  Brightpass from the auto-dimmed, unamplified scanlines, biased by the
    //  user's bloom excess, then encode (pass-through on the sRGB FBO):
    vec3 brightpass = intensity_dim * lerp(blur_ratio, vec3(1.0), bloom_excess);
    FragColor = encode_output(vec4(brightpass, 1.0));
}
