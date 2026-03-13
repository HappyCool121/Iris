#include <metal_stdlib>
using namespace metal;

// Struct to pass data from your C++ application to the GPU
// (Ensure your C++ struct matches this exactly)
struct Uniforms {

    // screen resolution
    uint width; 
    uint height;
    
    // packed_float3 avoids 16-byte alignment padding issues with C++
    packed_float3 camera_pos;

    // accretion disk rotation angles
    float disc_rot_x;
    float disc_rot_z;
    packed_float3 disc_normal;

    // sun position
    packed_float3 sun_pos;
    float sun_radius;

    packed_float3 cam_right;
    packed_float3 cam_up;
    packed_float3 cam_forward;
};

// Procedural checkerboard skybox (Replaces your C++ black_skybox function)
float3 sample_skybox(float3 dir) {
    return float3(0.05f);
}

kernel void render_black_hole(
    device uint* pixels [[buffer(0)]],
    constant Uniforms& uniforms [[buffer(1)]],
    texture2d<float, access::sample> noise_texture [[texture(0)]],
    uint2 gid [[thread_position_in_grid]]
) {
    // Prevent out-of-bounds execution
    if (gid.x >= uniforms.width || gid.y >= uniforms.height) {
        return;
    }

    // 1. Calculate Screen Space
    float aspect_ratio = (float)uniforms.width / (float)uniforms.height;
    float screen_x = (2.0f * (gid.x + 0.5f) / uniforms.width - 1.0f) * aspect_ratio;
    float screen_y = 1.0f - 2.0f * (gid.y + 0.5f) / uniforms.height;

    float3 camera_pos = uniforms.camera_pos;
    float3 cam_right = uniforms.cam_right;
    float3 cam_up = uniforms.cam_up;
    float3 cam_forward = uniforms.cam_forward;

    // focal_length of 1.0f roughly matches FOV
    float3 ray_d = normalize(screen_x * cam_right + screen_y * cam_up + 1.0f * cam_forward);

    // 2. Define basis vectors for 2D Cartesian plane
    float3 N = cross(camera_pos, ray_d);

    // SAFETY CHECK: Ray points directly at BH origin
    if (length(N) < 1e-6f) {
        pixels[gid.y * uniforms.width + gid.x] = 0xFF000000; // Black (ARGB)
        return;
    }

    N = normalize(N);
    float3 e1 = normalize(camera_pos);
    float3 e2 = normalize(cross(N, e1));

    // 3. Get initial velocities in x and y direction
    float vx = dot(ray_d, e1);
    float vy = dot(ray_d, e2);

    // 4. Define polar coordinates and initial values
    float r_init = length(camera_pos);
    float phi = 0.0f;
    float u = 1.0f / r_init;
    float v = -u * (vx / vy);

    // 5. Setup Loop Variables
    float3 prev_pos3D = camera_pos;
    float d_phi = 0.05f;
    int max_steps = 200;

    float3 disc_normal = uniforms.disc_normal;

    // initialize pixel color to black
    float3 color = float3(0.0f, 0.0f, 0.0f);

    // 6. RK4 step process
    for (int curr_steps = 0; curr_steps < max_steps; curr_steps++) {

        // RK4 Integration Step (Solving: v' = 1.5 * u^2 - u)
        float k1_u = v;
        float k1_v = 1.5f * u * u - u;

        float u_k2 = u + 0.5f * d_phi * k1_u;
        float k2_u = v + 0.5f * d_phi * k1_v;
        float k2_v = 1.5f * u_k2 * u_k2 - u_k2;

        float u_k3 = u + 0.5f * d_phi * k2_u;
        float k3_u = v + 0.5f * d_phi * k2_v;
        float k3_v = 1.5f * u_k3 * u_k3 - u_k3;

        float u_k4 = u + d_phi * k3_u;
        float k4_u = v + d_phi * k3_v;
        float k4_v = 1.5f * u_k4 * u_k4 - u_k4;

        // Update u, v, and phi
        u += (d_phi / 6.0f) * (k1_u + 2.0f * k2_u + 2.0f * k3_u + k4_u);
        v += (d_phi / 6.0f) * (k1_v + 2.0f * k2_v + 2.0f * k3_v + k4_v);
        phi += d_phi;

        // Map back to 3D to check for collisions
        float r = 1.0f / u;
        float local_x = r * cos(phi);
        float local_y = r * sin(phi);
        float3 curr_pos3D = e1 * local_x + e2 * local_y;

        // Termination Conditions
        // A. Fell into the Event Horizon
        if (r <= 1.0f) {
            color = float3(0.0f); // Black
            break;
        }

        // B. Escaped to infinity
        if (r > 100.0f) {
            float3 final_dir = normalize(curr_pos3D - prev_pos3D);
            color = sample_skybox(final_dir);
            break;
        }

        // C. Intersected the Accretion Disk
        float prev_dot = dot(prev_pos3D, disc_normal);
        float curr_dot = dot(curr_pos3D, disc_normal);

        if (prev_dot * curr_dot < 0.0f) {
            float t = abs(prev_dot) / (abs(prev_dot) + abs(curr_dot));
            float3 hit_point = prev_pos3D + (curr_pos3D - prev_pos3D) * t;
            float dist_to_center = length(hit_point);

            if (dist_to_center >= 3.0f && dist_to_center <= 10.0f) {
                // Calculate polar UVs
                float rad_x = uniforms.disc_rot_x * M_PI_F / 180.0f;
                float rad_z = uniforms.disc_rot_z * M_PI_F / 180.0f;

                // Inverse Z rotation
                float cos_z = cos(-rad_z);
                float sin_z = sin(-rad_z);
                float x1 = hit_point.x * cos_z - hit_point.y * sin_z;
                float y1 = hit_point.x * sin_z + hit_point.y * cos_z;
                float z1 = hit_point.z;

                // Inverse X rotation
                float cos_x = cos(-rad_x);
                float sin_x = sin(-rad_x);
                float x2 = x1;
                float z2 = y1 * sin_x + z1 * cos_x;

                // Now (x2, z2) are in the disc plane
                float angle = atan2(z2, x2);

                // Inside your kernel
                float u = (angle + M_PI_F) / (2.0f * M_PI_F);
                float v = (dist_to_center - 3.0f) / 7.0f;

                constexpr sampler linearSampler(mag_filter::linear, min_filter::linear, address::repeat);
                float noise_val = noise_texture.sample(linearSampler, float2(u, v)).r;

                // --- 1. Physics-based Temperature & Density Falloff ---
                // The center of an accretion disk is exponentially hotter and denser than the edge.
                // We use inverse square-ish falloff for the base intensity.
                float disk_temperature = 1.0f / (v * v * 5.0f + 0.8f);
                float disk_density = smoothstep(1.0f, 0.9f, v) * smoothstep(0.0f, 0.05f, v); // Smooth fade at inner/outer edges

                // --- 2. Doppler Beaming (The Secret Sauce) ---
                // We need the ray direction to see if the disk is spinning towards or away from the camera.
                float3 ray_dir = normalize(curr_pos3D - prev_pos3D);

                // Calculate the velocity vector of the disk at the hit point.
                // Assuming the disk spins around its normal.
                float3 hit_dir_norm = normalize(hit_point);
                float3 disk_velocity = normalize(cross(disc_normal, hit_dir_norm));

                // How much of the velocity is pointed directly at the camera ray?
                // (Positive if spinning towards camera, negative if away)
                float doppler_factor = dot(disk_velocity, ray_dir);

                // Amplify the doppler effect.
                // 0.6 is an arbitrary "speed" parameter. Increase it for a more extreme lopsided look.
                float beaming = pow(1.0f + doppler_factor * 0.5f, 1.5f);

                // --- 3. Blackbody Color Mapping ---
                // Map the temperature + noise to a specific color palette (White -> Yellow -> Orange -> Deep Red)
                float final_temp = disk_temperature * (0.5f + noise_val * 0.5f) * beaming;

                float3 color_inner = float3(1.0f, 0.95f, 0.8f); // Blinding white/yellow
                float3 color_mid   = float3(1.0f, 0.4f, 0.05f); // Deep orange
                float3 color_outer = float3(0.8f, 0.3f, 0.04f);  // Faint red/black

                float3 base_color;
                if (final_temp > 1.3f) { // if more than 1.3, white
                    base_color = color_inner; // Clamp core to bright white
                } else {
                    // Smoothly interpolate from dark red to orange to white based on heat
                    base_color = mix(color_outer, color_mid, smoothstep(0.0f, 0.4f, final_temp));
                    base_color = mix(base_color, color_inner, smoothstep(0.6f, 1.0f, final_temp));
                }

                // --- 4. Final Output ---
                // Combine color, density (fade at edges), beaming (brightness boost), and overall intensity
                float final_intensity = final_temp * 2.5f;
                color = base_color * final_intensity * disk_density;

                break;
            }
        }

        // D. Intersected the Sun
        float3 sun_pos = uniforms.sun_pos;
        float sun_radius = uniforms.sun_radius;

        float dist_prev = length(prev_pos3D - sun_pos) - sun_radius;
        float dist_curr = length(curr_pos3D - sun_pos) - sun_radius;

        if (dist_prev * dist_curr < 0.0f) {
            color = float3(0.9f, 0.95f, 0.97f); // White/Yellow sun
            break;
        }

        prev_pos3D = curr_pos3D;
    }

    // Convert float3 (0.0 - 1.0) to ARGB (0 - 255)
    uint r_byte = (uint)(clamp(color.x, 0.0f, 1.0f) * 255.0f);
    uint g_byte = (uint)(clamp(color.y, 0.0f, 1.0f) * 255.0f);
    uint b_byte = (uint)(clamp(color.z, 0.0f, 1.0f) * 255.0f);
    
    uint argb = (255u << 24) | (r_byte << 16) | (g_byte << 8) | b_byte;
    
    // Write directly into the CPU-readable array
    pixels[gid.y * uniforms.width + gid.x] = argb;
}