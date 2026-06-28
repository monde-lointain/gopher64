#version 450

//  crt-royale-xm29plus pass 10 (geometry + antialiasing) fragment stage.
//  Ported from slang-shaders/.../src/crt-royale-geometry-aa-last-pass.{slang,h}.
//  This is the FINAL pass: it samples the mipmapped pass-9 (bloom-reconstitute)
//  output, optionally applies screen curvature + antialiasing, dims the border,
//  and writes the UNORM swapchain.
//
//  Baked context (see crt_royale_contract.md / params.inc):
//   - geom_mode is flat (0.0) and geom_overscan is (1.0, 1.0), so upstream's own
//     fast path is taken: a single bilinear tex2D_linearize, no curvature and no
//     antialiasing (the input is already final-resolution).  The full curvature
//     and tex2Daa machinery is still translated and referenced, so it must (and
//     does) compile; -O dead-strips it for this configuration.
//   - aa_level 12, aa_filter 6 (cubic) come from params.inc; the override macros
//     below keep tex2dantialias.inc from redefining the baked aa_* names.
//
//  Gamma: LAST_PASS is set, so encode_output applies the display-gamma encode
//  (to lcd_gamma) for the UNORM swapchain.  The pass-9 input is an sRGB FBO, so
//  the hardware decodes it to linear on sample and tex2D_linearize is a
//  passthrough (not FIRST_PASS).  See gamma.inc for the full contract.
//
//  DRIVERS_ALLOW_DERIVATIVES enables the dFdx/dFdy tangent-matrix path in
//  geometry-functions.inc (legal in a fragment shader).  Stream B.1.

#define LAST_PASS
#define SIMULATE_CRT_ON_LCD
#define DRIVERS_ALLOW_DERIVATIVES

//  The baked aa_level/aa_filter/aa_temporal/aa_cubic_c/aa_gauss_sigma and
//  get_aa_subpixel_r_offset() live in params.inc; tell tex2dantialias.inc not to
//  redefine them (it still supplies the aa_*_support static constants).
#define ANTIALIAS_OVERRIDE_BASICS
#define ANTIALIAS_OVERRIDE_PARAMETERS

#include "compat.inc"
#include "params.inc"
#include "gamma.inc"

//  Derived geometry constants (baked).  No tilt => identity rotation matrices;
//  the upstream construction is kept so the structure matches the slang source.
#define geom_mode_static (geom_mode_runtime)
#define geom_tilt_angle_static (vec2(geom_tilt_angle_x, geom_tilt_angle_y))

static const float2 sin_tilt = sin(geom_tilt_angle_static);
static const float2 cos_tilt = cos(geom_tilt_angle_static);
static const float3x3 geom_global_to_local_static = float3x3(
    cos_tilt.x, 0.0, -sin_tilt.x,
    sin_tilt.y*sin_tilt.x, cos_tilt.y, sin_tilt.y*cos_tilt.x,
    cos_tilt.y*sin_tilt.x, -sin_tilt.y, cos_tilt.y*cos_tilt.x);

#include "10_tex2dantialias.inc"
#include "10_geometry-functions.inc"

//  set=0/binding=0 Params UBO, bound to BOTH stages (crt_royale_contract.md).
//  Declare the full canonical block so its std140 offsets match the fixed
//  64-byte block the C++ side uploads; this pass reads SourceSize/OutputSize
//  (via the vertex varyings) and FrameCount.
layout(std140, set = 0, binding = 0) uniform Params
{
    vec4 SourceSize;
    vec4 OriginalSize;
    vec4 OutputSize;
    uint FrameCount;
} params;

//  set=0/binding=1: Source = pass 9 (bloom-reconstitute) output, mipmapped and
//  sampled TrilinearClamp by the integration code.
layout(set = 0, binding = 1) uniform sampler2D Source;
#define input_texture Source

layout(location = 0) in vec2 tex_uv;
layout(location = 1) in vec4 video_and_texture_size_inv;
layout(location = 2) in vec2 output_size_inv;
layout(location = 3) in vec3 eye_pos_local;
layout(location = 4) in vec4 geom_aspect_and_overscan;
layout(location = 5) in vec3 global_to_local_row0;
layout(location = 6) in vec3 global_to_local_row1;
layout(location = 7) in vec3 global_to_local_row2;
layout(location = 0) out vec4 FragColor;

float2x2 mul_scale(float2 scale, float2x2 matrix)
{
    //  Scale a 2x2 matrix's rows: equivalent to mul(diag(scale), matrix).
    vec4 temp_matrix = (vec4(matrix[0][0], matrix[0][1], matrix[1][0], matrix[1][1]) * scale.xxyy);
    return mat2x2(temp_matrix.x, temp_matrix.y, temp_matrix.z, temp_matrix.w);
}

void main()
{
    //  Localize some parameters:
    const float2 geom_aspect = geom_aspect_and_overscan.xy;
    const float2 geom_overscan = geom_aspect_and_overscan.zw;
    const float2 video_size_inv = video_and_texture_size_inv.xy;
    const float2 texture_size_inv = video_and_texture_size_inv.zw;
    //  No runtime tilt: the baked path reads the static matrix directly.
    static const float3x3 global_to_local = geom_global_to_local_static;
    static const float geom_mode = geom_mode_static;

    //  Get flat and curved texture coords for the current fragment point sample
    //  and a pixel_to_video_uv matrix for transforming pixel offsets.  In this
    //  port video_size == texture_size == SourceSize (no FBO padding), so
    //  flat_video_uv == tex_uv.
    const float2 flat_video_uv = tex_uv * (params.SourceSize.xy * video_size_inv);
    float2x2 pixel_to_video_uv;
    float2 video_uv_no_geom_overscan;
    if(geom_mode > 0.5)
    {
        video_uv_no_geom_overscan =
            get_curved_video_uv_coords_and_tangent_matrix(flat_video_uv,
                eye_pos_local, output_size_inv, geom_aspect,
                geom_mode, global_to_local, pixel_to_video_uv);
    }
    else
    {
        video_uv_no_geom_overscan = flat_video_uv;
        pixel_to_video_uv = float2x2(
            output_size_inv.x, 0.0, 0.0, output_size_inv.y);
    }
    //  Correct for overscan here (not in curvature code):
    const float2 video_uv =
        (video_uv_no_geom_overscan - float2(0.5, 0.5))/geom_overscan + float2(0.5, 0.5);
    const float2 tex_uv = video_uv * (params.SourceSize.xy * texture_size_inv);

    //  Get a matrix transforming pixel vectors to tex_uv vectors:
    const float2x2 pixel_to_tex_uv =
        mul_scale(params.SourceSize.xy * texture_size_inv /
            geom_aspect_and_overscan.zw, pixel_to_video_uv);

    //  Sample!  Skip antialiasing if aa_level < 0.5 or both of these hold:
    //  1.) Geometry/curvature isn't used  2.) Overscan == float2(1.0, 1.0)
    //  In this baked config both hold, so the plain bilinear path is taken.
    const float2 abs_aa_r_offset = abs(get_aa_subpixel_r_offset());
    const bool need_subpixel_aa = false;
    float3 color;

    if(aa_level > 0.5 && (geom_mode > 0.5 || any(bool2((geom_overscan.x != 1.0), (geom_overscan.y != 1.0)))))
    {
        //  Sample the input with antialiasing (due to sharp phosphors, etc.):
        color = tex2Daa(input_texture, tex_uv, pixel_to_tex_uv, float(params.FrameCount));
    }
    else if(aa_level > 0.5 && need_subpixel_aa)
    {
        //  Sample at each subpixel location:
        color = tex2Daa_subpixel_weights_only(
            input_texture, tex_uv, pixel_to_tex_uv);
    }
    else
    {
        color = tex2D_linearize(input_texture, tex_uv).rgb;
    }

    //  Dim borders and output the final result:
    const float border_dim_factor = get_border_dim_factor(video_uv, geom_aspect);
    const float3 final_color = color * border_dim_factor;

    FragColor = encode_output(float4(final_color, 1.0));
}
