cmake_minimum_required(VERSION 4.1)
project(Iris)

set(CMAKE_CXX_STANDARD 20)

list(APPEND CMAKE_PREFIX_PATH "/opt/homebrew")

find_package(SDL2 REQUIRED)
find_package(glm REQUIRED)

# Find Apple Metal Frameworks
find_library(METAL_FRAMEWORK Metal REQUIRED)
find_library(FOUNDATION_FRAMEWORK Foundation REQUIRED)
find_library(QUARTZ_FRAMEWORK QuartzCore REQUIRED)

# --- Metal Shader Compilation ---
# This compiles BlackHole.metal into default.metallib
set(METAL_SRC "${CMAKE_CURRENT_SOURCE_DIR}/src/kernels/BlackHole.metal")
set(METAL_LIB "${CMAKE_CURRENT_BINARY_DIR}/default.metallib")

# Use xcrun to compile the metal shader into a library
add_custom_command(
        OUTPUT ${METAL_LIB}
        COMMAND xcrun -sdk macosx metal -c ${METAL_SRC} -o "${CMAKE_CURRENT_BINARY_DIR}/BlackHole.air"
        COMMAND xcrun -sdk macosx metallib "${CMAKE_CURRENT_BINARY_DIR}/BlackHole.air" -o ${METAL_LIB}
        DEPENDS ${METAL_SRC}
        COMMENT "Compiling Metal Shaders for Black Hole Engine"
)


set(IMGUI_SOURCES
        imgui/imgui.cpp
        imgui/imgui_draw.cpp
        imgui/imgui_tables.cpp
        imgui/imgui_widgets.cpp
        imgui/imgui_demo.cpp
        imgui/backends/imgui_impl_sdl2.cpp
        imgui/backends/imgui_impl_sdlrenderer2.cpp
        src/application.cpp
        header_files/application.h
)

add_executable(Iris
        main.cpp
        ${IMGUI_SOURCES}
        ${METAL_LIB}
)

target_include_directories(Iris PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/header_files"
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/metal-cpp" # metal-cpp path
        imgui
        imgui/backends
)

target_link_libraries(Iris
        PRIVATE
        SDL2::SDL2
        glm::glm
        ${METAL_FRAMEWORK}
        ${FOUNDATION_FRAMEWORK}
        ${QUARTZ_FRAMEWORK}
)

add_custom_command(TARGET Iris POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/images"
        "${CMAKE_CURRENT_BINARY_DIR}/images"
        COMMENT "Copying assets to build directory"
)
