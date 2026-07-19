#ifndef CRT_ROYALE_03_BLOOM_APPROX_HELPERS_INC
#define CRT_ROYALE_03_BLOOM_APPROX_HELPERS_INC

//  Bloom-approx fragment helpers for the crt-royale-xm29plus port (pass 3).
//  Translated from crt-royale/src/crt-royale-bloom-approx.h (the inline
//  tex2Dresize_gaussian4x4) and crt-royale/src/bloom-functions.h
//  (get_bloom_approx_sigma / get_min_sigma_to_blur_triad).
//
//  Only the bloom_approx_filter == 2.0 ("4x4 true Gaussian resize") codepath is
//  ported, because that value is baked here (params.inc).  The 3x3-resize and
//  bilinear codepaths, and the runtime-num-triads codepath, are all dead under
//  the baked parameters and are dropped to keep this readable.
//
//  Requires (include in this order first): compat.inc, params.inc, gamma.inc.
//  Uses tex2D_linearize (gamma.inc, a passthrough on this sRGB-FBO middle pass),
//  under_half / mask_triad_size_desired / beam_max_sigma (params.inc).

//  Squared length, for static Gaussian weight falloff.
#define LENGTH_SQ(v) (dot((v), (v)))

//////////////////////////////  BLOOM SIGMA MATH  //////////////////////////////

//  bloom-functions.h asymptotic constants.  max_viewport_size_x assumes a huge
//  viewport so the sigma estimate is resolution-independent.
float bloom_max_viewport_size_x = 1080.0 * 1024.0 * (4.0 / 3.0);
//  Max desired per-texel error in a fully blurred triad (bloom_diff_thresh).
float bloom_diff_thresh = 1.0 / 256.0;
//  min_allowed_viewport_triads.x from derived-settings-and-constants.h, baked:
//      mask_resize_num_tiles(2) * mask_triads_per_tile(8) /
//      mask_resize_viewport_scale.x(0.0625) = 256.0
//  (2 tiles because the mask resize uses TILE_FLAT_TWICE: no driver-extension
//  defines are set in this port, matching the preset's num_resized_mask_tiles.)
float min_allowed_viewport_triads_x = 256.0;

float get_min_sigma_to_blur_triad(float triad_size, float thresh)
{
    //  Closed-form curve fit: the minimum sigma that blurs a phosphor triad to an
    //  even color within thresh (bloom-functions.h).
    return -0.05168 + 0.6113 * triad_size -
        1.122 * triad_size * sqrt(0.000416 + thresh);
}

float get_bloom_approx_sigma(float output_size_x_runtime,
    float estimated_viewport_size_x)
{
    //  Dynamic small-bloom sigma for the 4x4 Gaussian resize.  mask_specify_num_
    //  triads is baked 0.0, so the triad count is derived from the estimated
    //  viewport size rather than the runtime "num triads" parameter.
    float mask_num_triads_from_size =
        estimated_viewport_size_x / mask_triad_size_desired;
    float mask_num_triads_runtime =
        max(min_allowed_viewport_triads_x, mask_num_triads_from_size);
    //  Blur as much as it would take to blur away one triad at an enormous
    //  viewport, then scale that sigma down to this pass's output resolution.
    float asymptotic_triad_size =
        bloom_max_viewport_size_x / mask_num_triads_runtime;
    float asymptotic_sigma =
        get_min_sigma_to_blur_triad(asymptotic_triad_size, bloom_diff_thresh);
    float bloom_approx_sigma =
        asymptotic_sigma * output_size_x_runtime / bloom_max_viewport_size_x;
    //  The input is ORIG_LINEARIZED (to avoid moire), but fold in the last pass's
    //  Gaussian scanline sigma so the bloom is tall enough vertically.
    return length(vec2(bloom_approx_sigma, beam_max_sigma));
}

///////////////////////////  4x4 TRUE GAUSSIAN RESIZE  /////////////////////////

vec3 tex2Dresize_gaussian4x4(sampler2D tex, vec2 tex_uv, vec2 dxdy,
    vec2 tex_size, vec2 texture_size_inv, vec2 tex_uv_to_pixel_scale,
    float sigma)
{
    //  A true 4x4 Gaussian resize: sample 4 locations along each dimension and
    //  weight them by their pixel-space distance from the destination pixel.
    //  See crt-royale-bloom-approx.h for the full preconditions.
    float denom_inv = 0.5 / (sigma * sigma);
    //  Snap to texels for upsizing; find sample 5 (second row/column):
    vec2 curr_texel = tex_uv * tex_size;
    vec2 prev_texel = floor(curr_texel - vec2(under_half)) + vec2(0.5);
    vec2 prev_texel_uv = prev_texel * texture_size_inv;
    vec2 snap = vec2(dxdy.x <= texture_size_inv.x, dxdy.y <= texture_size_inv.y);
    vec2 sample5_downsize_uv = tex_uv - 0.5 * dxdy;
    vec2 sample5_uv = mix(sample5_downsize_uv, prev_texel_uv, snap);
    //  Coords for the other samples, relative to sample 5:
    vec2 dx = vec2(dxdy.x, 0.0);
    vec2 sample0_uv = sample5_uv - dxdy;
    vec2 sample10_uv = sample5_uv + dxdy;
    vec2 sample15_uv = sample5_uv + 2.0 * dxdy;
    vec2 sample1_uv = sample0_uv + dx;
    vec2 sample2_uv = sample0_uv + 2.0 * dx;
    vec2 sample3_uv = sample0_uv + 3.0 * dx;
    vec2 sample4_uv = sample5_uv - dx;
    vec2 sample6_uv = sample5_uv + dx;
    vec2 sample7_uv = sample5_uv + 2.0 * dx;
    vec2 sample8_uv = sample10_uv - 2.0 * dx;
    vec2 sample9_uv = sample10_uv - dx;
    vec2 sample11_uv = sample10_uv + dx;
    vec2 sample12_uv = sample15_uv - 3.0 * dx;
    vec2 sample13_uv = sample15_uv - 2.0 * dx;
    vec2 sample14_uv = sample15_uv - dx;
    //  Load each sample (linearizing wrapper is a passthrough on this sRGB FBO).
    vec3 sample0 = tex2D_linearize(tex, sample0_uv).rgb;
    vec3 sample1 = tex2D_linearize(tex, sample1_uv).rgb;
    //  NOTE: This mirrors an upstream typo: crt-royale-bloom-approx.h samples
    //  `dx` here instead of `sample2_uv`.  Kept for visual parity with the
    //  reference shader (see the re-surface note in the port log).
    vec3 sample2 = tex2D_linearize(tex, dx).rgb;
    vec3 sample3 = tex2D_linearize(tex, sample3_uv).rgb;
    vec3 sample4 = tex2D_linearize(tex, sample4_uv).rgb;
    vec3 sample5 = tex2D_linearize(tex, sample5_uv).rgb;
    vec3 sample6 = tex2D_linearize(tex, sample6_uv).rgb;
    vec3 sample7 = tex2D_linearize(tex, sample7_uv).rgb;
    vec3 sample8 = tex2D_linearize(tex, sample8_uv).rgb;
    vec3 sample9 = tex2D_linearize(tex, sample9_uv).rgb;
    vec3 sample10 = tex2D_linearize(tex, sample10_uv).rgb;
    vec3 sample11 = tex2D_linearize(tex, sample11_uv).rgb;
    vec3 sample12 = tex2D_linearize(tex, sample12_uv).rgb;
    vec3 sample13 = tex2D_linearize(tex, sample13_uv).rgb;
    vec3 sample14 = tex2D_linearize(tex, sample14_uv).rgb;
    vec3 sample15 = tex2D_linearize(tex, sample15_uv).rgb;
    //  Destination-pixel offset of each sample:
    vec2 dest_pixel = tex_uv * tex_uv_to_pixel_scale;
    vec2 sample0_offset = sample0_uv * tex_uv_to_pixel_scale - dest_pixel;
    vec2 sample1_offset = sample1_uv * tex_uv_to_pixel_scale - dest_pixel;
    vec2 sample2_offset = sample2_uv * tex_uv_to_pixel_scale - dest_pixel;
    vec2 sample3_offset = sample3_uv * tex_uv_to_pixel_scale - dest_pixel;
    vec2 sample4_offset = sample4_uv * tex_uv_to_pixel_scale - dest_pixel;
    vec2 sample5_offset = sample5_uv * tex_uv_to_pixel_scale - dest_pixel;
    vec2 sample6_offset = sample6_uv * tex_uv_to_pixel_scale - dest_pixel;
    vec2 sample7_offset = sample7_uv * tex_uv_to_pixel_scale - dest_pixel;
    vec2 sample8_offset = sample8_uv * tex_uv_to_pixel_scale - dest_pixel;
    vec2 sample9_offset = sample9_uv * tex_uv_to_pixel_scale - dest_pixel;
    vec2 sample10_offset = sample10_uv * tex_uv_to_pixel_scale - dest_pixel;
    vec2 sample11_offset = sample11_uv * tex_uv_to_pixel_scale - dest_pixel;
    vec2 sample12_offset = sample12_uv * tex_uv_to_pixel_scale - dest_pixel;
    vec2 sample13_offset = sample13_uv * tex_uv_to_pixel_scale - dest_pixel;
    vec2 sample14_offset = sample14_uv * tex_uv_to_pixel_scale - dest_pixel;
    vec2 sample15_offset = sample15_uv * tex_uv_to_pixel_scale - dest_pixel;
    //  Gaussian sample weights:
    float w0 = exp(-LENGTH_SQ(sample0_offset) * denom_inv);
    float w1 = exp(-LENGTH_SQ(sample1_offset) * denom_inv);
    float w2 = exp(-LENGTH_SQ(sample2_offset) * denom_inv);
    float w3 = exp(-LENGTH_SQ(sample3_offset) * denom_inv);
    float w4 = exp(-LENGTH_SQ(sample4_offset) * denom_inv);
    float w5 = exp(-LENGTH_SQ(sample5_offset) * denom_inv);
    float w6 = exp(-LENGTH_SQ(sample6_offset) * denom_inv);
    float w7 = exp(-LENGTH_SQ(sample7_offset) * denom_inv);
    float w8 = exp(-LENGTH_SQ(sample8_offset) * denom_inv);
    float w9 = exp(-LENGTH_SQ(sample9_offset) * denom_inv);
    float w10 = exp(-LENGTH_SQ(sample10_offset) * denom_inv);
    float w11 = exp(-LENGTH_SQ(sample11_offset) * denom_inv);
    float w12 = exp(-LENGTH_SQ(sample12_offset) * denom_inv);
    float w13 = exp(-LENGTH_SQ(sample13_offset) * denom_inv);
    float w14 = exp(-LENGTH_SQ(sample14_offset) * denom_inv);
    float w15 = exp(-LENGTH_SQ(sample15_offset) * denom_inv);
    float weight_sum_inv = 1.0 / (
        w0 + w1 + w2 + w3 + w4 + w5 + w6 + w7 +
        w8 + w9 + w10 + w11 + w12 + w13 + w14 + w15);
    //  Weight and sum the samples:
    vec3 sum = w0 * sample0 + w1 * sample1 + w2 * sample2 + w3 * sample3 +
        w4 * sample4 + w5 * sample5 + w6 * sample6 + w7 * sample7 +
        w8 * sample8 + w9 * sample9 + w10 * sample10 + w11 * sample11 +
        w12 * sample12 + w13 * sample13 + w14 * sample14 + w15 * sample15;
    return sum * weight_sum_inv;
}

#endif  //  CRT_ROYALE_03_BLOOM_APPROX_HELPERS_INC
