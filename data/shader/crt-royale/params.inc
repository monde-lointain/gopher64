#ifndef CRT_ROYALE_PARAMS_INC
#define CRT_ROYALE_PARAMS_INC

//  Baked crt-royale-xm29plus parameters for the gopher64 port.
//
//  This replaces the runtime `global` UBO + #pragma parameter machinery of
//  slang-shaders/.../bind-shader-params.h. Every value the preset would feed at
//  runtime is hard-baked here as a compile-time constant, so no parameter UBO is
//  needed. The contract: RUNTIME_* options are OFF (equivalent to upstream's
//  HARDCODE_SETTINGS path), so the cooked names below are the only definitions.
//
//  Sources translated/merged into this file:
//   - presets/crt-royale-xm29plus.slangp  (the `parameters` block, the look)
//   - crt-royale/src/bind-shader-params.h  (HARDCODE_SETTINGS clamping)
//   - crt-royale/user-settings.h           (static-only codepath options)
//   - crt-royale/src/user-cgp-constants.h  (mask LUT + scale constants)
//   - crt-royale/src/derived-settings-and-constants.h  (math constants only)
//
//  Deliberate deviations from the preset (see crt_royale_contract.md):
//   - geom_aspect_ratio is N64 4:3, NOT the preset's SNES 432:329.
//   - geom_mode_runtime = 0 (flat, no curvature), mask_type = 1 (slot mask),
//     mask_sample_mode = 1 (hardware resize). levels_autodim_temp baked at 0.5.
//
//  Requires compat.inc to be included first (vec aliases, keyword strips).

//////////////////////////////  MATH CONSTANTS  ///////////////////////////////
//  From derived-settings-and-constants.h.

#define pi (3.141592653589)
//  Subtract slightly less than 0.5 before floor() to dodge a texel-center
//  rounding bug present on many GPU drivers.
#define under_half (0.4995)
//  Avoid divide-by-zero; 2^-16. Overloads for float/vec via abs()/max().
#define FIX_ZERO(c) (max(abs(c), 0.0000152587890625))

//  The royale pipeline always simulates a CRT viewed on an LCD: the first pass
//  decodes CRT gamma and the last encodes display gamma (see gamma.inc).
#ifndef SIMULATE_CRT_ON_LCD
#define SIMULATE_CRT_ON_LCD
#endif

/////////////////////////  BAKED SHADER PARAMETERS  ///////////////////////////
//  Values are the preset's `parameters` entries after bind-shader-params.h's
//  range clamps (most are already in range). Parenthesized so they paste safely
//  into larger expressions (including unary minus).

//  GAMMA:
#define crt_gamma (2.4)
#define lcd_gamma (2.4)

//  LEVELS MANAGEMENT:
#define levels_contrast (0.75)
//  Precision contract: auto-dim between 8-bit sRGB passes to avoid clipping.
#define levels_autodim_temp (0.5)

//  HALATION / DIFFUSION / BLOOM:
#define halation_weight (0.0)
#define diffusion_weight (0.075)
#define bloom_underestimate_levels (1.2)
#define bloom_excess (0.335)
//  BLOOM_APPROX filter: 2.0 = true 4x4 Gaussian resize (the "real bloom" path).
#define bloom_approx_filter_static (2.0)
#define bloom_approx_filter (2.0)

//  ELECTRON BEAM SCANLINE DISTRIBUTION:
#define beam_num_scanlines (3.0)
#define beam_generalized_gaussian (true)
#define beam_antialias_level (1.0)
#define beam_min_sigma (0.075)
#define beam_max_sigma (0.25)
#define beam_spot_shape_function (0.0)
#define beam_spot_power (0.33)
#define beam_min_shape (2.0)
#define beam_max_shape (2.0)
#define beam_shape_power (0.25)
#define beam_horiz_filter (0.0)
#define beam_horiz_sigma (0.555)
#define beam_horiz_linear_rgb_weight (1.0)
#define beam_misconvergence (true)
//  Convergence offsets in scanlines (R/G/B, x/y). xm29plus shifts R/B in y.
#define convergence_offset_x_r (0.0)
#define convergence_offset_x_g (0.0)
#define convergence_offset_x_b (0.0)
#define convergence_offset_y_r (0.2)
#define convergence_offset_y_g (0.0)
#define convergence_offset_y_b (-0.2)
//  Interlacing: detect on, top-field-first, 1080i detection off.
#define interlace_detect (true)
#define interlace_bff (false)
#define interlace_1080i (false)

//  ANTIALIASING (final geometry pass):
#define aa_level (12.0)
#define aa_filter (6.0)
#define aa_temporal (false)
#define aa_subpixel_r_offset_static (vec2(-1.0 / 3.0, 0.0))
#define aa_subpixel_r_offset_x_runtime (-0.333333)
#define aa_subpixel_r_offset_y_runtime (0.0)
#define aa_cubic_c (0.5)
#define aa_gauss_sigma (0.5)

//  PHOSPHOR MASK:
#define mask_type (1.0)
//  Hardware-resize the large mipmapped slot-mask LUT (sample mode 1).
#define mask_sample_mode_static (1.0)
#define mask_sample_mode_desired (1.0)
#define mask_specify_num_triads (0.0)
#define mask_triad_size_desired (3.0)
#define mask_num_triads_desired (480.0)
#define mask_sinc_lobes (3.0)
#define mask_min_allowed_triad_size (2.0)

//  GEOMETRY (flat: geom_mode 0, so most of these only size the mask/scanlines):
#define geom_mode_runtime (0.0)
#define geom_radius (3.0)
#define geom_view_dist (2.0)
#define geom_tilt_angle_x (0.0)
#define geom_tilt_angle_y (0.0)
//  DEVIATION: N64 4:3 instead of the preset's 432:329.
#define geom_aspect_ratio_x (4.0)
#define geom_aspect_ratio_y (3.0)
#define geom_overscan_x (1.0)
#define geom_overscan_y (1.0)
#define geom_force_correct_tangent_matrix (true)

//  BORDERS:
#define border_size (0.005)
#define border_darkness (0.0)
#define border_compress (2.5)

//  Color LUT size (xm29plus capture is 1024x32, 32 slices).
#define LUT_Size (32.0)

///////////////////////////  USER-CGP CONSTANTS  //////////////////////////////
//  From user-cgp-constants.h: properties of the mask LUTs and pass scales the
//  shader code needs but the .slangp can't pass through directly.

#define bloom_approx_size_x (320.0)
#define bloom_approx_size_x_for_fake (400.0)
#define mask_resize_viewport_scale (vec2(0.0625, 0.0625))
#define geom_max_aspect_ratio (4.0 / 3.0)
#define mask_texture_small_size (vec2(64.0, 64.0))
#define mask_texture_large_size (vec2(512.0, 512.0))
#define mask_triads_per_tile (8.0)
//  Average mask brightness, used to compensate for the dimming each mask causes.
#define mask_grille_avg_color (53.0 / 255.0)
#define mask_slot_avg_color (46.0 / 255.0)
#define mask_shadow_avg_color (41.0 / 255.0)

///////////////////////////  PARAMETER ACCESSORS  /////////////////////////////
//  Vector/"cooked" accessors mirrored from bind-shader-params.h so the royale
//  source headers can call them unchanged.

vec2 get_aspect_vector(float geom_aspect_ratio)
{
    //  Clamp to geom_max_aspect_ratio and drop absolute scale (curvature uv-map).
    float geom_clamped_aspect_ratio = min(geom_aspect_ratio, geom_max_aspect_ratio);
    return normalize(vec2(geom_clamped_aspect_ratio, 1.0));
}

vec2 get_geom_overscan_vector()
{
    return vec2(geom_overscan_x, geom_overscan_y);
}

vec2 get_geom_tilt_angle_vector()
{
    return vec2(geom_tilt_angle_x, geom_tilt_angle_y);
}

vec3 get_convergence_offsets_x_vector()
{
    return vec3(convergence_offset_x_r, convergence_offset_x_g, convergence_offset_x_b);
}

vec3 get_convergence_offsets_y_vector()
{
    return vec3(convergence_offset_y_r, convergence_offset_y_g, convergence_offset_y_b);
}

vec2 get_convergence_offsets_r_vector()
{
    return vec2(convergence_offset_x_r, convergence_offset_y_r);
}

vec2 get_convergence_offsets_g_vector()
{
    return vec2(convergence_offset_x_g, convergence_offset_y_g);
}

vec2 get_convergence_offsets_b_vector()
{
    return vec2(convergence_offset_x_b, convergence_offset_y_b);
}

vec2 get_aa_subpixel_r_offset()
{
    //  RUNTIME_ANTIALIAS_SUBPIXEL_OFFSETS is off, so use the static offset.
    return aa_subpixel_r_offset_static;
}

float get_mask_amplify()
{
    float mask_grille_amplify = 1.0 / mask_grille_avg_color;
    float mask_slot_amplify = 1.0 / mask_slot_avg_color;
    float mask_shadow_amplify = 1.0 / mask_shadow_avg_color;
    return mask_type < 0.5 ? mask_grille_amplify :
        mask_type < 1.5 ? mask_slot_amplify :
        mask_shadow_amplify;
}

float get_mask_sample_mode()
{
    //  Runtime mask-mode select is off and the mask is manually resizable, so
    //  the baked desired mode is used directly (1.0 = hardware resize).
    return mask_sample_mode_static;
}

#endif  //  CRT_ROYALE_PARAMS_INC
