#version 450

//  crt-royale-xm29plus pass 2 (vertical scanlines) vertex stage.
//  Ported from slang-shaders/.../src/crt-royale-scanlines-vertical-interlacing
//  .slang. Builds the fullscreen triangle from gl_VertexIndex (no vertex buffer,
//  like 01_linearize.vert) instead of global.MVP * Position, and forwards the
//  same varyings the original vertex stage emits: the half-texel-nudged texcoord,
//  the per-texel/per-scanline uv step, the interlace step multiple, and the
//  output pixel height measured in scanlines. All are affine in screen space.

//  Middle sRGB-FBO pass: define NEITHER FIRST_PASS nor LAST_PASS, so gamma.inc's
//  decode/encode become passthroughs (the hardware does sRGB on the FBO).
//  SIMULATE_CRT_ON_LCD matches the pipeline-wide gamma selection (params.inc also
//  sets it) and lets scanline-functions.inc's gamma helpers resolve.
#define SIMULATE_CRT_ON_LCD

#include "compat.inc"
#include "params.inc"
#include "gamma.inc"
#include "scanline-functions.inc"

//  set=0/binding=0 Params UBO, bound to BOTH stages (crt_royale_contract.md).
//  Declare the full canonical block so std140 offsets match the C++ upload and
//  the fragment stage; this stage reads SourceSize and OutputSize.
layout(std140, set = 0, binding = 0) uniform Params
{
    vec4 SourceSize;
    vec4 OriginalSize;
    vec4 OutputSize;
    uint FrameCount;
} params;

layout(location = 0) out vec2 tex_uv;
layout(location = 1) out vec2 uv_step;                     //  uv size of a texel (x) and scanline (y)
layout(location = 2) out vec2 il_step_multiple;            //  (1,1)=progressive, (1,2)=interlaced
layout(location = 3) out float pixel_height_in_scanlines;  //  output pixel height, in scanlines

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

    //  Map the clip-space triangle to [0,1] texcoords with the original
    //  half-texel nudge.
    vec2 tex = pos * 0.5 + 0.5;
    tex_uv = tex * 1.00001;

    //  Detect interlacing: il_step_multiple is the step multiple between lines,
    //  1 for progressive sources and 2 for interlaced. uv_step is the uv coord
    //  step between one texel (x) and one scanline (y).
    float y_step = 1.0 + float(is_interlaced(params.SourceSize.y));
    il_step_multiple = vec2(1.0, y_step);
    uv_step = il_step_multiple / params.SourceSize.xy;

    //  Pixel height in scanlines, needed for antialiased/integral beam sampling.
    pixel_height_in_scanlines =
        (params.SourceSize.y / params.OutputSize.y) / il_step_multiple.y;
}
