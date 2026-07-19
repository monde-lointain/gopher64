#ifndef CRT_ROYALE_COMPAT_INC
#define CRT_ROYALE_COMPAT_INC

//  Cg/HLSL -> GLSL compatibility macros for the crt-royale port.
//  Translated from slang-shaders/include/compat_macros.inc so the royale source
//  headers (gamma-management, special-functions, scanline-functions, ...) can be
//  reused with minimal edits. Include this FIRST in every pass, before any other
//  royale include, so the keyword strips and type aliases apply to them too.
//
//  Notes specific to this port:
//  - The size/frame-count aliases (IN, texture_size, video_size, output_size,
//    frame_count) resolve to members of a uniform block instance named `params`
//    at set=0, binding=0 (see crt_royale_contract.md). The including pass must
//    declare that block with vec4 SourceSize/OutputSize/OriginalSize and a uint
//    FrameCount as needed.
//  - `static`, `inline` and `const` are stripped: the royale headers declare
//    function-local `const` values from non-constant expressions (e.g.
//    `const float3 s = sign(x);`), which is illegal in GLSL. Stripping `const`
//    matches the upstream slang compile path.

#define mul(a, b) (b * a)
#define lerp(a, b, c) mix(a, b, c)
#define saturate(c) clamp(c, 0.0, 1.0)
#define frac(x) (fract(x))
#define float2 vec2
#define float3 vec3
#define float4 vec4
#define bool2 bvec2
#define bool3 bvec3
#define bool4 bvec4
#define float2x2 mat2x2
#define float3x3 mat3x3
#define float4x4 mat4x4
#define float4x3 mat4x3
#define float2x4 mat2x4
#define IN params
#define texture_size SourceSize.xy
#define video_size SourceSize.xy
#define output_size OutputSize.xy
#define frame_count FrameCount
#define static
#define inline
#define const
#define fmod(x, y) mod(x, y)
#define ddx(c) dFdx(c)
#define ddy(c) dFdy(c)
#define atan2(x, y) atan(x, y)
#define rsqrt(c) inversesqrt(c)

#endif  //  CRT_ROYALE_COMPAT_INC
