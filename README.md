# IRIS by imari
A real time GPU-accelerated render engine utilizing MacOS's Metal API for visualizing non-rotating Shwarzchild black holes. Uses a Range-Kutta solver (ran on GPU kernels) for the simplified null geodesic equation to compute non linear light paths.

### Average render time on M4:
* ~14ms per frame (~70 fps)
* 1000 x 800 screen resolution (~8ms per frame for 800 x 600)
* 0.05 rad step size, 1000 steps

# How to use
The code works right out the box as it contains all the relavant libraries and dependencies needed. However, this
is designed to work on Macs, more specifically anything with an Apple Sillicon chip (M1 or higher), since it uses
apple GPU accelerated raytracing. You do need to have Xcode installed to be able to use Metal. After building the project
with CMake, the first render will be a front view of the black hole. To interact, use the WASD (lateral translation)
and arrow keys (rotation) for navigation. You can also adjust the sliders to control the accretion disc and position of the sun
(not really a sun, more like a star but whatever).

# Tweaking the results

You can directly change the resolution of the render (and the window) by changing the HEIGHT and WIDTH values in application.cpp.
The quality of the render can be adjusted using the step size and number of steps used for the RK4 light path solver. By default,
the step size is 0.05 radians, with 1000 steps, which yields an accurate render of a typical black hole. Decreasing the step size
will significantly reduce the performance of the renderer, and may no longer work in real time.

# How does it work?

## The physics (and the math)

At the core of IRIS is the simulation of light paths in the curved spacetime around a **Schwarzschild Black Hole**. Unlike traditional raytracers where light travels in straight lines, IRIS computes the **null geodesics**—the paths that light follows—which are bent by the black hole's gravity.

### 1. The Schwarzschild Metric
The engine models a static, non-rotating, spherically symmetric black hole described by the Schwarzschild metric. In natural units (where $G = c = 1$ and the Schwarzschild radius $r_s = 2M = 1$), the motion of a photon is governed by the geodesic equation.

### 2. Binet's Equation
To solve for the light path efficiently, we transform the trajectory into polar coordinates $(r, \phi)$ within the plane of the ray. By substituting $u = 1/r$, the equation of motion becomes a second-order differential equation known as the **Binet Equation**:

$$\frac{d^2u}{d\phi^2} + u = \frac{3}{2}u^2$$

Defining $v = \frac{du}{d\phi}$, we can break this down into a system of two first-order equations:

- $$\frac{du}{d\phi} = v$$
- $$\frac{dv}{d\phi} = \frac{3}{2}u^2 - u$$

### 3. Numerical Integration (RK4)
IRIS uses the **Runge-Kutta 4th Order (RK4)** method to solve this system. For every pixel, the GPU integrates the ray's path step-by-step.
- **Step Size:** Typically $0.05$ radians.
- **Basis Vectors:** Since the Schwarzschild metric is spherically symmetric, the ray stays within a 2D plane defined by the camera's position and the initial ray direction. The kernel computes this 2D path and then maps it back to 3D space to check for intersections with the accretion disc or the sun.

## GPU accelerated raytracing with Metalcpp

To achieve real-time performance, IRIS offloads the heavy RK4 integration to the GPU using **Apple Metal**.

- **Highly Parallel:** Every pixel on the screen is a separate thread on the GPU, allowing thousands of rays to be integrated simultaneously.
- **Metal-cpp:** The project utilizes the `metal-cpp` header-only library, which allows the C++ codebase to interact directly with the Metal API without the need for Objective-C or Swift.
- **Zero-Copy Memory:** Data such as the pixel buffer and camera uniforms are stored in shared memory, enabling efficient transfer between the CPU (for SDL/ImGui) and the GPU (for rendering) without expensive copies.

## Visual Fidelity

### 1. Relativistic Accretion Disc
The accretion disc isn't just a static texture. It incorporates several relativistic effects:
- **Doppler Beaming:** Due to the high orbital velocities of the disc, light from the side moving towards the observer appears brighter and shifted in color, while the side moving away appears dimmer.
- **Blackbody Coloring:** The disc's color is determined by its temperature (modeled with a radial falloff), shifting from blinding white at the inner edge to deep oranges and reds at the periphery.

### 2. Procedural Noise
The wispy, organic look of the disc is generated via **Domain-Warped Perlin Noise**. A noise texture is generated on the CPU using a custom gradient hash and then passed to the GPU. The kernel samples this noise using polar coordinates $(r, \phi)$ to create a realistic, flowing disc structure.

### 3. Environment Mapping
Rays that escape the black hole's gravity sample an **Equirectangular Skybox** (e.g., a high-resolution Milky Way texture). Spherical UV mapping ensures that the stars and nebulae appear correctly distorted by the gravitational lensing.

<script>
  window.MathJax = {
    tex: {
      inlineMath: [['$', '$'], ['\\(', '\\)']],
      displayMath: [['$$', '$$'], ['\\[', '\\]']],
      processEscapes: true
    }
  };
</script>
<script id="MathJax-script" async src="https://cdn.jsdelivr.net/npm/mathjax@3/es5/tex-mml-chtml.js"></script>





