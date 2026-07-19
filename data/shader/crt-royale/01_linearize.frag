#version 450

//  crt-royale-xm29plus pass 1 (linearize) fragment stage.
//  Ported from slang-shaders/.../crt-royale-first-pass-linearize-crt-gamma-bob-
//  fields.slang. Linearizes the LUT-output (pass 0) by decoding CRT gamma, and
//  bobs interlaced fields so later passes can blur without field artifacts.
//
//  Gamma: this is FIRST_PASS, so the input (a UNORM, gamma-encoded LUT output)
//  is manually linearized by tex2D_linearize. The render target is an sRGB FBO,
//  so the hardware does the linear->sRGB encode on store; encode_output is a
//  passthrough here (not LAST_PASS). See gamma.inc for the full contract.
//  Stream B.0.

#define FIRST_PASS
#define SIMULATE_CRT_ON_LCD

#include "compat.inc"
#include "params.inc"
#include "gamma.inc"

//  set=0/binding=0 Params UBO, bound to BOTH stages (crt_royale_contract.md).
//  Every pass declares the full canonical block so its std140 offsets match the
//  fixed 64-byte block the C++ side uploads; this pass reads SourceSize and
//  FrameCount (interlace bob) and ignores the rest.
layout(std140, set = 0, binding = 0) uniform Params
{
    vec4 SourceSize;
    vec4 OriginalSize;
    vec4 OutputSize;
    uint FrameCount;
} params;

//  set=0/binding=1: Source = pass 0 (LUT) output.
layout(set = 0, binding = 1) uniform sampler2D Source;

layout(location = 0) in vec2 tex_uv;
layout(location = 1) in vec2 uv_step;
layout(location = 2) in float interlaced;
layout(location = 0) out vec4 FragColor;

void main()
{
    if (interlace_detect)
    {
        //  Sample the current line and the average of the previous/next line;
        //  tex2D_linearize decodes CRT gamma. Don't bother branching.
        vec2 v_step = vec2(0.0, uv_step.y);
        vec3 curr_line = tex2D_linearize(Source, tex_uv).rgb;
        vec3 last_line = tex2D_linearize(Source, tex_uv - v_step).rgb;
        vec3 next_line = tex2D_linearize(Source, tex_uv + v_step).rgb;
        vec3 interpolated_line = 0.5 * (last_line + next_line);

        //  If we're interlacing, determine which field curr_line is in.
        float modulus = interlaced + 1.0;
        float field_offset =
            mod(float(params.FrameCount) + float(interlace_bff), modulus);
        float curr_line_texel = tex_uv.y * params.SourceSize.y;
        //  under_half fixes a rounding bug around exact texel locations.
        float line_num_last = floor(curr_line_texel - under_half);
        float wrong_field = mod(line_num_last + field_offset, modulus);

        //  Select the current line in its field, else the bobbed average.
        vec3 color = mix(curr_line, interpolated_line, wrong_field);
        FragColor = encode_output(vec4(color, 1.0));
    }
    else
    {
        FragColor = encode_output(tex2D_linearize(Source, tex_uv));
    }
}
