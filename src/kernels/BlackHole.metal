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

    packed_float3 sun_pos;
    float sun_radius;
};

float3 sample_skybox(float3 dir) {
    // Pure black void
    float3 color = float3(0.0f);
    return color;
}

kernel void render_black_hole(
    device uint* pixels [[buffer(0)]],
    constant Uniforms& uniforms [[buffer(1)]],
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
    float3 ray_d = normalize(float3(screen_x, screen_y, 1.0f));

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
    int max_steps = 500;
    
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
                float blend = (dist_to_center - 3.0f) / 7.0f;
                color = float3(1.0f, 0.6f * (1.0f - blend), 0.1f * (1.0f - blend));
                break;
            }
        }

        // D. Intersected the Sun
        float3 sun_pos = uniforms.sun_pos;
        float sun_radius = uniforms.sun_radius;

        float dist_prev = length(prev_pos3D - sun_pos) - sun_radius;
        float dist_curr = length(curr_pos3D - sun_pos) - sun_radius;

        if (dist_prev * dist_curr < 0.0f) {
            color = float3(0.67f, 0.85f, 0.9f); // White/Yellow sun
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