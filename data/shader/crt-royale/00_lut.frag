#version 450

// crt-royale-xm29plus pass 0 (LUT) fragment stage.
// Applies the NEC XM29 color LUT to the raw RDP scanout before the CRT chain.
// Ported from slang-shaders reshade/shaders/LUT/LUT.slang: identical lookup math,
// with the MVP/TexCoord plumbing replaced by gopher64's binding model. Output is
// gamma-encoded color (UNORM target; no sRGB on this pass). Stream B.1.

layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 FragColor;

// set=0/binding=0 Params UBO (canonical block, per crt_royale_contract.md). The
// LUT pass reads no size uniforms, but declares the block so its sampler bindings
// align with the pipeline's UBO@0 + inputs@1..N model used by every other pass.
// Without this, the C++ side bound the scanout/LUT one slot too low and the LUT
// pass sampled the wrong textures (black screen).
layout(std140, set = 0, binding = 0) uniform Params
{
    vec4 SourceSize;
    vec4 OriginalSize;
    vec4 OutputSize;
    uint FrameCount;
} params;

layout(set = 0, binding = 1) uniform sampler2D Source;      // raw RDP scanout
layout(set = 0, binding = 2) uniform sampler2D SamplerLUT;  // NEC XM29 color LUT

// Guard against undefined values some GPUs introduce; keeps the blue lerp safe.
vec4 mixfix(vec4 a, vec4 b, float c) {
    return (a.z < 1.0) ? mix(a, b, c) : a;
}

void main() {
    vec2 LUT_Size = textureSize(SamplerLUT, 0);
    vec4 imgColor = texture(Source, vTexCoord.xy);
    float red = (imgColor.r * (LUT_Size.y - 1.0) + 0.4999) / (LUT_Size.y * LUT_Size.y);
    float green = (imgColor.g * (LUT_Size.y - 1.0) + 0.4999) / LUT_Size.y;
    float blue1 = (floor(imgColor.b * (LUT_Size.y - 1.0)) / LUT_Size.y) + red;
    float blue2 = (ceil(imgColor.b * (LUT_Size.y - 1.0)) / LUT_Size.y) + red;
    float mixer = clamp(max((imgColor.b - blue1) / (blue2 - blue1), 0.0), 0.0, 32.0);
    vec4 color1 = texture(SamplerLUT, vec2(blue1, green));
    vec4 color2 = texture(SamplerLUT, vec2(blue2, green));
    FragColor = mixfix(color1, color2, mixer);
}
