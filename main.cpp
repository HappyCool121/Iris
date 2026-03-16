#include "application.h"
#include "dataTypes.h"
#include <chrono>
#include <cstring>
#include <glm/glm.hpp>
#include <iostream>
#include <random>

// Metal headers
#define NS_PRIVATE_IMPLEMENTATION
#define CA_PRIVATE_IMPLEMENTATION
#define MTL_PRIVATE_IMPLEMENTATION
#include <Metal/Metal.hpp>

#define STB_IMAGE_IMPLEMENTATION
#include <filesystem>

#include "stb_image.h"


// ============================================================================
// Uniforms struct — must match the Metal shader's Uniforms exactly
// ============================================================================
struct Uniforms {
  uint32_t width;
  uint32_t height;
  float camera_pos[3]; // packed_float3 compatible
  float disc_rot_x;
  float disc_rot_z;
  float disc_normal[3]; // Pre-computed normal (packed_float3 compatible)
  float sun_pos[3];
  float sun_radius;
  float cam_right[3];
  float cam_up[3];
  float cam_forward[3];
};

// global variables
// camera settings
glm::vec3 camera_pos = {0.0f, 0.0f, -14.0f};

// checkerboard texture wall
glm::vec3 skybox_pos = {0.0f, 0.0f, 50.0f};
glm::vec3 skybox_scale = {1.0f, 1.0f, 1.0f};

// black hole settings
glm::vec3 bh_pos = {0.0f, 0.0f, 0.0f};

// ============================================================================
// Metal objects (initialized in main, used in RenderImageGPU)
// ============================================================================
static MTL::Device *g_device = nullptr;
static MTL::CommandQueue *g_commandQueue = nullptr;
static MTL::ComputePipelineState *g_pipelineState = nullptr;
static MTL::Buffer *g_pixelBuffer = nullptr;
static MTL::Buffer *g_uniformsBuffer = nullptr;
static MTL::Texture *g_noiseTexture = nullptr;
static MTL::Texture *g_skyboxTexture = nullptr;

// ============================================================================
// GPU Render — dispatches the render_black_hole compute kernel
// ============================================================================
void RenderImageGPU() {
  auto start = std::chrono::high_resolution_clock::now();

  // 1. Populate uniforms with current values
  Uniforms *uniforms =
      static_cast<Uniforms *>(g_uniformsBuffer->contents());
  uniforms->width = WIDTH;
  uniforms->height = HEIGHT;
  uniforms->camera_pos[0] = camera_pos.x;
  uniforms->camera_pos[1] = camera_pos.y;
  uniforms->camera_pos[2] = camera_pos.z;
  uniforms->disc_rot_x = disc_rot_x;
  uniforms->disc_rot_z = disc_rot_z;

  // Pre-compute disc normal
  float rad_x = disc_rot_x * 3.14159265359f / 180.0f;
  float rad_z = disc_rot_z * 3.14159265359f / 180.0f;

  // Analytically rotated normal (matches logic in Metal shader)
  glm::vec3 normal = glm::normalize(glm::vec3(
      -sin(rad_z) * cos(rad_x),
       cos(rad_z) * cos(rad_x),
       sin(rad_x)
  ));

  uniforms->disc_normal[0] = normal.x;
  uniforms->disc_normal[1] = normal.y;
  uniforms->disc_normal[2] = normal.z;

  uniforms->sun_pos[0] = sun_pos_x;
  uniforms->sun_pos[1] = sun_pos_y;
  uniforms->sun_pos[2] = sun_pos_z;
  uniforms->sun_radius = sun_radius;

  uniforms->cam_right[0] = cam_right.x;
  uniforms->cam_right[1] = cam_right.y;
  uniforms->cam_right[2] = cam_right.z;

  uniforms->cam_up[0] = cam_up.x;
  uniforms->cam_up[1] = cam_up.y;
  uniforms->cam_up[2] = cam_up.z;

  uniforms->cam_forward[0] = cam_forward.x;
  uniforms->cam_forward[1] = cam_forward.y;
  uniforms->cam_forward[2] = cam_forward.z;

  // 2. Create command buffer and compute encoder
  MTL::CommandBuffer *commandBuffer = g_commandQueue->commandBuffer();
  MTL::ComputeCommandEncoder *encoder =
      commandBuffer->computeCommandEncoder();

  encoder->setComputePipelineState(g_pipelineState);
  encoder->setBuffer(g_pixelBuffer, 0, 0);      // buffer(0) = pixels
  encoder->setBuffer(g_uniformsBuffer, 0, 1);   // buffer(1) = uniforms
  encoder->setTexture(g_noiseTexture, 0);       // texture(0) = noise
  encoder->setTexture(g_skyboxTexture, 1);    // texture(1) = skybox

  // 3. Calculate threadgroup and grid sizes
  MTL::Size gridSize = MTL::Size(WIDTH, HEIGHT, 1);

  // Get the maximum threads per threadgroup for this pipeline
  NS::UInteger maxThreads =
      g_pipelineState->maxTotalThreadsPerThreadgroup();
  // Use a square-ish threadgroup (e.g. 16x16 = 256, or adapt to maxThreads)
  NS::UInteger threadWidth = 16;
  NS::UInteger threadHeight = 16;
  if (threadWidth * threadHeight > maxThreads) {
    threadWidth = 8;
    threadHeight = 8;
  }
  MTL::Size threadgroupSize = MTL::Size(threadWidth, threadHeight, 1);

  // Dispatch (dispatchThreads handles non-uniform grids automatically)
  encoder->dispatchThreads(gridSize, threadgroupSize);

  encoder->endEncoding();

  // 4. Commit and wait for GPU to finish
  commandBuffer->commit();
  commandBuffer->waitUntilCompleted();

  // 5. Copy GPU results into the SDL pixel array
  memcpy(pixels.data(), g_pixelBuffer->contents(),
         WIDTH * HEIGHT * sizeof(uint32_t));

  // 6. Update the SDL texture with the new pixels
  SDL_UpdateTexture(app.texture, nullptr, pixels.data(),
                    WIDTH * sizeof(uint32_t));

  auto end = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double, std::milli> duration = end - start;
  std::cout << "GPU Raytracing took: " << duration.count() << "ms"
            << std::endl;
}


// NOISE GENERATION (for accretion disc)
// A better hash for smoother gradients
float grad(int hash, float x, float y) {
  int h = hash & 15;
  float u = h < 8 ? x : y;
  float v = h < 4 ? y : h == 12 || h == 14 ? x : 0;
  return ((h & 1) == 0 ? u : -u) + ((h & 2) == 0 ? v : -v);
}

float perlin(float x, float y) {
  int ix = (int)floor(x);
  int iy = (int)floor(y);
  float fx = x - ix;
  float fy = y - iy;
  float ux = fx * fx * fx * (fx * (fx * 6 - 15) + 10); // Quintic smoothing
  float uy = fy * fy * fy * (fy * (fy * 6 - 15) + 10);

  // Simple deterministic hash
  auto h = [](int x, int y) {
    unsigned int a = x * 374761393 + y * 668265263;
    a = (a ^ (a >> 13)) * 1274126177;
    return (int)(a ^ (a >> 16));
  };

  float n00 = grad(h(ix, iy), fx, fy);
  float n10 = grad(h(ix + 1, iy), fx - 1, fy);
  float n01 = grad(h(ix, iy + 1), fx, fy - 1);
  float n11 = grad(h(ix + 1, iy + 1), fx - 1, fy - 1);

  return (1.0f - ux) * (1.0f - uy) * n00 + ux * (1.0f - uy) * n10 +
         (1.0f - ux) * uy * n01 + ux * uy * n11;
}

// 2. Updated Texture Creation
MTL::Texture *CreateNoiseTexture(MTL::Device *device) {
  const int texSize = 512;
  MTL::TextureDescriptor *desc = MTL::TextureDescriptor::texture2DDescriptor(
      MTL::PixelFormatR8Unorm, texSize, texSize, false);
  desc->setUsage(MTL::TextureUsageShaderRead);
  MTL::Texture *texture = device->newTexture(desc);

  std::vector<uint8_t> data(texSize * texSize);

  // Inside CreateNoiseTexture loop:
  for (int y = 0; y < texSize; ++y) {
    for (int x = 0; x < texSize; ++x) {
      float u = (float)x / texSize;
      float v = (float)y / texSize;

      // X = Angle (U), Y = Radius (V)
      // Domain Warping: We want the rings to wobble slightly inward/outward
      // Low U frequency stretches the noise around the disk.
      // High V frequency creates multiple thin bands (rings).
      float warp = perlin(u * 4.0f, v * 15.0f) * 0.1f;

      // Primary Rings: Stretched heavily along the angle (U)
      float val = perlin(u * 2.0f, (v + warp) * 40.0f);

      // Secondary detail layer (broken up slightly more)
      val += perlin(u * 6.0f, v * 80.0f) * 0.3f;

      // Convert -1..1 to 0..1
      val = (val / 1.3f) * 0.5f + 0.5f;

      // Soften the contrast. The reference is a dense, opaque disk, not wispy
      // fibers. A power of 1.5 to 2.0 keeps it smooth but defines the rings.
      val = std::pow(std::max(0.0f, val), 1.8f);

      data[y * texSize + x] =
          static_cast<uint8_t>(std::clamp(val * 255.0f, 0.0f, 255.0f));
    }
  }

  MTL::Region region = MTL::Region(0, 0, texSize, texSize);
  texture->replaceRegion(region, 0, data.data(), texSize);

  desc->release();
  return texture;
}

MTL::Texture *CreateSkyboxTexture(MTL::Device *device, const char *path) {
  int width, height, channels;
  unsigned char *data = stbi_load(path, &width, &height, &channels, 4);
  if (!data) {
    std::cerr << "Failed to load image: " << path << std::endl;
    return nullptr;
  }

  MTL::TextureDescriptor *desc = MTL::TextureDescriptor::texture2DDescriptor(
      MTL::PixelFormatRGBA8Unorm, width, height, false);
  desc->setUsage(MTL::TextureUsageShaderRead);
  MTL::Texture *texture = device->newTexture(desc);

  MTL::Region region = MTL::Region(0, 0, width, height);
  texture->replaceRegion(region, 0, data, width * 4);

  stbi_image_free(data);
  desc->release();
  return texture;

}


int main(int argc, char *argv[]) {

  // --------------------------------------------------------------------------
  // METAL INIT: device, CommandQueue, Library (which contains all the kernels)
  // --------------------------------------------------------------------------

  // 1. Setup Device and Queue
  // device is our GPU
  g_device = MTL::CreateSystemDefaultDevice();
  // command queue contains all the stuff we want our GPU to do
  g_commandQueue = g_device->newCommandQueue();

  // 2. Load the kernels (Default library looks for .metal files in the app
  // bundle) CMake bundles them into one default metallib (if there are multiple
  // metal files)
  NS::Error *error = nullptr;
  MTL::Library *defaultLibrary = g_device->newDefaultLibrary();

  if (!defaultLibrary) {
    std::cout << "newDefaultLibrary failed, attempting to load from disk..." << std::endl;
    NS::String* path = NS::String::string("default.metallib", NS::ASCIIStringEncoding);
    defaultLibrary = g_device->newLibrary(path, &error);

    if (!defaultLibrary) {
      std::cerr << "Failed to find 'default.metallib'. Ensure .metal file is compiled "
                << "and in the same directory as the executable." << std::endl;
      if (error) {
        std::cerr << "Error: " << error->localizedDescription()->utf8String() << std::endl;
      }
      return -1;
    }
  }

  // 3. Get the render_black_hole kernel function
  NS::String *functionName =
      NS::String::string("render_black_hole", NS::ASCIIStringEncoding);
  MTL::Function *kernelFunction =
      defaultLibrary->newFunction(functionName);

  if (!kernelFunction) {
    std::cerr << "Failed to find 'render_black_hole' function in Metal library."
              << std::endl;
    return -1;
  }

  // 4. Create the compute pipeline state
  g_pipelineState = g_device->newComputePipelineState(kernelFunction, &error);
  if (!g_pipelineState) {
    std::cerr << "Failed to create compute pipeline state: "
              << error->localizedDescription()->utf8String() << std::endl;
    return -1;
  }

  // 5. Create Metal buffers (shared memory so CPU can read GPU output)
  g_pixelBuffer = g_device->newBuffer(
      WIDTH * HEIGHT * sizeof(uint32_t), MTL::ResourceStorageModeShared);
  g_uniformsBuffer = g_device->newBuffer(
      sizeof(Uniforms), MTL::ResourceStorageModeShared);

  // 6. Create Textures
  g_noiseTexture = CreateNoiseTexture(g_device);

  // Try multiple paths for the skybox image
  const char *skyboxPaths[] = {"images/galaxy_bg.jpg",
                               "../images/galaxy_bg.jpg",
                               "../../images/galaxy_bg.jpg"};

  for (const char *path : skyboxPaths) {
    g_skyboxTexture = CreateSkyboxTexture(g_device, path);
    if (g_skyboxTexture) {
      std::cout << "Successfully loaded skybox from: " << path << std::endl;
      break;
    }
  }

  if (!g_skyboxTexture) {
    std::cerr << "Warning: Falling back to procedural skybox if possible, "
                 "but kernel expects texture(1)."
              << std::endl;
    // We should probably create a dummy texture if loading fails to avoid GPU
    // crashes
    uint32_t dummy = 0xFF000000;
    MTL::TextureDescriptor *desc = MTL::TextureDescriptor::texture2DDescriptor(
        MTL::PixelFormatRGBA8Unorm, 1, 1, false);
    g_skyboxTexture = g_device->newTexture(desc);
    g_skyboxTexture->replaceRegion(MTL::Region(0, 0, 1, 1), 0, &dummy, 4);
    desc->release();
  }


  // Release intermediate objects
  kernelFunction->release();
  defaultLibrary->release();

  std::cout << "--- blackholev1 start ---" << std::endl;
  std::cout << "Metal GPU pipeline ready." << std::endl;

  // 1. INIT SDL and IMGUI
  app = initSDL();
  initIMGUI(app);

  if (app.window == nullptr) {
    return 1;
  }

  pixels.resize(WIDTH * HEIGHT);

  // 8. DISPLAY LOOP
  std::cout << "beginning render..." << std::endl;
  bool running = true;
  SDL_Event event;

  while (running) {
    bool inputChanged = false;
    while (SDL_PollEvent(&event)) {
      ImGui_ImplSDL2_ProcessEvent(&event);
      if (event.type == SDL_QUIT)
        running = false;
    }

    // 1. Handle Keyboard Input for Camera
    const Uint8* state = SDL_GetKeyboardState(NULL);
    float moveSpeed = 0.5f;
    float rotSpeed = 0.05f;

    if (cameraMode == CameraMode::ORBIT) {
        if (state[SDL_SCANCODE_A]) { orbit_yaw -= rotSpeed; inputChanged = true; }
        if (state[SDL_SCANCODE_D]) { orbit_yaw += rotSpeed; inputChanged = true; }
        if (state[SDL_SCANCODE_W]) { orbit_radius -= moveSpeed; inputChanged = true; }
        if (state[SDL_SCANCODE_S]) { orbit_radius += moveSpeed; inputChanged = true; }

        if (orbit_radius < 0.1f) orbit_radius = 0.1f;

        if (inputChanged) {
            // Calculate camera position in spherical coordinates (orbiting origin)
            camera_pos.x = orbit_radius * sin(orbit_yaw) * cos(orbit_pitch);
            camera_pos.y = orbit_radius * sin(orbit_pitch);
            camera_pos.z = orbit_radius * cos(orbit_yaw) * cos(orbit_pitch);

            // Point at origin
            cam_forward = glm::normalize(bh_pos - camera_pos);
            cam_right = glm::normalize(glm::cross(glm::vec3(0, 1, 0), cam_forward));
            cam_up = glm::cross(cam_forward, cam_right);

            toRender = true;
        }
    } else {
        if (state[SDL_SCANCODE_W]) { camera_pos.y += moveSpeed; inputChanged = true; }
        if (state[SDL_SCANCODE_S]) { camera_pos.y -= moveSpeed; inputChanged = true; }
        if (state[SDL_SCANCODE_A]) { camera_pos.x -= moveSpeed; inputChanged = true; }
        if (state[SDL_SCANCODE_D]) { camera_pos.x += moveSpeed; inputChanged = true; }

        if (state[SDL_SCANCODE_UP])    { camera_pitch += rotSpeed; inputChanged = true; }
        if (state[SDL_SCANCODE_DOWN])  { camera_pitch -= rotSpeed; inputChanged = true; }
        if (state[SDL_SCANCODE_LEFT])  { camera_yaw -= rotSpeed; inputChanged = true; }
        if (state[SDL_SCANCODE_RIGHT]) { camera_yaw += rotSpeed; inputChanged = true; }

        if (inputChanged) {
          // Update basis vectors
          cam_forward.x = cos(camera_yaw) * cos(camera_pitch);
          cam_forward.y = sin(camera_pitch);
          cam_forward.z = sin(camera_yaw) * cos(camera_pitch);
          cam_forward = glm::normalize(cam_forward);

          cam_right = glm::normalize(glm::cross(glm::vec3(0, 1, 0), cam_forward));
          cam_up = glm::cross(cam_forward, cam_right);

          toRender = true;
        }
    }

    ImGui_ImplSDLRenderer2_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    ImGui::Begin("Status");
    ImGui::Text("blackholev1");

    ImGui::Separator();
    ImGui::Text("Camera Mode");
    if (ImGui::RadioButton("Original", cameraMode == CameraMode::ORIGINAL)) {
        cameraMode = CameraMode::ORIGINAL;
        toRender = true;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Orbit", cameraMode == CameraMode::ORBIT)) {
        cameraMode = CameraMode::ORBIT;
        // Initialize orbit parameters from current position if switching
        orbit_radius = glm::length(camera_pos - bh_pos);
        orbit_yaw = atan2(camera_pos.x, camera_pos.z);
        orbit_pitch = asin(camera_pos.y / orbit_radius);
        toRender = true;
    }

    ImGui::Separator();
    ImGui::Text("Accretion Disc Rotation");
    if (ImGui::SliderFloat("Rotation X", &disc_rot_x, -180.0f, 180.0f)) toRender = true;
    if (ImGui::SliderFloat("Rotation Z", &disc_rot_z, -180.0f, 180.0f)) toRender = true;

    ImGui::Separator();
    ImGui::Text("Sun Settings");
    if (ImGui::SliderFloat("Sun X", &sun_pos_x, -50.0f, 50.0f)) toRender = true;
    if (ImGui::SliderFloat("Sun Y", &sun_pos_y, -50.0f, 50.0f)) toRender = true;
    if (ImGui::SliderFloat("Sun Z", &sun_pos_z, -50.0f, 50.0f)) toRender = true;
    if (ImGui::SliderFloat("Sun Radius", &sun_radius, 0.1f, 10.0f)) toRender = true;
    ImGui::End();

    if (toRender) {
      RenderImageGPU();
      toRender = false;
    }

    ImGui::Render();
    SDL_SetRenderDrawColor(app.renderer, 0, 0, 0, 255);
    SDL_RenderClear(app.renderer);
    SDL_RenderCopy(app.renderer, app.texture, nullptr, nullptr);
    ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), app.renderer);
    SDL_RenderPresent(app.renderer);
    SDL_Delay(30);
  }

  // Cleanup Metal objects
  g_pixelBuffer->release();
  g_uniformsBuffer->release();
  g_pipelineState->release();
  g_noiseTexture->release();
  g_commandQueue->release();
  g_device->release();

  ImGui_ImplSDLRenderer2_Shutdown();
  ImGui_ImplSDL2_Shutdown();
  ImGui::DestroyContext();
  SDL_DestroyTexture(app.texture);
  if (g_skyboxTexture)
    g_skyboxTexture->release();
  SDL_DestroyRenderer(app.renderer);
  SDL_DestroyWindow(app.window);
  SDL_Quit();

  return 0;
}
