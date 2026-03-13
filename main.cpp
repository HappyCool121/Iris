//
// Created by Imari on 13/3/26.
//

#include "application.h"
#include "dataTypes.h"
#include <chrono>
#include <cstring>
#include <glm/glm.hpp>
#include <iostream>

// Metal headers
#define NS_PRIVATE_IMPLEMENTATION
#define CA_PRIVATE_IMPLEMENTATION
#define MTL_PRIVATE_IMPLEMENTATION
#include <Metal/Metal.hpp>

const int TARGET_FPS = 60;
const int FRAME_DELAY = 1000 / TARGET_FPS;

// ============================================================================
// Uniforms struct — must match the Metal shader's Uniforms exactly
// ============================================================================
struct Uniforms {
  uint32_t width;
  uint32_t height;
  float camera_pos[3]; // packed_float3 compatible

  // accretion disc settings
  float disc_rot_x;
  float disc_rot_z;
  float disc_normal[3]; // Pre-computed normal (packed_float3 compatible)

  // sun settings
  float sun_pos[3];
  float sun_radius;
};


// global variables
// camera settings
glm::vec3 camera_pos = {0.0f, 0.0f, -18.0f};

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
  uniforms->sun_pos[0] = sun_pos_x;
  uniforms->sun_pos[1] = sun_pos_y;
  uniforms->sun_pos[2] = sun_pos_z;
  uniforms->sun_radius = sun_radius;

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

  // 2. Create command buffer and compute encoder
  MTL::CommandBuffer *commandBuffer = g_commandQueue->commandBuffer();
  MTL::ComputeCommandEncoder *encoder =
      commandBuffer->computeCommandEncoder();

  encoder->setComputePipelineState(g_pipelineState);
  encoder->setBuffer(g_pixelBuffer, 0, 0);      // buffer(0) = pixels
  encoder->setBuffer(g_uniformsBuffer, 0, 1);   // buffer(1) = uniforms

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

  // Release intermediate objects
  kernelFunction->release();
  defaultLibrary->release();

  std::cout << "--- blackholev3 start ---" << std::endl;
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
    while (SDL_PollEvent(&event)) {
      ImGui_ImplSDL2_ProcessEvent(&event);
      if (event.type == SDL_QUIT)
        running = false;

      // start render
      if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_r) {
        RenderImageGPU();
      }
    }

    ImGui_ImplSDLRenderer2_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    ImGui::Begin("Status");
    ImGui::Text("blackholev3");

    if (ImGui::Button("Render image (R)")) {
      RenderImageGPU();
    }

    ImGui::Separator();
    ImGui::Text("Accretion Disc Rotation");
    ImGui::SliderFloat("Rotation X", &disc_rot_x, -180.0f, 180.0f);
    ImGui::SliderFloat("Rotation Z", &disc_rot_z, -180.0f, 180.0f);

    ImGui::Separator();
    ImGui::Text("Sun Settings");
    ImGui::SliderFloat("Sun X", &sun_pos_x, -50.0f, 50.0f);
    ImGui::SliderFloat("Sun Y", &sun_pos_y, -50.0f, 50.0f);
    ImGui::SliderFloat("Sun Z", &sun_pos_z, -50.0f, 50.0f);
    ImGui::SliderFloat("Sun Radius", &sun_radius, 0.1f, 10.0f);

    ImGui::End();

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
  g_commandQueue->release();
  g_device->release();

  ImGui_ImplSDLRenderer2_Shutdown();
  ImGui_ImplSDL2_Shutdown();
  ImGui::DestroyContext();
  SDL_DestroyTexture(app.texture);
  SDL_DestroyRenderer(app.renderer);
  SDL_DestroyWindow(app.window);
  SDL_Quit();

  return 0;
}
