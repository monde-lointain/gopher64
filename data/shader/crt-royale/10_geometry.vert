#version 450

//  crt-royale-xm29plus pass 10 (geometry + antialiasing) vertex stage.
//  Ported from slang-shaders/.../src/crt-royale-geometry-aa-last-pass.{slang,h}.
//  Builds the fullscreen triangle from gl_VertexIndex (no vertex buffer, like
//  01_linearize.vert) instead of global.MVP * Position, and forwards the same
//  varyings the original geometry vertex stage computed: texcoord, packed
//  video/texture-size inverses, output-size inverse, the local eye position,
//  the aspect/overscan vector, and the three global-to-local rotation rows.
//
//  Baked context (see crt_royale_contract.md / params.inc):
//   - geom_mode is flat (0.0); the curvature path never runs, but the eye-
//     placement machinery still does, so it must compile.
//   - There is no tilt, so the local<->global rotation matrices are identity;
//     the rotation rows are emitted as identity for interface completeness even
//     though the fragment's baked path reads geom_global_to_local_static.
//
//  Gamma: LAST_PASS is set so the shared includes agree with the fragment, but
//  this stage performs no gamma work.  DRIVERS_ALLOW_DERIVATIVES is deliberately
//  NOT defined here: dFdx/dFdy are illegal in a vertex shader, so
//  geometry-functions.inc compiles its non-derivative tangent path in this stage
//  (that path is never called from the vertex anyway).  Stream B.1.

#define LAST_PASS
#define SIMULATE_CRT_ON_LCD

#include "compat.inc"
#include "params.inc"
#include "gamma.inc"

//  Derived geometry constants (baked replacements for the values
//  derived-settings-and-constants.h would compute).  With no tilt the
//  local<->global rotation matrices reduce to identity; we keep the upstream
//  matrix construction so the structure matches the slang source.
#define geom_mode_static (geom_mode_runtime)
#define geom_tilt_angle_static (vec2(geom_tilt_angle_x, geom_tilt_angle_y))

static const float2 sin_tilt = sin(geom_tilt_angle_static);
static const float2 cos_tilt = cos(geom_tilt_angle_static);
static const float3x3 geom_local_to_global_static = float3x3(
    cos_tilt.x, sin_tilt.y*sin_tilt.x, cos_tilt.y*sin_tilt.x,
    0.0, cos_tilt.y, -sin_tilt.y,
    -sin_tilt.x, sin_tilt.y*cos_tilt.x, cos_tilt.y*cos_tilt.x);
static const float3x3 geom_global_to_local_static = float3x3(
    cos_tilt.x, 0.0, -sin_tilt.x,
    sin_tilt.y*sin_tilt.x, cos_tilt.y, sin_tilt.y*cos_tilt.x,
    cos_tilt.y*sin_tilt.x, -sin_tilt.y, cos_tilt.y*cos_tilt.x);

#include "10_geometry-functions.inc"

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
layout(location = 1) out vec4 video_and_texture_size_inv;
layout(location = 2) out vec2 output_size_inv;
layout(location = 3) out vec3 eye_pos_local;
layout(location = 4) out vec4 geom_aspect_and_overscan;
layout(location = 5) out vec3 global_to_local_row0;
layout(location = 6) out vec3 global_to_local_row1;
layout(location = 7) out vec3 global_to_local_row2;

void main()
{
    //  Fullscreen triangle from gl_VertexIndex (no vertex buffer).
    vec2 pos;
    if (gl_VertexIndex == 0)
        pos = vec2(-1.0, -1.0);
    else if (gl_VertexIndex == 1)
        pos = vec2(-1.0, 3.0);
    else
        pos = vec2(3.0, -1.0);
    gl_Position = vec4(pos, 0.0, 1.0);
    tex_uv = pos * 0.5 + 0.5;

    //  video_size == texture_size == SourceSize in this port (FBOs aren't
    //  padded), so both halves of the packed inverse are SourceSize.zw.
    video_and_texture_size_inv =
        vec4(1.0) / vec4(params.SourceSize.xy, params.SourceSize.xy);
    output_size_inv = vec2(1.0) / params.OutputSize.xy;

    //  Get aspect/overscan vectors from baked parameters:
    const float viewport_aspect_ratio = params.OutputSize.x/params.OutputSize.y;
    const float2 geom_aspect = get_aspect_vector(viewport_aspect_ratio);
    const float2 geom_overscan = get_geom_overscan_vector();
    geom_aspect_and_overscan = float4(geom_aspect, geom_overscan);

    //  No runtime tilt: identity local<->global rotation (baked).
    static const float3x3 global_to_local = geom_global_to_local_static;
    static const float3x3 local_to_global = geom_local_to_global_static;
    //  Decompose global_to_local into rows for the fragment stage.
    global_to_local_row0 = float3(global_to_local[0][0], global_to_local[0][1], global_to_local[0][2]);
    global_to_local_row1 = float3(global_to_local[1][0], global_to_local[1][1], global_to_local[1][2]);
    global_to_local_row2 = float3(global_to_local[2][0], global_to_local[2][1], global_to_local[2][2]);

    //  Get an optimal eye position based on geom_view_dist, viewport aspect,
    //  and CRT radius/rotation, then move it into the CRT's local frame:
    static const float geom_mode = geom_mode_static;
    const float3 eye_pos_global =
        get_ideal_global_eye_pos(local_to_global, geom_aspect, geom_mode);
    eye_pos_local = mul(global_to_local, eye_pos_global);
}
