#version 450

//  crt-royale-xm29plus pass 6 (masked scanlines) vertex stage.
//  Ported from slang-shaders/.../src/crt-royale-scanlines-horizontal-apply-
//  mask.{slang,h}. Builds the fullscreen triangle from gl_VertexIndex (no vertex
//  buffer, like 01_linearize.vert) and forwards what the fragment needs:
//    - tex_uv: the normalized [0,1] sampling coord. The slang vertex emitted
//      separate scanline_tex_uv / blur3x3_tex_uv / halation_tex_uv, but each was
//      video_uv * (input video_size / input texture_size); every input here is a
//      non-padded FBO (video_size == texture_size), so all three collapse to the
//      same normalized coord. Hence a single varying.
//    - mask_tiles_per_screen: how many phosphor-mask tiles span the viewport,
//      used to wrap the large slot-mask LUT during hardware-resize sampling.
//
//  Mask-sizing surgery: with mask_type=1 (slot) and mask_sample_mode=1 (hardware
//  resize) baked, get_mask_sampling_parameters()/get_resized_mask_tile_size()
//  collapse to their hardware-resize early-out: the resized tile spans
//  mask_triads_per_tile triads of the desired pixel size, and the slot LUT is
//  square, so the tile is that many pixels square. The tile start/size varying
//  (0,0,1,1) the slang also emitted is unused by the hardware-resize sampler
//  (convert_phosphor_tile_uv_wrap_to_tex_uv returns tile_uv_wrap unchanged), so
//  it is dropped.
//  Stream stream/p6-mask-bright.

#define SIMULATE_CRT_ON_LCD

#include "compat.inc"
#include "params.inc"

//  set=0/binding=0 Params UBO, bound to BOTH stages (crt_royale_contract.md).
//  Declare the full canonical block so std140 offsets match the C++ upload and
//  the fragment stage; this stage reads OutputSize for the mask tile count.
layout(std140, set = 0, binding = 0) uniform Params
{
    vec4 SourceSize;
    vec4 OriginalSize;
    vec4 OutputSize;
    uint FrameCount;
} params;

layout(location = 0) out vec2 tex_uv;
layout(location = 1) out vec2 mask_tiles_per_screen;

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

    //  Hardware-resized slot-mask tile size, in viewport pixels. mask_specify_-
    //  num_triads is 0 (baked), so a tile spans mask_triads_per_tile triads at
    //  the desired pixel size; the slot LUT is square, so the tile is square.
    float mask_resized_tile_size = mask_triads_per_tile * mask_triad_size_desired;
    mask_tiles_per_screen = params.OutputSize.xy / vec2(mask_resized_tile_size);
}
