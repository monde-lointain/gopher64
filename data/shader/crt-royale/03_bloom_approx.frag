#version 450

//  crt-royale-xm29plus pass 3 (bloom-approx) fragment stage.
//  Ported from crt-royale/src/crt-royale-bloom-approx.h.  Does a small resize
//  blur of ORIG_LINEARIZED down to an absolute 320x240, approximating the
//  phosphor bloom, and bakes in per-channel convergence offsets so later passes
//  can sample the bloom once instead of three times.
//
//  Baked parameters select a single codepath: bloom_approx_filter == 2.0 (4x4
//  true Gaussian resize) and beam_misconvergence == true, so only those branches
//  are kept.
//
//  Gamma: middle sRGB-FBO pass (NOT first/last).  tex2D_linearize and
//  encode_output are passthroughs; the hardware does the sRGB decode on read and
//  encode on store.  See gamma.inc.  Stream P3.

#include "compat.inc"
#include "params.inc"
#include "gamma.inc"
#include "03_bloom_approx-helpers.inc"

//  set=0/binding=0 Params UBO, bound to BOTH stages (crt_royale_contract.md).
layout(std140, set = 0, binding = 0) uniform Params
{
    vec4 SourceSize;
    vec4 OriginalSize;
    vec4 OutputSize;
    uint FrameCount;
} params;

//  Inputs in C++ InputBinding order (crt_royale.cpp kInputsBloomApprox):
//   binding 1: VERTICAL_SCANLINES (this pass's nominal Source).  Only its size
//              (params.SourceSize, used in the vertex stage) is needed, so the
//              texture itself is unreferenced here, exactly as upstream.
//   binding 2: ORIG_LINEARIZED  -- the image actually blurred.
//   binding 3: Original (raw pre-LUT RDP scanout).  Declared to match the
//              pipeline's binding order; unreferenced, as in upstream.
layout(set = 0, binding = 1) uniform sampler2D VERTICAL_SCANLINES;
layout(set = 0, binding = 2) uniform sampler2D ORIG_LINEARIZED;
layout(set = 0, binding = 3) uniform sampler2D Original;

layout(location = 0) in vec2 tex_uv;
layout(location = 1) in vec2 blur_dxdy;
layout(location = 2) in vec2 uv_scanline_step;
layout(location = 3) in float estimated_viewport_size_x;
layout(location = 4) in vec2 texture_size_inv;
layout(location = 5) in vec2 tex_uv_to_pixel_scale;
layout(location = 0) out vec4 FragColor;

void main()
{
    //  ORIG_LINEARIZED is at the original resolution (OriginalSize).
    vec2 orig_texture_size = params.OriginalSize.xy;

    //  Per-channel convergence offsets (beam_misconvergence is baked true).
    //  Applying them here triples the bloom samples, but that is cheaper than
    //  sampling the bloom three times at full resolution everywhere it is used.
    vec2 convergence_offsets_r = get_convergence_offsets_r_vector();
    vec2 convergence_offsets_g = get_convergence_offsets_g_vector();
    vec2 convergence_offsets_b = get_convergence_offsets_b_vector();
    vec2 tex_uv_r = tex_uv - convergence_offsets_r * uv_scanline_step;
    vec2 tex_uv_g = tex_uv - convergence_offsets_g * uv_scanline_step;
    vec2 tex_uv_b = tex_uv - convergence_offsets_b * uv_scanline_step;

    float bloom_approx_sigma =
        get_bloom_approx_sigma(params.OutputSize.x, estimated_viewport_size_x);

    //  4x4 Gaussian resize per beam, then pack each beam's own channel.
    vec3 color_r = tex2Dresize_gaussian4x4(ORIG_LINEARIZED, tex_uv_r, blur_dxdy,
        orig_texture_size, texture_size_inv, tex_uv_to_pixel_scale,
        bloom_approx_sigma);
    vec3 color_g = tex2Dresize_gaussian4x4(ORIG_LINEARIZED, tex_uv_g, blur_dxdy,
        orig_texture_size, texture_size_inv, tex_uv_to_pixel_scale,
        bloom_approx_sigma);
    vec3 color_b = tex2Dresize_gaussian4x4(ORIG_LINEARIZED, tex_uv_b, blur_dxdy,
        orig_texture_size, texture_size_inv, tex_uv_to_pixel_scale,
        bloom_approx_sigma);
    vec3 color = vec3(color_r.r, color_g.g, color_b.b);

    FragColor = encode_output(vec4(color, 1.0));
}
