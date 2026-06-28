#version 450

//  crt-royale-xm29plus pass 3 (bloom-approx) vertex stage.
//  Ported from crt-royale/src/crt-royale-bloom-approx.h.  Builds the fullscreen
//  triangle from gl_VertexIndex (no vertex buffer, like 01_linearize.vert) and
//  forwards the varyings the original vertex stage computed: the blur stride, the
//  scanline/convergence step, the estimated viewport width, and the resize-blur
//  scale factors.
//
//  Size remapping for gopher64's binding model (see crt_royale_contract.md and
//  crt_royale_layout.hpp): upstream aliased the ORIG_LINEARIZED sizes to
//  SourceSize because its preset gave VERTICAL_SCANLINES and ORIG_LINEARIZED the
//  same size.  Here pass 2 (VERTICAL_SCANLINES, this pass's Source/SourceSize)
//  stretches Y to the viewport, so its size differs from ORIG_LINEARIZED.  The
//  ORIG_LINEARIZED resampling math therefore uses OriginalSize (the linearized
//  original-resolution input), while estimated_viewport_size_x still uses
//  SourceSize.y (the vertical-scanlines viewport height), matching upstream
//  intent.  Stream P3.

#include "compat.inc"
#include "params.inc"
#include "gamma.inc"
#include "scanline-functions.inc"

//  set=0/binding=0 Params UBO, bound to BOTH stages (crt_royale_contract.md).
//  Full canonical block so std140 offsets match the C++ upload; this stage reads
//  SourceSize, OriginalSize and OutputSize.
layout(std140, set = 0, binding = 0) uniform Params
{
    vec4 SourceSize;
    vec4 OriginalSize;
    vec4 OutputSize;
    uint FrameCount;
} params;

layout(location = 0) out vec2 tex_uv;
layout(location = 1) out vec2 blur_dxdy;
layout(location = 2) out vec2 uv_scanline_step;
layout(location = 3) out float estimated_viewport_size_x;
layout(location = 4) out vec2 texture_size_inv;
layout(location = 5) out vec2 tex_uv_to_pixel_scale;

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

    //  Upstream's texture_size/video_size and ORIG_LINEARIZED video/texture
    //  ratios are all 1.0, so the forwarded uv is just the screen-space uv.
    tex_uv = pos * 0.5 + 0.5;

    //  The vertical-scanlines pass had a viewport Y scale, so use it for a better
    //  runtime bloom sigma (SourceSize.y is that viewport height).
    estimated_viewport_size_x =
        params.SourceSize.y * geom_aspect_ratio_x / geom_aspect_ratio_y;

    //  Sample-distance setup for the 4x4 Gaussian resize of ORIG_LINEARIZED.
    //  ORIG_LINEARIZED is at the original resolution, so use OriginalSize.
    vec2 dxdy_min_scale = params.OriginalSize.xy / params.OutputSize.xy;
    texture_size_inv = params.OriginalSize.zw;
    if (bloom_approx_filter > 1.5)
    {
        //  4x4 true Gaussian resize: snap to texels and sample the nearest 4.
        vec2 dxdy_scale = max(dxdy_min_scale, vec2(1.0));
        blur_dxdy = dxdy_scale * texture_size_inv;
    }
    else
    {
        blur_dxdy = dxdy_min_scale * texture_size_inv;
    }
    //  tex2Dresize_gaussian4x4 needs the uv->output-pixel scale.  Upstream's
    //  output_size * ORIG_texture / ORIG_video reduces to OutputSize here.
    tex_uv_to_pixel_scale = params.OutputSize.xy;

    //  Detect interlacing to apply convergence offsets in this pass.  il_step_
    //  multiple is the (texel, scanline) step: 1 progressive, 2 interlaced.
    float y_step = 1.0 + float(is_interlaced(params.OriginalSize.y));
    vec2 il_step_multiple = vec2(1.0, y_step);
    uv_scanline_step = il_step_multiple * params.OriginalSize.zw;
}
