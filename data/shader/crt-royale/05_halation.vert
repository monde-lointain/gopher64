#version 450

//  crt-royale-xm29plus pass 5 (halation, blur-horizontal) vertex stage.
//  Ported from blurs/shaders/royale/blur5fast-horizontal.slang +
//  vertex-shader-blur-fast-horizontal.h.  Builds the fullscreen triangle from
//  gl_VertexIndex and forwards the one-texel horizontal blur stride.
//
//  Second half of the separable 5-tap halation blur; it blurs the pass-4
//  (blur-vertical) output, aliased HALATION_BLUR, horizontally at source scale
//  1.0.  Stream P3.

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

layout(location = 0) out vec2 tex_uv;
layout(location = 1) out vec2 blur_dxdy;

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

    //  uv sample distance between output pixels.  Blur at the destination size
    //  (video/output), then divide by the texture size to get a uv stride.
    vec2 dxdy_scale = params.SourceSize.xy * params.OutputSize.zw;
    vec2 dxdy = dxdy_scale * params.SourceSize.zw;
    //  Horizontal-only blur: zero the vertical offset.
    blur_dxdy = vec2(dxdy.x, 0.0);
}
