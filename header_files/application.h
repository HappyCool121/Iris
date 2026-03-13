//
// Created by Imari on 13/3/26.
//

// APPLICATION HEADER FILE
// Handles windows, imgui overlay, scene data (object states, textures, texture
// maps)

#pragma once

#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_sdlrenderer2.h"
#include "dataTypes.h"
#include "imgui.h"
#include <SDL.h>
#include <glm/glm.hpp>

// scene data: contains globals and state variables

// will handle all variables and objects in the scene, including the render
// settings serves as the orchestrator for the whole application

// camera settings
inline float cameraDist = 1.0f;
inline const int WIDTH = 800;
inline const int HEIGHT = 600;

// camera orientation
inline float camera_yaw = 1.5708f; // Pointing towards +Z (PI/2)
inline float camera_pitch = 0.0f;
inline glm::vec3 cam_forward = {0, 0, 1};
inline glm::vec3 cam_right = {1, 0, 0};
inline glm::vec3 cam_up = {0, 1, 0};

inline float cameraFOV = 90.0f;
inline float FOVscale =
    (HEIGHT / 2.0f) / std::tan(cameraFOV * 0.5f * 3.14159f / 180.0f);

// accretion disc settings
inline float disc_rot_x = 0.0f;
inline float disc_rot_z = 0.0f;

// sun settings
inline float sun_pos_x = 10.0f;
inline float sun_pos_y = 5.0f;
inline float sun_pos_z = 10.0f;
inline float sun_radius = 2.0f;

// continuous render flags
inline bool continuousRender;
inline bool toRender = true;

// Debug vars
inline int frameCount;
inline Uint32 lastLogTime = 0;
inline int frameStart = 0;
inline float frameTime = 0.0f;

inline std::vector<ray> rayList;
inline std::vector<uint32_t> pixels;

// =================================
// Init SDL window
// =================================

// SLD window pointers
typedef struct {
  SDL_Window *window;
  SDL_Renderer *renderer;
  SDL_Texture *texture;
} AppContext;

inline AppContext app;

// initialize SDL window
AppContext initSDL();

// =================================
// Init imgui interface
// =================================

void initIMGUI(AppContext app);

// =================================
// Skybox sampling
// =================================

glm::vec3 sample_skybox(glm::vec3 dir);

glm::vec3 black_skybox(glm::vec3 dir);

// =================================
// Debug Log
// =================================

void LogFrame(int frameID, Uint32 elapsedMS);
