#ifndef CRT_ROYALE_GAMMA_INC
#define CRT_ROYALE_GAMMA_INC

//  Gamma-aware texture reads and output encoding for the crt-royale port.
//  Translated from slang-shaders/include/gamma-management.h.
//
//  ==========================  GAMMA CONTRACT  ===============================
//  gopher64 runs the intermediate royale passes into VK_FORMAT_R8G8B8A8_SRGB
//  framebuffers, so the HARDWARE does linear<->sRGB on every store/load of those
//  FBOs. To avoid double gamma, the shader must NOT also encode/decode there.
//  This is exactly what gamma-management.h already does, switched by two macros
//  the *including pass* sets BEFORE including this file:
//
//    #define FIRST_PASS   - the pass reads a UNORM (gamma-encoded, not hardware
//                           sRGB) input and must manually linearize it. Only the
//                           LUT-output linearize pass (pass 1) sets this.
//    #define LAST_PASS    - the pass writes the UNORM swapchain and must manually
//                           encode display gamma. Only the final pass (10) sets it.
//
//  An intermediate sRGB pass sets NEITHER macro:
//    - decode_input()/tex2D_linearize() become a passthrough (hardware already
//      decoded the sRGB FBO on read), and
//    - encode_output() becomes a passthrough (hardware encodes on sRGB store).
//
//  Do NOT #define GAMMA_ENCODE_EVERY_FBO: we rely on sRGB FBOs, not manual
//  per-pass gamma. The only manual-gamma boundaries are the UNORM ones above.
//  ===========================================================================
//
//  Requires compat.inc and params.inc to be included first (vec aliases,
//  keyword strips, and crt_gamma/lcd_gamma).

//  Use the baked crt_gamma/lcd_gamma as the device gammas, and bypass the
//  SIMULATE_* device-gamma table the same way bind-shader-params.h does.
#define OVERRIDE_DEVICE_GAMMA
float gba_gamma = 3.5;  //  Unused here, but get_gba_gamma() must resolve.

///////////////////////////////  BASE CONSTANTS  ///////////////////////////////

float ntsc_gamma = 2.2;
float pal_gamma = 2.8;
float crt_reference_gamma_high = 2.5;
float crt_reference_gamma_low = 2.35;
float lcd_reference_gamma = 2.5;
float crt_office_gamma = 2.2;
float lcd_office_gamma = 2.2;

bool assume_opaque_alpha = false;

///////////////////////  DERIVED CONSTANTS AS FUNCTIONS  ///////////////////////

//  OVERRIDE_DEVICE_GAMMA: the user (params.inc) defines crt_gamma / lcd_gamma.
float get_crt_gamma() { return crt_gamma; }
float get_gba_gamma() { return gba_gamma; }
float get_lcd_gamma() { return lcd_gamma; }

//  No OVERRIDE_FINAL_GAMMA: derive from the SIMULATE_* selection. The royale
//  pipeline always defines SIMULATE_CRT_ON_LCD (params.inc), so the first pass
//  decodes CRT gamma and the last encodes LCD gamma.
float get_intermediate_gamma() { return ntsc_gamma; }
float get_input_gamma() { return get_crt_gamma(); }
float get_output_gamma() { return get_lcd_gamma(); }

//  Per-pass input/output gamma, switched by FIRST_PASS / LAST_PASS. We never
//  define GAMMA_ENCODE_EVERY_FBO, so intermediate passes are passthrough.
#ifdef FIRST_PASS
    bool linearize_input = true;
    float get_pass_input_gamma() { return get_input_gamma(); }
#else
    bool linearize_input = false;
    float get_pass_input_gamma() { return 1.0; }
#endif
#ifdef LAST_PASS
    bool gamma_encode_output = true;
    float get_pass_output_gamma() { return get_output_gamma(); }
#else
    bool gamma_encode_output = false;
    float get_pass_output_gamma() { return 1.0; }
#endif

bool gamma_aware_bilinear = !linearize_input;

//////////////////////  COLOR ENCODING/DECODING FUNCTIONS  /////////////////////

vec4 encode_output(vec4 color)
{
    if (gamma_encode_output)
    {
        if (assume_opaque_alpha)
            return vec4(pow(color.rgb, vec3(1.0 / get_pass_output_gamma())), 1.0);
        else
            return vec4(pow(color.rgb, vec3(1.0 / get_pass_output_gamma())), color.a);
    }
    else
    {
        return color;
    }
}

vec4 decode_input(vec4 color)
{
    if (linearize_input)
    {
        if (assume_opaque_alpha)
            return vec4(pow(color.rgb, vec3(get_pass_input_gamma())), 1.0);
        else
            return vec4(pow(color.rgb, vec3(get_pass_input_gamma())), color.a);
    }
    else
    {
        return color;
    }
}

vec4 decode_gamma_input(vec4 color, vec3 gamma)
{
    if (assume_opaque_alpha)
        return vec4(pow(color.rgb, gamma), 1.0);
    else
        return vec4(pow(color.rgb, gamma), color.a);
}

///////////////////////////  TEXTURE LOOKUP WRAPPERS  //////////////////////////
//  Replace tex2D() calls with these so gamma handling stays consistent.

vec4 tex2D_linearize(sampler2D tex, vec2 tex_coords)
{   return decode_input(texture(tex, tex_coords));   }

vec4 tex2D_linearize(sampler2D tex, vec3 tex_coords)
{   return decode_input(texture(tex, tex_coords.xy));   }

vec4 tex2D_linearize(sampler2D tex, vec2 tex_coords, int texel_off)
{   return decode_input(textureLod(tex, tex_coords, float(texel_off)));   }

vec4 tex2D_linearize(sampler2D tex, vec3 tex_coords, int texel_off)
{   return decode_input(textureLod(tex, tex_coords.xy, float(texel_off)));   }

vec4 tex2Dlod_linearize(sampler2D tex, vec4 tex_coords)
{   return decode_input(textureLod(tex, tex_coords.xy, 0.0));   }

vec4 tex2Dlod_linearize(sampler2D tex, vec4 tex_coords, int texel_off)
{   return decode_input(textureLod(tex, tex_coords.xy, float(texel_off)));   }

//  Manually-specified-gamma lookups (for LUTs and pass0 input in later passes).
vec4 tex2D_linearize_gamma(sampler2D tex, vec2 tex_coords, vec3 gamma)
{   return decode_gamma_input(texture(tex, tex_coords), gamma);   }

vec4 tex2D_linearize_gamma(sampler2D tex, vec3 tex_coords, vec3 gamma)
{   return decode_gamma_input(texture(tex, tex_coords.xy), gamma);   }

vec4 tex2Dlod_linearize_gamma(sampler2D tex, vec4 tex_coords, vec3 gamma)
{   return decode_gamma_input(textureLod(tex, tex_coords.xy, 0.0), gamma);   }

vec4 tex2Dlod_linearize_gamma(sampler2D tex, vec4 tex_coords, int texel_off, vec3 gamma)
{   return decode_gamma_input(textureLod(tex, tex_coords.xy, float(texel_off)), gamma);   }

#endif  //  CRT_ROYALE_GAMMA_INC
