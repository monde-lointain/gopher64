#ifndef CRT_ROYALE_SPECIAL_FUNCTIONS_INC
#define CRT_ROYALE_SPECIAL_FUNCTIONS_INC

//  Mathematical special functions for the crt-royale scanline beam model.
//  Translated from slang-shaders/include/special-functions.h:
//    erf()                  Gaussian error function (Abramowitz/Stegun approx)
//    gamma() / gamma_impl() real-numbered factorial (Lanczos approx)
//    normalized_ligamma()   normalized lower incomplete gamma, valid for s < 0.5
//  Each is provided for float and vec2/3/4 (GLSL has no function templates).
//  The hyperbolic-tangent erf approximation (erft) is omitted: it is only used
//  under ERF_FAST_APPROXIMATION, which this port never defines.
//
//  Requires compat.inc to be included first (only standard GLSL is used here,
//  so it is otherwise self-contained).

///////////////////////////  GAUSSIAN ERROR FUNCTION  //////////////////////////

vec4 erf6(vec4 x)
{
    vec4 sign_x = sign(x);
    vec4 t = vec4(1.0) / (vec4(1.0) + 0.47047 * abs(x));
    vec4 result = vec4(1.0) - t * (0.3480242 + t * (-0.0958798 + t * 0.7478556)) * exp(-(x * x));
    return result * sign_x;
}

vec3 erf6(vec3 x)
{
    vec3 sign_x = sign(x);
    vec3 t = vec3(1.0) / (vec3(1.0) + 0.47047 * abs(x));
    vec3 result = vec3(1.0) - t * (0.3480242 + t * (-0.0958798 + t * 0.7478556)) * exp(-(x * x));
    return result * sign_x;
}

vec2 erf6(vec2 x)
{
    vec2 sign_x = sign(x);
    vec2 t = vec2(1.0) / (vec2(1.0) + 0.47047 * abs(x));
    vec2 result = vec2(1.0) - t * (0.3480242 + t * (-0.0958798 + t * 0.7478556)) * exp(-(x * x));
    return result * sign_x;
}

float erf6(float x)
{
    float sign_x = sign(x);
    float t = 1.0 / (1.0 + 0.47047 * abs(x));
    float result = 1.0 - t * (0.3480242 + t * (-0.0958798 + t * 0.7478556)) * exp(-(x * x));
    return result * sign_x;
}

vec4 erf(vec4 x)  { return erf6(x); }
vec3 erf(vec3 x)  { return erf6(x); }
vec2 erf(vec2 x)  { return erf6(x); }
float erf(float x) { return erf6(x); }

///////////////////////////  COMPLETE GAMMA FUNCTION  //////////////////////////
//  s_inv = 1.0/s precomputed so callers can reuse it.

vec4 gamma_impl(vec4 s, vec4 s_inv)
{
    vec4 g = vec4(1.12906830989);
    vec4 c0 = vec4(0.8109119309638332633713423362694399653724431);
    vec4 c1 = vec4(0.4808354605142681877121661197951496120000040);
    vec4 e = vec4(2.71828182845904523536028747135266249775724709);
    vec4 sph = s + vec4(0.5);
    vec4 lanczos_sum = c0 + c1 / (s + vec4(1.0));
    vec4 base = (sph + g) / e;
    return (pow(base, sph) * lanczos_sum) * s_inv;
}

vec3 gamma_impl(vec3 s, vec3 s_inv)
{
    vec3 g = vec3(1.12906830989);
    vec3 c0 = vec3(0.8109119309638332633713423362694399653724431);
    vec3 c1 = vec3(0.4808354605142681877121661197951496120000040);
    vec3 e = vec3(2.71828182845904523536028747135266249775724709);
    vec3 sph = s + vec3(0.5);
    vec3 lanczos_sum = c0 + c1 / (s + vec3(1.0));
    vec3 base = (sph + g) / e;
    return (pow(base, sph) * lanczos_sum) * s_inv;
}

vec2 gamma_impl(vec2 s, vec2 s_inv)
{
    vec2 g = vec2(1.12906830989);
    vec2 c0 = vec2(0.8109119309638332633713423362694399653724431);
    vec2 c1 = vec2(0.4808354605142681877121661197951496120000040);
    vec2 e = vec2(2.71828182845904523536028747135266249775724709);
    vec2 sph = s + vec2(0.5);
    vec2 lanczos_sum = c0 + c1 / (s + vec2(1.0));
    vec2 base = (sph + g) / e;
    return (pow(base, sph) * lanczos_sum) * s_inv;
}

float gamma_impl(float s, float s_inv)
{
    float g = 1.12906830989;
    float c0 = 0.8109119309638332633713423362694399653724431;
    float c1 = 0.4808354605142681877121661197951496120000040;
    float e = 2.71828182845904523536028747135266249775724709;
    float sph = s + 0.5;
    float lanczos_sum = c0 + c1 / (s + 1.0);
    float base = (sph + g) / e;
    return (pow(base, sph) * lanczos_sum) * s_inv;
}

vec4 gamma(vec4 s)  { return gamma_impl(s, vec4(1.0) / s); }
vec3 gamma(vec3 s)  { return gamma_impl(s, vec3(1.0) / s); }
vec2 gamma(vec2 s)  { return gamma_impl(s, vec2(1.0) / s); }
float gamma(float s) { return gamma_impl(s, 1.0 / s); }

////////////////  INCOMPLETE GAMMA FUNCTIONS (RESTRICTED INPUT)  ///////////////
//  Lower incomplete gamma for small s and z (series, 4 terms).

vec4 ligamma_small_z_impl(vec4 s, vec4 z, vec4 s_inv)
{
    vec4 scale = pow(z, s);
    vec4 sum = s_inv;
    vec4 z_sq = z * z;
    sum -= z / (s + vec4(1.0));
    sum += z_sq / (2.0 * s + vec4(4.0));
    sum -= z * z_sq / (6.0 * s + vec4(18.0));
    return scale * sum;
}

vec3 ligamma_small_z_impl(vec3 s, vec3 z, vec3 s_inv)
{
    vec3 scale = pow(z, s);
    vec3 sum = s_inv;
    vec3 z_sq = z * z;
    sum -= z / (s + vec3(1.0));
    sum += z_sq / (2.0 * s + vec3(4.0));
    sum -= z * z_sq / (6.0 * s + vec3(18.0));
    return scale * sum;
}

vec2 ligamma_small_z_impl(vec2 s, vec2 z, vec2 s_inv)
{
    vec2 scale = pow(z, s);
    vec2 sum = s_inv;
    vec2 z_sq = z * z;
    sum -= z / (s + vec2(1.0));
    sum += z_sq / (2.0 * s + vec2(4.0));
    sum -= z * z_sq / (6.0 * s + vec2(18.0));
    return scale * sum;
}

float ligamma_small_z_impl(float s, float z, float s_inv)
{
    float scale = pow(z, s);
    float sum = s_inv;
    float z_sq = z * z;
    sum -= z / (s + 1.0);
    sum += z_sq / (2.0 * s + 4.0);
    sum -= z * z_sq / (6.0 * s + 18.0);
    return scale * sum;
}

//  Upper incomplete gamma for small s and large z (continued fraction, 4 terms).

vec4 uigamma_large_z_impl(vec4 s, vec4 z)
{
    vec4 numerator = pow(z, s) * exp(-z);
    vec4 denom = vec4(7.0) + z - s;
    denom = vec4(5.0) + z - s + (3.0 * s - vec4(9.0)) / denom;
    denom = vec4(3.0) + z - s + (2.0 * s - vec4(4.0)) / denom;
    denom = vec4(1.0) + z - s + (s - vec4(1.0)) / denom;
    return numerator / denom;
}

vec3 uigamma_large_z_impl(vec3 s, vec3 z)
{
    vec3 numerator = pow(z, s) * exp(-z);
    vec3 denom = vec3(7.0) + z - s;
    denom = vec3(5.0) + z - s + (3.0 * s - vec3(9.0)) / denom;
    denom = vec3(3.0) + z - s + (2.0 * s - vec3(4.0)) / denom;
    denom = vec3(1.0) + z - s + (s - vec3(1.0)) / denom;
    return numerator / denom;
}

vec2 uigamma_large_z_impl(vec2 s, vec2 z)
{
    vec2 numerator = pow(z, s) * exp(-z);
    vec2 denom = vec2(7.0) + z - s;
    denom = vec2(5.0) + z - s + (3.0 * s - vec2(9.0)) / denom;
    denom = vec2(3.0) + z - s + (2.0 * s - vec2(4.0)) / denom;
    denom = vec2(1.0) + z - s + (s - vec2(1.0)) / denom;
    return numerator / denom;
}

float uigamma_large_z_impl(float s, float z)
{
    float numerator = pow(z, s) * exp(-z);
    float denom = 7.0 + z - s;
    denom = 5.0 + z - s + (3.0 * s - 9.0) / denom;
    denom = 3.0 + z - s + (2.0 * s - 4.0) / denom;
    denom = 1.0 + z - s + (s - 1.0) / denom;
    return numerator / denom;
}

//  Normalized lower incomplete gamma for small s (two-branch, s < ~0.5).
//  s_inv = 1/s, gamma_s_inv = 1/gamma(s), precomputed for caller reuse.

vec4 normalized_ligamma_impl(vec4 s, vec4 z, vec4 s_inv, vec4 gamma_s_inv)
{
    vec4 thresh = vec4(0.775075);
    bvec4 z_is_large;
    z_is_large.x = z.x > thresh.x;
    z_is_large.y = z.y > thresh.y;
    z_is_large.z = z.z > thresh.z;
    z_is_large.w = z.w > thresh.w;
    vec4 large_z = vec4(1.0) - uigamma_large_z_impl(s, z) * gamma_s_inv;
    vec4 small_z = ligamma_small_z_impl(s, z, s_inv) * gamma_s_inv;
    return large_z * vec4(z_is_large) + small_z * vec4(not(z_is_large));
}

vec3 normalized_ligamma_impl(vec3 s, vec3 z, vec3 s_inv, vec3 gamma_s_inv)
{
    vec3 thresh = vec3(0.775075);
    bvec3 z_is_large;
    z_is_large.x = z.x > thresh.x;
    z_is_large.y = z.y > thresh.y;
    z_is_large.z = z.z > thresh.z;
    vec3 large_z = vec3(1.0) - uigamma_large_z_impl(s, z) * gamma_s_inv;
    vec3 small_z = ligamma_small_z_impl(s, z, s_inv) * gamma_s_inv;
    return large_z * vec3(z_is_large) + small_z * vec3(not(z_is_large));
}

vec2 normalized_ligamma_impl(vec2 s, vec2 z, vec2 s_inv, vec2 gamma_s_inv)
{
    vec2 thresh = vec2(0.775075);
    bvec2 z_is_large;
    z_is_large.x = z.x > thresh.x;
    z_is_large.y = z.y > thresh.y;
    vec2 large_z = vec2(1.0) - uigamma_large_z_impl(s, z) * gamma_s_inv;
    vec2 small_z = ligamma_small_z_impl(s, z, s_inv) * gamma_s_inv;
    return large_z * vec2(z_is_large) + small_z * vec2(not(z_is_large));
}

float normalized_ligamma_impl(float s, float z, float s_inv, float gamma_s_inv)
{
    float thresh = 0.775075;
    bool z_is_large = z > thresh;
    float large_z = 1.0 - uigamma_large_z_impl(s, z) * gamma_s_inv;
    float small_z = ligamma_small_z_impl(s, z, s_inv) * gamma_s_inv;
    return large_z * float(z_is_large) + small_z * float(!z_is_large);
}

vec4 normalized_ligamma(vec4 s, vec4 z)
{
    vec4 s_inv = vec4(1.0) / s;
    vec4 gamma_s_inv = vec4(1.0) / gamma_impl(s, s_inv);
    return normalized_ligamma_impl(s, z, s_inv, gamma_s_inv);
}

vec3 normalized_ligamma(vec3 s, vec3 z)
{
    vec3 s_inv = vec3(1.0) / s;
    vec3 gamma_s_inv = vec3(1.0) / gamma_impl(s, s_inv);
    return normalized_ligamma_impl(s, z, s_inv, gamma_s_inv);
}

vec2 normalized_ligamma(vec2 s, vec2 z)
{
    vec2 s_inv = vec2(1.0) / s;
    vec2 gamma_s_inv = vec2(1.0) / gamma_impl(s, s_inv);
    return normalized_ligamma_impl(s, z, s_inv, gamma_s_inv);
}

float normalized_ligamma(float s, float z)
{
    float s_inv = 1.0 / s;
    float gamma_s_inv = 1.0 / gamma_impl(s, s_inv);
    return normalized_ligamma_impl(s, z, s_inv, gamma_s_inv);
}

#endif  //  CRT_ROYALE_SPECIAL_FUNCTIONS_INC
