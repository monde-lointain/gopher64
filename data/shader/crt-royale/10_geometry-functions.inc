#ifndef CRT_ROYALE_GEOMETRY_FUNCTIONS_INC
#define CRT_ROYALE_GEOMETRY_FUNCTIONS_INC

//  Screen-curvature, eye-placement, tangent-matrix and border helpers for the
//  crt-royale final geometry pass.  Translated near-verbatim from
//  slang-shaders/.../src/geometry-functions.h (crt-royale, GPLv2+,
//  Copyright (C) 2014 TroggleMonkey).  The Cg-style float3/mul/atan2/rsqrt
//  spellings are kept and resolved by compat.inc.
//
//  Edits versus upstream:
//   - The upstream #includes (user-settings.h, derived-settings-and-constants.h,
//     bind-shader-params.h) are dropped; the baked params.inc supplies every
//     global these functions read (geom_radius, geom_view_dist, geom_aspect,
//     geom_force_correct_tangent_matrix, border_size/darkness/compress, pi,
//     FIX_ZERO).
//   - get_border_dim_factor keeps upstream's "const then reassign" idiom; it
//     only works because compat.inc strips const.
//
//  Derivative handling: get_curved_video_uv_coords_and_tangent_matrix uses
//  ddx/ddy when DRIVERS_ALLOW_DERIVATIVES is defined.  The fragment stage
//  defines it (Vulkan GLSL has dFdx/dFdy); the vertex stage does NOT, so this
//  file compiles cleanly there too (the non-derivative tangent path is used).
//  With geom_mode baked to 0 (flat) the curvature path is never *called*, but it
//  must still compile, so the full machinery is translated.
//
//  Prerequisites (include in this order before this file):
//    compat.inc, params.inc, gamma.inc

////////////////////////////  MACROS AND CONSTANTS  ////////////////////////////

//  Curvature-related constants:
#define MAX_POINT_CLOUD_SIZE 9


/////////////////////////////  CURVATURE FUNCTIONS /////////////////////////////

float2 quadratic_solve(const float a, const float b_over_2, const float c)
{
    //  Requires:   1.) a, b, and c are quadratic formula coefficients
    //              2.) b_over_2 = b/2.0 (simplifies terms to factor 2 out)
    //              3.) b_over_2 must be guaranteed < 0.0 (avoids a branch)
    //  Returns:    Returns float2(first_solution, discriminant), so the caller
    //              can choose how to handle the "no intersection" case.  The
    //              Kahan or Citardauq formula is used for numerical robustness.
    const float discriminant = b_over_2*b_over_2 - a*c;
    const float solution0 = c/(-b_over_2 + sqrt(discriminant));
    return float2(solution0, discriminant);
}

float2 intersect_sphere(const float3 view_vec, const float3 eye_pos_vec)
{
    //  Cast a ray of direction view_vec from eye_pos_vec at a sphere of radius
    //  geom_radius, and return the distance to the first intersection in units
    //  of length(view_vec).
    //  Quadratic formula coefficients (b_over_2 is guaranteed negative):
    const float a = dot(view_vec, view_vec);
    const float b_over_2 = dot(view_vec, eye_pos_vec);  //  * 2.0 factored out
    const float c = dot(eye_pos_vec, eye_pos_vec) - geom_radius*geom_radius;
    return quadratic_solve(a, b_over_2, c);
}

float2 intersect_cylinder(const float3 view_vec, const float3 eye_pos_vec)
{
    //  Cast a ray of direction view_vec from eye_pos_vec at a cylinder of radius
    //  geom_radius, and return the distance to the first intersection in units
    //  of length(view_vec).  Uses LaGrange's identity to reduce operations.
    //  Arbitrary "cylinder top" reference point for an infinite cylinder:
    const float3 cylinder_top_vec = float3(0.0, geom_radius, 0.0);
    const float3 cylinder_axis_vec = float3(0.0, 1.0, 0.0);
    const float3 top_to_eye_vec = eye_pos_vec - cylinder_top_vec;
    const float3 axis_x_view = cross(cylinder_axis_vec, view_vec);
    const float3 axis_x_top_to_eye = cross(cylinder_axis_vec, top_to_eye_vec);
    //  Quadratic formula coefficients (b_over_2 is guaranteed negative):
    const float a = dot(axis_x_view, axis_x_view);
    const float b_over_2 = dot(axis_x_top_to_eye, axis_x_view);
    const float c = dot(axis_x_top_to_eye, axis_x_top_to_eye) -
        geom_radius*geom_radius;
    return quadratic_solve(a, b_over_2, c);
}

float2 cylinder_xyz_to_uv(const float3 intersection_pos_local,
    const float2 geom_aspect)
{
    //  Requires:   An xyz intersection position on a cylinder.
    //  Returns:    video_uv coords mapped to range [-0.5, 0.5]
    //  Start with a numerically robust arc length calculation.
    const float angle_from_image_center = atan2(intersection_pos_local.x,
        intersection_pos_local.z);
    const float signed_arc_len = angle_from_image_center * geom_radius;
    //  Get a uv-mapping where [-0.5, 0.5] maps to a "square" area, then divide
    //  by the aspect ratio to stretch the mapping appropriately:
    const float2 square_uv = float2(signed_arc_len, -intersection_pos_local.y);
    const float2 video_uv = square_uv / geom_aspect;
    return video_uv;
}

float3 cylinder_uv_to_xyz(const float2 video_uv, const float2 geom_aspect)
{
    //  Requires:   video_uv coords mapped to range [-0.5, 0.5]
    //  Returns:    An xyz intersection position on a cylinder (inverse of
    //              cylinder_xyz_to_uv()).
    const float2 square_uv = video_uv * geom_aspect;
    const float arc_len = square_uv.x;
    const float angle_from_image_center = arc_len / geom_radius;
    const float x_pos = sin(angle_from_image_center) * geom_radius;
    const float z_pos = cos(angle_from_image_center) * geom_radius;
    const float3 intersection_pos_local = float3(x_pos, -square_uv.y, z_pos);
    return intersection_pos_local;
}

float2 sphere_xyz_to_uv(const float3 intersection_pos_local,
    const float2 geom_aspect)
{
    //  Requires:   An xyz intersection position on a sphere.
    //  Returns:    video_uv coords mapped to range [-0.5, 0.5]
    //  Credit for this mapping goes to cgwg: it is based on great circle
    //  distances along the tangent great circle from the image center.
    const float3 image_center_pos_local = float3(0.0, 0.0, geom_radius);
    const float cp_len =
        length(cross(intersection_pos_local, image_center_pos_local));
    const float dp = dot(intersection_pos_local, image_center_pos_local);
    const float angle_from_image_center = atan2(cp_len, dp);
    const float arc_len = angle_from_image_center * geom_radius;
    //  Get a uv-mapping where [-0.5, 0.5] maps to a "square" area, then divide
    //  by the aspect ratio to stretch the mapping appropriately:
    const float2 square_uv_unit = normalize(float2(intersection_pos_local.x,
        -intersection_pos_local.y));
    const float2 square_uv = arc_len * square_uv_unit;
    const float2 video_uv = square_uv / geom_aspect;
    return video_uv;
}

float3 sphere_uv_to_xyz(const float2 video_uv, const float2 geom_aspect)
{
    //  Requires:   video_uv coords mapped to range [-0.5, 0.5]
    //  Returns:    An xyz intersection position on a sphere (inverse of
    //              sphere_xyz_to_uv()).
    const float2 square_uv = video_uv * geom_aspect;
    const float2 square_uv_unit = normalize(square_uv);
    const float arc_len = square_uv.y/square_uv_unit.y;
    const float angle_from_image_center = arc_len / geom_radius;
    const float xy_dist_from_sphere_center =
        sin(angle_from_image_center) * geom_radius;
    const float2 xy_pos = xy_dist_from_sphere_center * square_uv_unit;
    const float z_pos = cos(angle_from_image_center) * geom_radius;
    const float3 intersection_pos_local = float3(xy_pos.x, -xy_pos.y, z_pos);
    return intersection_pos_local;
}

float2 sphere_alt_xyz_to_uv(const float3 intersection_pos_local,
    const float2 geom_aspect)
{
    //  Requires:   An xyz intersection position on a sphere.
    //  Returns:    video_uv coords mapped to range [-0.5, 0.5]
    //  See cylinder_xyz_to_uv() for implementation details (very similar).
    const float2 angle_from_image_center = atan2(
        float2(intersection_pos_local.x, -intersection_pos_local.y),
        intersection_pos_local.zz);
    const float2 signed_arc_len = angle_from_image_center * geom_radius;
    const float2 video_uv = signed_arc_len / geom_aspect;
    return video_uv;
}

float3 sphere_alt_uv_to_xyz(const float2 video_uv, const float2 geom_aspect)
{
    //  Requires:   video_uv coords mapped to range [-0.5, 0.5]
    //  Returns:    An xyz intersection position on a sphere (inverse of
    //              sphere_alt_xyz_to_uv()).
    const float2 square_uv = video_uv * geom_aspect;
    const float2 arc_len = square_uv;
    const float2 angle_from_image_center = arc_len / geom_radius;
    const float2 xy_pos = sin(angle_from_image_center) * geom_radius;
    const float z_pos = sqrt(geom_radius*geom_radius - dot(xy_pos, xy_pos));
    return float3(xy_pos.x, -xy_pos.y, z_pos);
}

inline float2 intersect(const float3 view_vec_local, const float3 eye_pos_local,
    const float geom_mode)
{
    return geom_mode < 2.5 ? intersect_sphere(view_vec_local, eye_pos_local) :
        intersect_cylinder(view_vec_local, eye_pos_local);
}

inline float2 xyz_to_uv(const float3 intersection_pos_local,
    const float2 geom_aspect, const float geom_mode)
{
    return geom_mode < 1.5 ?
            sphere_xyz_to_uv(intersection_pos_local, geom_aspect) :
        geom_mode < 2.5 ?
            sphere_alt_xyz_to_uv(intersection_pos_local, geom_aspect) :
            cylinder_xyz_to_uv(intersection_pos_local, geom_aspect);
}

inline float3 uv_to_xyz(const float2 uv, const float2 geom_aspect,
    const float geom_mode)
{
    return geom_mode < 1.5 ? sphere_uv_to_xyz(uv, geom_aspect) :
        geom_mode < 2.5 ? sphere_alt_uv_to_xyz(uv, geom_aspect) :
        cylinder_uv_to_xyz(uv, geom_aspect);
}

float2 view_vec_to_uv(const float3 view_vec_local, const float3 eye_pos_local,
    const float2 geom_aspect, const float geom_mode, out float3 intersection_pos)
{
    //  Get the intersection point on the primitive, given an eye position
    //  and view vector already in its local coordinate frame:
    const float2 intersect_dist_and_discriminant = intersect(view_vec_local,
        eye_pos_local, geom_mode);
    const float3 intersection_pos_local = eye_pos_local +
        view_vec_local * intersect_dist_and_discriminant.x;
    //  Save the intersection position to an output parameter:
    intersection_pos = intersection_pos_local;
    //  Transform into uv coords, but give out-of-range coords if the
    //  view ray doesn't intersect the primitive in the first place:
    return intersect_dist_and_discriminant.y > 0.005 ?
        xyz_to_uv(intersection_pos_local, geom_aspect, geom_mode) : float2(1.0);
}

float3 get_ideal_global_eye_pos_for_points(float3 eye_pos,
    const float2 geom_aspect, const float3 global_coords[MAX_POINT_CLOUD_SIZE],
    const int num_points)
{
    //  Return an eye position at which the point cloud spans as much of the
    //  screen as possible (given the FOV controlled by geom_view_dist) without
    //  being cropped or sheared.  Move laterally to maximize forward travel,
    //  then move forward as far as possible without clipping the CRT.
    static const int max_centering_iters = 1;  //  Keep for easy testing.
    for(int iter = 0; iter < max_centering_iters; iter++)
    {
        //  0.) Get the eyespace coordinates of our point cloud:
        float3 eyespace_coords[MAX_POINT_CLOUD_SIZE];
        for(int i = 0; i < num_points; i++)
        {
            eyespace_coords[i] = global_coords[i] - eye_pos;
        }
        //  1a.)For each point, find out how far we can move eye_pos in each
        //      lateral direction without the point clipping the frustum.
        float abs_radius = abs(geom_radius);  //  In case anyone gets ideas. ;)
        float2 offset_dr_min = float2(10.0 * abs_radius, 10.0 * abs_radius);
        float2 offset_ul_max = float2(-10.0 * abs_radius, -10.0 * abs_radius);
        for(int i = 0; i < num_points; i++)
        {
            static const float2 flipy = float2(1.0, -1.0);
            float3 eyespace_xyz = eyespace_coords[i];
            float2 offset_dr = eyespace_xyz.xy - float2(-0.5) *
                (geom_aspect * -eyespace_xyz.z) / (geom_view_dist * flipy);
            float2 offset_ul = eyespace_xyz.xy - float2(0.5) *
                (geom_aspect * -eyespace_xyz.z) / (geom_view_dist * flipy);
            offset_dr_min = min(offset_dr_min, offset_dr);
            offset_ul_max = max(offset_ul_max, offset_ul);
        }
        //  1b.)Update eye_pos: equal leeway top/bottom and left/right.
        float2 center_offset = 0.5 * (offset_ul_max + offset_dr_min);
        eye_pos.xy += center_offset;
        for(int i = 0; i < num_points; i++)
        {
            eyespace_coords[i] = global_coords[i] - eye_pos;
        }
        //  2a.)For each point, find out how far we can move eye_pos forward
        //      without the point clipping the frustum.
        float offset_z_max = -10.0 * geom_radius * geom_view_dist;
        for(int i = 0; i < num_points; i++)
        {
            float3 eyespace_xyz_flipy = eyespace_coords[i] *
                float3(1.0, -1.0, 1.0);
            float4 offset_zzzz = eyespace_xyz_flipy.zzzz +
                (eyespace_xyz_flipy.xyxy * geom_view_dist) /
                (float4(-0.5, -0.5, 0.5, 0.5) * float4(geom_aspect, geom_aspect));
            //  Ignore offsets that push positive x/y values to opposite
            //  boundaries, and vice versa, and don't let the camera move
            //  past a point in the dead center of the screen:
            offset_z_max = (eyespace_xyz_flipy.x < 0.0) ?
                max(offset_z_max, offset_zzzz.x) : offset_z_max;
            offset_z_max = (eyespace_xyz_flipy.y < 0.0) ?
                max(offset_z_max, offset_zzzz.y) : offset_z_max;
            offset_z_max = (eyespace_xyz_flipy.x > 0.0) ?
                max(offset_z_max, offset_zzzz.z) : offset_z_max;
            offset_z_max = (eyespace_xyz_flipy.y > 0.0) ?
                max(offset_z_max, offset_zzzz.w) : offset_z_max;
            offset_z_max = max(offset_z_max, eyespace_xyz_flipy.z);
        }
        //  2b.)Update eye_pos: Add the maximum (smallest negative) z offset.
        eye_pos.z += offset_z_max;
    }
    return eye_pos;
}

float3 get_ideal_global_eye_pos(const float3x3 local_to_global,
    const float2 geom_aspect, const float geom_mode)
{
    //  Start with an initial eye_pos that includes the entire primitive
    //  (sphere or cylinder) in its field-of-view:
    const float3 high_view = float3(0.0, geom_aspect.y, -geom_view_dist);
    const float3 low_view = high_view * float3(1.0, -1.0, 1.0);
    const float len_sq = dot(high_view, high_view);
    const float fov = abs(acos(dot(high_view, low_view)/len_sq));
    //  Trigonometry/similar triangles say distance = geom_radius/sin(fov/2):
    const float eye_z_spherical = geom_radius/sin(fov*0.5);
    const float3 eye_pos = geom_mode < 2.5 ?
        float3(0.0, 0.0, eye_z_spherical) :
        float3(0.0, 0.0, max(geom_view_dist, eye_z_spherical));

    //  Get global xyz coords of extreme sample points on the simulated CRT
    //  screen: the center, edge centers, and corners of the video image.
    static const int num_points = MAX_POINT_CLOUD_SIZE;
    float3 global_coords[MAX_POINT_CLOUD_SIZE];
    global_coords[0] = mul(local_to_global, uv_to_xyz(float2(0.0, 0.0), geom_aspect, geom_mode));
    global_coords[1] = mul(local_to_global, uv_to_xyz(float2(0.0, -0.5), geom_aspect, geom_mode));
    global_coords[2] = mul(local_to_global, uv_to_xyz(float2(0.0, 0.5), geom_aspect, geom_mode));
    global_coords[3] = mul(local_to_global, uv_to_xyz(float2(-0.5, 0.0), geom_aspect, geom_mode));
    global_coords[4] = mul(local_to_global, uv_to_xyz(float2(0.5, 0.0), geom_aspect, geom_mode));
    global_coords[5] = mul(local_to_global, uv_to_xyz(float2(-0.5, -0.5), geom_aspect, geom_mode));
    global_coords[6] = mul(local_to_global, uv_to_xyz(float2(0.5, -0.5), geom_aspect, geom_mode));
    global_coords[7] = mul(local_to_global, uv_to_xyz(float2(-0.5, 0.5), geom_aspect, geom_mode));
    global_coords[8] = mul(local_to_global, uv_to_xyz(float2(0.5, 0.5), geom_aspect, geom_mode));
    //  For safety, default to the initial eye_pos if any z coords are negative:
    float num_negative_z_coords = 0.0;
    for(int i = 0; i < num_points; i++)
    {
        num_negative_z_coords += float(global_coords[0].z < 0.0);
    }
    //  Outsource the optimized eye_pos calculation:
    return num_negative_z_coords > 0.5 ? eye_pos :
        get_ideal_global_eye_pos_for_points(eye_pos, geom_aspect,
            global_coords, num_points);
}

float3x3 get_pixel_to_object_matrix(const float3x3 global_to_local,
    const float3 eye_pos_local, const float3 view_vec_global,
    const float3 intersection_pos_local, const float3 normal,
    const float2 output_size_inv)
{
    //  Return a transformation matrix from 2D pixel-space vectors (where
    //  (+1.0, +1.0) is a vector to one pixel down-right) to 3D object-space
    //  vectors in the CRT's local coordinate frame, tangent to the CRT surface
    //  at the intersection position.
    const float3 pos = intersection_pos_local;
    const float3 eye_pos = eye_pos_local;
    //  1.) Get the local view vecs for the pixels to the right and down:
    const float3 view_vec_right_global = view_vec_global +
        float3(output_size_inv.x, 0.0, 0.0);
    const float3 view_vec_down_global = view_vec_global +
        float3(0.0, -output_size_inv.y, 0.0);
    const float3 view_vec_right_local =
        mul(global_to_local, view_vec_right_global);
    const float3 view_vec_down_local =
        mul(global_to_local, view_vec_down_global);
    //  2.) Using the true intersection point, intersect the neighboring
    //      view vectors with the tangent plane:
    const float3 intersection_vec_dot_normal = float3(dot(pos - eye_pos, normal), dot(pos - eye_pos, normal), dot(pos - eye_pos, normal));
    const float3 right_pos = eye_pos + (intersection_vec_dot_normal /
        dot(view_vec_right_local, normal))*view_vec_right_local;
    const float3 down_pos = eye_pos + (intersection_vec_dot_normal /
        dot(view_vec_down_local, normal))*view_vec_down_local;
    //  3.) Subtract the original intersection pos from its neighbors; the
    //      resulting object-space vectors are the transforms of (1, 0) and
    //      (0, 1) pixel offsets.  The third basis vector is (0, 0, 0).
    const float3 object_right_vec = right_pos - pos;
    const float3 object_down_vec = down_pos - pos;
    const float3x3 pixel_to_object = float3x3(
        object_right_vec.x, object_down_vec.x, 0.0,
        object_right_vec.y, object_down_vec.y, 0.0,
        object_right_vec.z, object_down_vec.z, 0.0);
    return pixel_to_object;
}

float3x3 get_object_to_tangent_matrix(const float3 intersection_pos_local,
    const float3 normal, const float2 geom_aspect, const float geom_mode)
{
    //  Return a transformation matrix from 3D object-space vectors in the CRT's
    //  local coordinate frame (right-handed, +y = up) to 2D video_uv vectors
    //  (+v = down).  This is the transpose of the cotangent frame (the inverse
    //  of the TBN matrix), built per Christian Schueler's "Normal Mapping
    //  Without Precomputed Tangents."
    const float3 pos = intersection_pos_local;
    static const float3 x_vec = float3(1.0, 0.0, 0.0);
    static const float3 y_vec = float3(0.0, 1.0, 0.0);
    float3 cotangent_unscaled, cobitangent_unscaled;
    //  geom_mode should be constant-folded without RUNTIME_GEOMETRY_MODE.
    if(geom_mode < 1.5)
    {
        //  Sphere:
        cotangent_unscaled = normalize(cross(y_vec, pos)) * geom_aspect.y;
        cobitangent_unscaled = normalize(cross(x_vec, pos)) * geom_aspect.x;
    }
    else if(geom_mode < 2.5)
    {
        //  Sphere, alternate mapping:
        const float3 tangent = normalize(
            cross(y_vec, float3(pos.x, 0.0, pos.z))) * geom_aspect.x;
        const float3 bitangent = normalize(
            cross(x_vec, float3(0.0, pos.yz))) * geom_aspect.y;
        cotangent_unscaled = cross(normal, bitangent);
        cobitangent_unscaled = cross(tangent, normal);
    }
    else
    {
        //  Cylinder:
        cotangent_unscaled = cross(y_vec, normal) * geom_aspect.y;
        cobitangent_unscaled = float3(0.0, -geom_aspect.x, 0.0);
    }
    const float3 computed_normal =
        cross(cobitangent_unscaled, cotangent_unscaled);
    const float inv_determinant = rsqrt(dot(computed_normal, computed_normal));
    const float3 cotangent = cotangent_unscaled * inv_determinant;
    const float3 cobitangent = cobitangent_unscaled * inv_determinant;
    //  The [cotangent, cobitangent, normal] column vecs form the cotangent
    //  frame, i.e. the inverse-transpose TBN matrix.  Get its transpose:
    const float3x3 object_to_tangent = float3x3(cotangent, cobitangent, normal);
    return object_to_tangent;
}

float2 get_curved_video_uv_coords_and_tangent_matrix(
    const float2 flat_video_uv, const float3 eye_pos_local,
    const float2 output_size_inv, const float2 geom_aspect,
    const float geom_mode, const float3x3 global_to_local,
    out float2x2 pixel_to_tangent_video_uv)
{
    //  Transform flat_video_uv into a view vector, project from the eye,
    //  intersect with the CRT primitive, and convert the intersection position
    //  into final uv coords and a pixel-space -> tangent-space matrix.
    //  1.) Center uv around 0; map the screen corners to (-0.5,-0.5)/(0.5,0.5).
    //  2.) Multiply by geom_aspect to undo the screen-space aspect correction.
    //  3.) (x, y) = (u, -v): +v is down in 2D, +y is up in 3D (right-handed).
    //  4.) The view vector z sets the "near plane" distance and the FOV.
    const float2 view_uv = (flat_video_uv - float2(0.5)) * geom_aspect;
    const float3 view_vec_global =
        float3(view_uv.x, -view_uv.y, -geom_view_dist);
    //  Transform the view vector into the CRT's local coordinate frame, convert
    //  to video_uv coords, and get the local 3D intersection position:
    const float3 view_vec_local = mul(global_to_local, view_vec_global);
    float3 pos;
    const float2 centered_uv = view_vec_to_uv(
        view_vec_local, eye_pos_local, geom_aspect, geom_mode, pos);
    const float2 video_uv = centered_uv + float2(0.5);
    //  Get a pixel-to-tangent-video-uv matrix.
    #ifdef DRIVERS_ALLOW_DERIVATIVES
        //  Derivatives obtain a matrix very fast, but the direction of pixel-
        //  space +y seems to depend on the pass.  Enforce the correct direction
        //  on a best-effort basis (it shouldn't matter for antialiasing).
        const float2 duv_dx = ddx(video_uv);
        const float2 duv_dy = ddy(video_uv);
        #ifdef LAST_PASS
            pixel_to_tangent_video_uv = float2x2(
                duv_dx.x, duv_dy.x,
                -duv_dx.y, -duv_dy.y);
        #else
            pixel_to_tangent_video_uv = float2x2(
                duv_dx.x, duv_dy.x,
                duv_dx.y, duv_dy.y);
        #endif
    #else
        //  Manually define a transformation matrix.  We'll assume pixel-space
        //  +y = down, just like +v = down.
        if(geom_force_correct_tangent_matrix)
        {
            //  Get the surface normal based on the local intersection position:
            const float3 normal_base = geom_mode < 2.5 ? pos :
                float3(pos.x, 0.0, pos.z);
            const float3 normal = normalize(normal_base);
            //  Get pixel-to-object and object-to-tangent matrices and combine
            //  them into a 2x2 pixel-to-tangent matrix for video_uv offsets:
            const float3x3 pixel_to_object = get_pixel_to_object_matrix(
                global_to_local, eye_pos_local, view_vec_global, pos, normal,
                output_size_inv);
            const float3x3 object_to_tangent = get_object_to_tangent_matrix(
                pos, normal, geom_aspect, geom_mode);
            const float3x3 pixel_to_tangent3x3 =
                mul(object_to_tangent, pixel_to_object);
            pixel_to_tangent_video_uv = float2x2(
                pixel_to_tangent3x3[0][0], pixel_to_tangent3x3[0][1], pixel_to_tangent3x3[1][0], pixel_to_tangent3x3[1][1]);
        }
        else
        {
            //  Ignore curvature, and just consider flat scaling.  The
            //  difference is only apparent with strong curvature:
            pixel_to_tangent_video_uv = float2x2(
                output_size_inv.x, 0.0, 0.0, output_size_inv.y);
        }
    #endif
    return video_uv;
}

float get_border_dim_factor(const float2 video_uv, const float2 geom_aspect)
{
    //  COPYRIGHT NOTE FOR THIS FUNCTION:
    //  Copyright (C) 2010-2012 cgwg, 2014 TroggleMonkey
    //  This function uses an algorithm first coded in several of cgwg's GPL-
    //  licensed lines in crt-geom-curved.cg and its ancestors.

    //  Calculate border_dim_factor from the proximity to uv-space image borders:
    const float2 edge_dists = min(video_uv, float2(1.0) - video_uv) *
        geom_aspect;
    const float2 border_penetration =
        max(float2(border_size) - edge_dists, float2(0.0));
    const float penetration_ratio = length(border_penetration)/border_size;
    if (border_size == 0.0)
        penetration_ratio = 0.0;
    const float border_escape_ratio = max(1.0 - penetration_ratio, 0.0);
    const float border_dim_factor =
        pow(border_escape_ratio, border_darkness) * max(1.0, border_compress);
    return min(border_dim_factor, 1.0);
}

#endif  //  CRT_ROYALE_GEOMETRY_FUNCTIONS_INC
