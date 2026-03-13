//
// Created by Imari on 13/3/26.
//
#include "application.h"
#include "dataTypes.h"
#include <iostream>

// initialize SDL window, return pointers window, renderer, texture
AppContext initSDL() {

  AppContext app = {nullptr, nullptr, nullptr};

  if (SDL_Init(SDL_INIT_VIDEO) < 0)
    return app;

  app.window = SDL_CreateWindow("Black hole Renderer", SDL_WINDOWPOS_CENTERED,
                                SDL_WINDOWPOS_CENTERED, WIDTH, HEIGHT, 0);

  app.renderer = SDL_CreateRenderer(
      app.window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);

  app.texture = SDL_CreateTexture(app.renderer, SDL_PIXELFORMAT_ARGB8888,
                                  SDL_TEXTUREACCESS_STREAMING, WIDTH, HEIGHT);

  std::cout << "Initialized SDL window" << std::endl;

  return app;
}
void initIMGUI(AppContext app) {
  // --- Init Imgui ---
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO &io = ImGui::GetIO();
  (void)io;
  ImGui::StyleColorsDark();
  ImGui_ImplSDL2_InitForSDLRenderer(app.window, app.renderer);
  ImGui_ImplSDLRenderer2_Init(app.renderer);

  std::cout << "Initialized SDL Renderer" << std::endl;
}

// checkerboard skybox
glm::vec3 sample_skybox(glm::vec3 dir) {
  // Convert 3D direction to spherical coordinates
  // atan2 returns values from -pi to pi
  double longitude = atan2(dir.z, dir.x);
  // asin returns values from -pi/2 to pi/2
  double latitude = asin(dir.y);

  // Scale them to make grid squares
  double u = longitude * 10.0;
  double v = latitude * 10.0;

  // Create a checkerboard pattern using modulo
  // (Ensure you handle negative modulo correctly in C++)
  bool is_even = (int(floor(u)) + int(floor(v))) % 2 == 0;

  if (is_even) {
    return glm::vec3(0.8, 0.8, 0.8); // Light gray
  } else {
    return glm::vec3(0.1, 0.1, 0.1); // Dark gray
  }
}

// black skybox
glm::vec3 black_skybox(glm::vec3 dir) {
  // Returns pure black for all directions
  return glm::vec3(0.0, 0.0, 0.0);
}

void LogFrame(int frameID, Uint32 elapsedMS) {
  std::cout << "[Frame " << frameID << "] "
            << "Time: " << elapsedMS << "ms | "
            << "FPS: " << (elapsedMS > 0 ? 1000 / elapsedMS : 999) << " | ";
}