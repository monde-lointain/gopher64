#version 450

//  crt-royale-xm29plus pass 2 (vertical scanlines) fragment stage.
//  Ported from slang-shaders/.../src/crt-royale-scanlines-vertical-interlacing
//  .slang. Samples several (possibly misconverged) scanlines from the linearized
//  pass-1 output (ORIG_LINEARIZED) and integrates the electron-beam profile to
//  the final vertical resolution, then auto-dims to avoid clipping in the 8-bit
//  sRGB FBO.
//
//  Gamma: middle sRGB-FBO pass, so NEITHER FIRST_PASS nor LAST_PASS is defined.
//  tex2D_linearize is a passthrough (the hardware already sRGB-decoded the input
//  on read) and encode_output is a passthrough (the hardware sRGB-encodes on
//  store). See gamma.inc for the full contract.

#define SIMULATE_CRT_ON_LCD

#include "compat.inc"
#include "params.inc"
#include "gamma.inc"
#include "scanline-functions.inc"

//  set=0/binding=0 Params UBO, bound to BOTH stages (crt_royale_contract.md).
//  Declare the full canonical block so its std140 offsets match the fixed 64-byte
//  block the C++ side uploads; this pass reads SourceSize and FrameCount (the
//  interlace bob) and ignores the rest.
layout(std140, set = 0, binding = 0) uniform Params
{
    vec4 SourceSize;
    vec4 OriginalSize;
    vec4 OutputSize;
    uint FrameCount;
} params;

//  set=0/binding=1: Source = pass 1 (linearize) output, ORIG_LINEARIZED.
layout(set = 0, binding = 1) uniform sampler2D Source;

layout(location = 0) in vec2 tex_uv;
layout(location = 1) in vec2 uv_step;
layout(location = 2) in vec2 il_step_multiple;
layout(location = 3) in float pixel_height_in_scanlines;
layout(location = 0) out vec4 FragColor;

void main()
{
    //  Read attributes into locals. The trailing underscore on texture_size_
    //  dodges compat.inc's texture_size -> SourceSize.xy macro.
    vec2 texture_size_ = params.SourceSize.xy;
    vec2 texture_size_inv = 1.0 / texture_size_;
    float ph = pixel_height_in_scanlines;

    //  Get the uv coords of the previous scanline (in this field) and its
    //  distance from this sample, in scanlines.
    float dist;
    vec2 scanline_uv = get_last_scanline_uv(tex_uv, texture_size_,
        texture_size_inv, il_step_multiple, float(params.FrameCount), dist);

    //  Sample scanlines 2 and 3 (the previous and next scanlines). Horizontal
    //  sampling is skipped because output_size.x == video_size.x. ORIG_LINEARIZED
    //  already bobbed any interlaced input, so plain bilinear is safe here.
    vec2 v_step = vec2(0.0, uv_step.y);
    vec3 scanline2_color = tex2D_linearize(Source, scanline_uv).rgb;
    vec3 scanline3_color = tex2D_linearize(Source, scanline_uv + v_step).rgb;
    vec3 scanline0_color, scanline1_color, scanline4_color, scanline5_color,
        scanline_outside_color;
    float dist_round;
    //  Sample additional scanlines per the requested beam_num_scanlines.
    if (beam_num_scanlines > 5.5)
    {
        scanline1_color = tex2D_linearize(Source, scanline_uv - v_step).rgb;
        scanline4_color = tex2D_linearize(Source, scanline_uv + 2.0 * v_step).rgb;
        scanline0_color = tex2D_linearize(Source, scanline_uv - 2.0 * v_step).rgb;
        scanline5_color = tex2D_linearize(Source, scanline_uv + 3.0 * v_step).rgb;
    }
    else if (beam_num_scanlines > 4.5)
    {
        scanline1_color = tex2D_linearize(Source, scanline_uv - v_step).rgb;
        scanline4_color = tex2D_linearize(Source, scanline_uv + 2.0 * v_step).rgb;
        //  dist is in [0, 1]: pick scanline 0 or 5 by which is nearer.
        dist_round = round(dist);
        vec2 sample_0_or_5_uv_off = mix(-2.0 * v_step, 3.0 * v_step, dist_round);
        scanline_outside_color =
            tex2D_linearize(Source, scanline_uv + sample_0_or_5_uv_off).rgb;
    }
    else if (beam_num_scanlines > 3.5)
    {
        scanline1_color = tex2D_linearize(Source, scanline_uv - v_step).rgb;
        scanline4_color = tex2D_linearize(Source, scanline_uv + 2.0 * v_step).rgb;
    }
    else if (beam_num_scanlines > 2.5)
    {
        //  dist is in [0, 1]: pick scanline 1 or 4 by which is nearer.
        dist_round = round(dist);
        vec2 sample_1or4_uv_off = mix(-v_step, 2.0 * v_step, dist_round);
        scanline_outside_color =
            tex2D_linearize(Source, scanline_uv + sample_1or4_uv_off).rgb;
    }

    //  Compute scanline contributions, accounting for vertical convergence.
    //  Vertical convergence offsets are in units of current-field scanlines.
    //  dist2 is the positive sample distance from scanline 2, in scanlines.
    vec3 dist2 = vec3(dist);
    if (beam_misconvergence)
    {
        dist2 = vec3(dist) - get_convergence_offsets_y_vector();
    }
    //  Compute {sigma,shape}_range once per pixel (not per scanline). Static
    //  params let these constant-fold.
    float sigma_range = max(beam_max_sigma, beam_min_sigma) - beam_min_sigma;
    float shape_range = max(beam_max_shape, beam_min_shape) - beam_min_shape;
    //  Sum additive light contributions; no normalization, since each scanline is
    //  an additive light source rather than a sample of a continuous signal.
    vec3 scanline2_contrib =
        scanline_contrib(dist2, scanline2_color, ph, sigma_range, shape_range);
    vec3 scanline3_contrib = scanline_contrib(abs(vec3(1.0) - dist2),
        scanline3_color, ph, sigma_range, shape_range);
    vec3 scanline_intensity = scanline2_contrib + scanline3_contrib;
    if (beam_num_scanlines > 5.5)
    {
        vec3 scanline0_contrib = scanline_contrib(dist2 + vec3(2.0),
            scanline0_color, ph, sigma_range, shape_range);
        vec3 scanline1_contrib = scanline_contrib(dist2 + vec3(1.0),
            scanline1_color, ph, sigma_range, shape_range);
        vec3 scanline4_contrib = scanline_contrib(abs(vec3(2.0) - dist2),
            scanline4_color, ph, sigma_range, shape_range);
        vec3 scanline5_contrib = scanline_contrib(abs(vec3(3.0) - dist2),
            scanline5_color, ph, sigma_range, shape_range);
        scanline_intensity += scanline0_contrib + scanline1_contrib +
            scanline4_contrib + scanline5_contrib;
    }
    else if (beam_num_scanlines > 4.5)
    {
        vec3 scanline1_contrib = scanline_contrib(dist2 + vec3(1.0),
            scanline1_color, ph, sigma_range, shape_range);
        vec3 scanline4_contrib = scanline_contrib(abs(vec3(2.0) - dist2),
            scanline4_color, ph, sigma_range, shape_range);
        vec3 dist0or5 = mix(dist2 + vec3(2.0), vec3(3.0) - dist2, dist_round);
        vec3 scanline0or5_contrib = scanline_contrib(dist0or5,
            scanline_outside_color, ph, sigma_range, shape_range);
        scanline_intensity +=
            scanline1_contrib + scanline4_contrib + scanline0or5_contrib;
    }
    else if (beam_num_scanlines > 3.5)
    {
        vec3 scanline1_contrib = scanline_contrib(dist2 + vec3(1.0),
            scanline1_color, ph, sigma_range, shape_range);
        vec3 scanline4_contrib = scanline_contrib(abs(vec3(2.0) - dist2),
            scanline4_color, ph, sigma_range, shape_range);
        scanline_intensity += scanline1_contrib + scanline4_contrib;
    }
    else if (beam_num_scanlines > 2.5)
    {
        vec3 dist1or4 = mix(dist2 + vec3(1.0), vec3(2.0) - dist2, dist_round);
        vec3 scanline1or4_contrib = scanline_contrib(dist1or4,
            scanline_outside_color, ph, sigma_range, shape_range);
        scanline_intensity += scanline1or4_contrib;
    }

    //  Auto-dim to avoid clipping in the 8-bit sRGB FBO, then output. encode_output
    //  is a passthrough here; the sRGB FBO encodes display gamma on store.
    FragColor = encode_output(vec4(scanline_intensity * levels_autodim_temp, 1.0));
}
