#ifndef CRT_ROYALE_SCANLINE_FUNCTIONS_INC
#define CRT_ROYALE_SCANLINE_FUNCTIONS_INC

//  Electron-beam scanline distribution and interlacing helpers for crt-royale.
//  Translated from slang-shaders/.../src/scanline-functions.h. The Cg-style
//  float3/mul/lerp/fmod spellings are left intact and resolved by compat.inc.
//
//  The only edits versus upstream:
//   - The upstream #includes are dropped; this file pulls in special-functions
//     directly and documents its remaining prerequisites below.
//   - References to the runtime `global` UBO are rebound to baked params:
//       global.interlace_bff -> interlace_bff   (in get_last_scanline_uv).
//     (global.beam_horiz_linear_rgb_weight only appears in a preprocessed-out
//      branch, so it needs no change.)
//
//  Prerequisites (include in this order before this file):
//    compat.inc, params.inc, gamma.inc       (vec aliases, baked params, gamma)
//  special-functions.inc is a hard dependency and is included here.

#include "special-functions.inc"

/////////////////////////////  SCANLINE FUNCTIONS  /////////////////////////////

inline float3 get_gaussian_sigma(const float3 color, const float sigma_range)
{
    //  Returns the beam standard deviation for "color": wider for bright colors.
    if(beam_spot_shape_function < 0.5)
    {
        //  Use a power function:
        return float3(beam_min_sigma) + sigma_range *
            pow(color, float3(beam_spot_power));
    }
    else
    {
        //  Use a spherical function:
        const float3 color_minus_1 = color - float3(1.0);
        return float3(beam_min_sigma) + sigma_range *
            sqrt(float3(1.0) - color_minus_1*color_minus_1);
    }
}

inline float3 get_generalized_gaussian_beta(const float3 color,
    const float shape_range)
{
    //  Returns the type-I generalized Gaussian "shape" parameter beta.
    return beam_min_shape + shape_range * pow(color, float3(beam_shape_power));
}

float3 scanline_gaussian_integral_contrib(const float3 dist,
    const float3 color, const float pixel_height, const float sigma_range)
{
    //  Integrate a Gaussian beam profile over a pixel using erf().
    const float3 sigma = get_gaussian_sigma(color, sigma_range);
    const float3 ph_offset = float3(pixel_height * 0.5);
    const float3 denom_inv = 1.0/(sigma*sqrt(2.0));
    const float3 integral_high = erf((dist + ph_offset)*denom_inv);
    const float3 integral_low = erf((dist - ph_offset)*denom_inv);
    return color * 0.5*(integral_high - integral_low)/pixel_height;
}

float3 scanline_generalized_gaussian_integral_contrib(float3 dist,
    float3 color, float pixel_height, float sigma_range,
    float shape_range)
{
    //  Integrate a generalized Gaussian beam profile over a pixel.
    const float3 alpha = sqrt(2.0) * get_gaussian_sigma(color, sigma_range);
    const float3 beta = get_generalized_gaussian_beta(color, shape_range);
    const float3 alpha_inv = float3(1.0)/alpha;
    const float3 s = float3(1.0)/beta;
    const float3 ph_offset = float3(pixel_height * 0.5);
    const float3 gamma_s_inv = float3(1.0)/gamma_impl(s, beta);
    const float3 dist1 = dist + ph_offset;
    const float3 dist0 = dist - ph_offset;
    const float3 integral_high = sign(dist1) * normalized_ligamma_impl(
        s, pow(abs(dist1)*alpha_inv, beta), beta, gamma_s_inv);
    const float3 integral_low = sign(dist0) * normalized_ligamma_impl(
        s, pow(abs(dist0)*alpha_inv, beta), beta, gamma_s_inv);
    return color * 0.5*(integral_high - integral_low)/pixel_height;
}

float3 scanline_gaussian_sampled_contrib(const float3 dist, const float3 color,
    const float pixel_height, const float sigma_range)
{
    //  Point-sample a Gaussian beam profile (optionally 3x antialiased).
    const float3 sigma = get_gaussian_sigma(color, sigma_range);
    const float3 sigma_inv = float3(1.0)/sigma;
    const float3 inner_denom_inv = 0.5 * sigma_inv * sigma_inv;
    const float3 outer_denom_inv = sigma_inv/sqrt(2.0*pi);
    if(beam_antialias_level > 0.5)
    {
        const float3 sample_offset = float3(pixel_height/3.0);
        const float3 dist2 = dist + sample_offset;
        const float3 dist3 = abs(dist - sample_offset);
        const float3 scale = color/3.0 * outer_denom_inv;
        const float3 weight1 = exp(-(dist*dist)*inner_denom_inv);
        const float3 weight2 = exp(-(dist2*dist2)*inner_denom_inv);
        const float3 weight3 = exp(-(dist3*dist3)*inner_denom_inv);
        return scale * (weight1 + weight2 + weight3);
    }
    else
    {
        return color*exp(-(dist*dist)*inner_denom_inv)*outer_denom_inv;
    }
}

float3 scanline_generalized_gaussian_sampled_contrib(float3 dist,
    float3 color, float pixel_height, float sigma_range,
    float shape_range)
{
    //  Point-sample a generalized Gaussian beam profile (optionally 3x AA).
    const float3 alpha = sqrt(2.0) * get_gaussian_sigma(color, sigma_range);
    const float3 beta = get_generalized_gaussian_beta(color, shape_range);
    const float3 alpha_inv = float3(1.0)/alpha;
    const float3 beta_inv = float3(1.0)/beta;
    const float3 scale = color * beta * 0.5 * alpha_inv /
        gamma_impl(beta_inv, beta);
    if(beam_antialias_level > 0.5)
    {
        const float3 sample_offset = float3(pixel_height/3.0);
        const float3 dist2 = dist + sample_offset;
        const float3 dist3 = abs(dist - sample_offset);
        const float3 weight1 = exp(-pow(abs(dist*alpha_inv), beta));
        const float3 weight2 = exp(-pow(abs(dist2*alpha_inv), beta));
        const float3 weight3 = exp(-pow(abs(dist3*alpha_inv), beta));
        return scale/3.0 * (weight1 + weight2 + weight3);
    }
    else
    {
        return scale * exp(-pow(abs(dist*alpha_inv), beta));
    }
}

inline float3 scanline_contrib(float3 dist, float3 color,
    float pixel_height, const float sigma_range, const float shape_range)
{
    //  A scanline's light output over a pixel, dispatched by user codepath.
    if(beam_generalized_gaussian)
    {
        if(beam_antialias_level > 1.5)
        {
            return scanline_generalized_gaussian_integral_contrib(
                dist, color, pixel_height, sigma_range, shape_range);
        }
        else
        {
            return scanline_generalized_gaussian_sampled_contrib(
                dist, color, pixel_height, sigma_range, shape_range);
        }
    }
    else
    {
        if(beam_antialias_level > 1.5)
        {
            return scanline_gaussian_integral_contrib(
                dist, color, pixel_height, sigma_range);
        }
        else
        {
            return scanline_gaussian_sampled_contrib(
                dist, color, pixel_height, sigma_range);
        }
    }
}

inline float3 get_raw_interpolated_color(const float3 color0,
    const float3 color1, const float3 color2, const float3 color3,
    const float4 weights)
{
    //  Use max to avoid bizarre artifacts from negative colors:
    return max(mul(weights, float4x3(color0, color1, color2, color3)), 0.0);
}

float3 get_interpolated_linear_color(const float3 color0, const float3 color1,
    const float3 color2, const float3 color3, const float4 weights)
{
    //  Interpolate between 4 colors, blending in linear and/or gamma RGB per
    //  beam_horiz_linear_rgb_weight. Returns a linear-RGB result.
    const float intermediate_gamma = get_intermediate_gamma();
    //  Branch if beam_horiz_linear_rgb_weight is static (it is, here):
    #ifndef RUNTIME_SCANLINES_HORIZ_FILTER_COLORSPACE
        #define SCANLINES_BRANCH_FOR_LINEAR_RGB_WEIGHT
    #else
        #ifdef DRIVERS_ALLOW_DYNAMIC_BRANCHES
            #define SCANLINES_BRANCH_FOR_LINEAR_RGB_WEIGHT
        #endif
    #endif
    #ifdef SCANLINES_BRANCH_FOR_LINEAR_RGB_WEIGHT
        #ifdef GAMMA_ENCODE_EVERY_FBO
            const float3 gamma_mixed_color = pow(get_raw_interpolated_color(
                color0, color1, color2, color3, weights), float3(intermediate_gamma));
            if(beam_horiz_linear_rgb_weight > 0.0)
            {
                const float3 linear_mixed_color = get_raw_interpolated_color(
                    pow(color0, float3(intermediate_gamma)),
                    pow(color1, float3(intermediate_gamma)),
                    pow(color2, float3(intermediate_gamma)),
                    pow(color3, float3(intermediate_gamma)),
                    weights);
                return lerp(gamma_mixed_color, linear_mixed_color,
                    beam_horiz_linear_rgb_weight);
            }
            else
            {
                return gamma_mixed_color;
            }
        #else
            const float3 linear_mixed_color = get_raw_interpolated_color(
                color0, color1, color2, color3, weights);
            if(beam_horiz_linear_rgb_weight < 1.0)
            {
                const float3 gamma_mixed_color = get_raw_interpolated_color(
                    pow(color0, float3(1.0/intermediate_gamma)),
                    pow(color1, float3(1.0/intermediate_gamma)),
                    pow(color2, float3(1.0/intermediate_gamma)),
                    pow(color3, float3(1.0/intermediate_gamma)),
                    weights);
                return lerp(gamma_mixed_color, linear_mixed_color,
                    beam_horiz_linear_rgb_weight);
            }
            else
            {
                return linear_mixed_color;
            }
        #endif  //  GAMMA_ENCODE_EVERY_FBO
    #else
        #ifdef GAMMA_ENCODE_EVERY_FBO
            const float3 gamma_mixed_color = pow(get_raw_interpolated_color(
                color0, color1, color2, color3, weights), intermediate_gamma);
            const float3 linear_mixed_color = get_raw_interpolated_color(
                pow(color0, float3(intermediate_gamma)),
                pow(color1, float3(intermediate_gamma)),
                pow(color2, float3(intermediate_gamma)),
                pow(color3, float3(intermediate_gamma)),
                weights);
            return lerp(gamma_mixed_color, linear_mixed_color,
                beam_horiz_linear_rgb_weight);
        #else
            const float3 linear_mixed_color = get_raw_interpolated_color(
                color0, color1, color2, color3, weights);
            const float3 gamma_mixed_color = get_raw_interpolated_color(
                    pow(color0, float3(1.0/intermediate_gamma)),
                    pow(color1, float3(1.0/intermediate_gamma)),
                    pow(color2, float3(1.0/intermediate_gamma)),
                    pow(color3, float3(1.0/intermediate_gamma)),
                    weights);
            return lerp(gamma_mixed_color, linear_mixed_color,
                beam_horiz_linear_rgb_weight);
        #endif  //  GAMMA_ENCODE_EVERY_FBO
    #endif  //  SCANLINES_BRANCH_FOR_LINEAR_RGB_WEIGHT
}

float3 get_scanline_color(const sampler2D tex, const float2 scanline_uv,
    const float2 uv_step_x, const float4 weights)
{
    //  Horizontally interpolate a texture lookup using 2-4 nearby texels.
    const float3 color1 = texture(tex, scanline_uv).rgb;
    const float3 color2 = texture(tex, scanline_uv + uv_step_x).rgb;
    float3 color0 = float3(0.0);
    float3 color3 = float3(0.0);
    if(beam_horiz_filter > 0.5)
    {
        color0 = texture(tex, scanline_uv - uv_step_x).rgb;
        color3 = texture(tex, scanline_uv + 2.0 * uv_step_x).rgb;
    }
    return get_interpolated_linear_color(color0, color1, color2, color3, weights);
}

float3 sample_single_scanline_horizontal(const sampler2D tex,
    const float2 tex_uv, const float2 tex_size,
    const float2 texture_size_inv)
{
    //  Snap to the previous texel and resample horizontally (Quilez/Gaussian/
    //  Lanczos2 weights per beam_horiz_filter).
    const float2 curr_texel = tex_uv * tex_size;
    const float2 prev_texel =
        floor(curr_texel - float2(under_half)) + float2(0.5);
    const float2 prev_texel_hor = float2(prev_texel.x, curr_texel.y);
    const float2 prev_texel_hor_uv = prev_texel_hor * texture_size_inv;
    const float prev_dist = curr_texel.x - prev_texel_hor.x;
    const float4 sample_dists = float4(1.0 + prev_dist, prev_dist,
        1.0 - prev_dist, 2.0 - prev_dist);
    float4 weights;
    if(beam_horiz_filter < 0.5)
    {
        //  Quilez:
        const float x = sample_dists.y;
        const float w2 = x*x*x*(x*(x*6.0 - 15.0) + 10.0);
        weights = float4(0.0, 1.0 - w2, w2, 0.0);
    }
    else if(beam_horiz_filter < 1.5)
    {
        //  Gaussian:
        float inner_denom_inv = 1.0/(2.0*beam_horiz_sigma*beam_horiz_sigma);
        weights = exp(-(sample_dists*sample_dists)*inner_denom_inv);
    }
    else
    {
        //  Lanczos2:
        const float4 pi_dists = FIX_ZERO(sample_dists * pi);
        weights = 2.0 * sin(pi_dists) * sin(pi_dists * 0.5) /
            (pi_dists * pi_dists);
    }
    const float4 final_weights = weights/dot(weights, float4(1.0));
    const float2 uv_step_x = float2(texture_size_inv.x, 0.0);
    return get_scanline_color(
        tex, prev_texel_hor_uv, uv_step_x, final_weights);
}

float3 sample_rgb_scanline_horizontal(const sampler2D tex,
    const float2 tex_uv, const float2 tex_size,
    const float2 texture_size_inv)
{
    //  Resample with optional per-channel convergence offsets.
    if(beam_misconvergence)
    {
        const float3 convergence_offsets_rgb =
            get_convergence_offsets_x_vector();
        const float3 offset_u_rgb =
            convergence_offsets_rgb * texture_size_inv.xxx;
        const float2 scanline_uv_r = tex_uv - float2(offset_u_rgb.r, 0.0);
        const float2 scanline_uv_g = tex_uv - float2(offset_u_rgb.g, 0.0);
        const float2 scanline_uv_b = tex_uv - float2(offset_u_rgb.b, 0.0);
        const float3 sample_r = sample_single_scanline_horizontal(
            tex, scanline_uv_r, tex_size, texture_size_inv);
        const float3 sample_g = sample_single_scanline_horizontal(
            tex, scanline_uv_g, tex_size, texture_size_inv);
        const float3 sample_b = sample_single_scanline_horizontal(
            tex, scanline_uv_b, tex_size, texture_size_inv);
        return float3(sample_r.r, sample_g.g, sample_b.b);
    }
    else
    {
        return sample_single_scanline_horizontal(tex, tex_uv, tex_size,
            texture_size_inv);
    }
}

float2 get_last_scanline_uv(const float2 tex_uv, const float2 tex_size,
    const float2 texture_size_inv, const float2 il_step_multiple,
    const float frame_count, out float dist)
{
    //  Texture coords for the last/upper scanline, accounting for interlacing.
    const float field_offset = floor(il_step_multiple.y * 0.75) *
        fmod(frame_count + float(interlace_bff), 2.0);
    const float2 curr_texel = tex_uv * tex_size;
    const float2 prev_texel_num = floor(curr_texel - float2(under_half));
    const float wrong_field = fmod(
        prev_texel_num.y + field_offset, il_step_multiple.y);
    const float2 scanline_texel_num = prev_texel_num - float2(0.0, wrong_field);
    const float2 scanline_texel = scanline_texel_num + float2(0.5);
    const float2 scanline_uv = scanline_texel * texture_size_inv;
    dist = (curr_texel.y - scanline_texel.y)/il_step_multiple.y;
    return scanline_uv;
}

inline bool is_interlaced(float num_lines)
{
    //  Detect interlacing based on the number of active lines in the source.
    if(interlace_detect)
    {
        const bool sd_interlace = ((num_lines > 288.5) && (num_lines < 576.5));
        const bool hd_interlace = bool(interlace_1080i) ?
            ((num_lines > 1079.5) && (num_lines < 1080.5)) :
            false;
        return (sd_interlace || hd_interlace);
    }
    else
    {
        return false;
    }
}

#endif  //  CRT_ROYALE_SCANLINE_FUNCTIONS_INC
