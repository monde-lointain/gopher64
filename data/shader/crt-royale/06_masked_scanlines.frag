#version 450

//  crt-royale-xm29plus pass 6 (masked scanlines) fragment stage.
//  Ported from slang-shaders/.../src/crt-royale-scanlines-horizontal-apply-
//  mask.{slang,h}. Horizontally resamples the vertically-scanlined image (with
//  per-channel convergence), applies halation, and multiplies in the phosphor
//  mask.
//
//  Sampler-excision surgery (baked mask_type=1 slot, mask_sample_mode=1 hardware
//  resize): the slang declared 8 samplers and branched on mask type/mode. Only
//  the slot-mask hardware-resize path survives, so the grille/shadow/MASK_RESIZE
//  samplers and every now-unreachable branch (MASK_RESIZE sampling, the
//  mask_type != slot cases, manual tiling) are deleted. The hardware-resize path
//  samples the large mipmapped slot LUT at the wrapped tile coords directly; the
//  Trilinear/Wrap sampler state is configured on the C++ side.
//
//  Gamma: intermediate sRGB-FBO pass (neither FIRST nor LAST), so tex2D_linearize
//  and encode_output are pass-throughs -- the sRGB inputs are already linear and
//  the hardware re-encodes on store. See gamma.inc.
//
//  Bindings (must match crt_royale.cpp InputBinding order): 1 VERTICAL_SCANLINES,
//  2 slot mask large, 3 BLOOM_APPROX, 4 HALATION_BLUR. BLOOM_APPROX is only read
//  by the (disabled) fake-bloom path; it is declared to keep this pass's
//  4-sampler binding table aligned with the pipeline's.
//  Stream stream/p6-mask-bright.

#define SIMULATE_CRT_ON_LCD

#include "compat.inc"
#include "params.inc"
#include "gamma.inc"
#include "scanline-functions.inc"

//  set=0/binding=0 Params UBO, bound to BOTH stages (crt_royale_contract.md).
//  The full canonical block keeps std140 offsets matching the C++ upload. Note
//  SourceSize here is the previous-pass (halation) FBO size, not VERTICAL_-
//  SCANLINES; the scanline FBO size is reconstructed from OriginalSize/OutputSize
//  in main() (see there). OutputSize also drives the mask tile count.
layout(std140, set = 0, binding = 0) uniform Params
{
    vec4 SourceSize;
    vec4 OriginalSize;
    vec4 OutputSize;
    uint FrameCount;
} params;

layout(set = 0, binding = 1) uniform sampler2D VERTICAL_SCANLINES;
layout(set = 0, binding = 2) uniform sampler2D mask_slot_texture_large;
layout(set = 0, binding = 3) uniform sampler2D BLOOM_APPROX;
layout(set = 0, binding = 4) uniform sampler2D HALATION_BLUR;

layout(location = 0) in vec2 tex_uv;
layout(location = 1) in vec2 mask_tiles_per_screen;
layout(location = 0) out vec4 FragColor;

void main()
{
    //  Horizontally resample the (vertically interpolated) scanline row, applying
    //  per-channel horizontal convergence. The resampler needs VERTICAL_SCANLINES'
    //  true pixel size for texel snapping, but params.SourceSize describes this
    //  pass's previous-pass input (the halation FBO), not VERTICAL_SCANLINES. Per
    //  kPassScaleRules the vertical-scanlines FBO (pass 2) is source.x wide (==
    //  OriginalSize.x, the raw scanout width kept at 1x through passes 0-2) and
    //  viewport.y tall (== this pass's OutputSize.y, as pass 6 is 1.0 viewport
    //  scale), so reconstruct its size from those fields.
    vec2 scanline_size = vec2(params.OriginalSize.x, params.OutputSize.y);
    vec2 scanline_size_inv = vec2(params.OriginalSize.z, params.OutputSize.w);
    vec3 scanline_color_dim = sample_rgb_scanline_horizontal(
        VERTICAL_SCANLINES, tex_uv, scanline_size, scanline_size_inv);
    float auto_dim_factor = levels_autodim_temp;

    //  Phosphor mask: hardware-resize the large slot LUT. In this mode the wrapped
    //  tile coord doubles as the tex_uv (the LUT holds a single tile and the
    //  sampler wraps + mipmaps), so sample it directly.
    vec2 mask_tex_uv = tex_uv * mask_tiles_per_screen;
    vec3 phosphor_mask_sample =
        tex2D_linearize(mask_slot_texture_large, mask_tex_uv).rgb;

    //  Halation models electrons exciting the wrong phosphors; it desaturates, so
    //  reduce it to a scalar and auto-dim it to match the scanlines. halation_-
    //  weight is baked 0 (xm29plus), so this reduces to the scanline color.
    vec3 halation_color = tex2D_linearize(HALATION_BLUR, tex_uv).rgb;
    vec3 halation_intensity_dim =
        vec3(dot(halation_color, vec3(auto_dim_factor / 3.0)));
    vec3 electron_intensity_dim =
        lerp(scanline_color_dim, halation_intensity_dim, halation_weight);

    //  Apply the mask. Real bloom happens later (passes 8-9), so there is no fake
    //  bloom here: output the dim, masked phosphor emission directly.
    vec3 phosphor_emission_dim = electron_intensity_dim * phosphor_mask_sample;
    FragColor = encode_output(vec4(phosphor_emission_dim, 1.0));
}
