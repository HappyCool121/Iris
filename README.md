# IRIS by imari
Iris is a a real time GPU-accelerated render engine utilizing MacOS's Metal API for visualizing non-rotating Shwarzchild black holes. It uses a Range-Kutta solver ran on GPU kernels for the simplified null geodesic equations to compute non linear light paths.

### Average render time on Macbook Air M4:
* ~14ms per frame (~70 fps)
* 1000 x 800 screen resolution (~8ms per frame for 800 x 600)
* 0.05 rad step size, 1000 steps

# How to use
The code works right out the box as it contains (almost) all the relavant libraries and dependencies needed. However, this
is designed to work on Macs, more specifically anything with an Apple Sillicon chip (M1 or higher), since it uses
Apple GPU kernels for the raytracing. You need to have Xcode installed to be able to use Metal, and SDL installed via Homebrew. After building the project with CMake, the first render will be a front view of the black hole. There are two primary ways to interact with the renderer; POV camera view (with WASD and arrow keys) and orbital camera view (AD to rotate around the black hole, WS to change the radius of orbit). You can also adjust the sliders to control the accretion disc and position of the sun (not really a sun, more like a star but whatever).

# Tweaking the results

You can directly change the resolution of the render (and the window) by changing the HEIGHT and WIDTH values in application.cpp.
The quality of the raytracing can be adjusted using the step size and step count used for the RK4 light path solver. By default,
the step size is 0.05 radians, with 1000 steps, which yields an accurate render of a typical black hole with a photon ring and gravitational lensing. Decreasing the step size will significantly reduce the performance of the renderer, and may no longer work in real time.

# How does it work?

## The physics (and the math)

At the core of Iris is the simulation of light paths in the curved spacetime around a Schwarzschild Black Hole. Unlike traditional raytracers where light travels in straight lines, Iris computes the null geodesics, which are the curved paths that light follows around the black hole due to its gravity. 

### 1. The Schwarzschild Metric
The engine models a static, non-rotating, spherically symmetric black hole described by the Schwarzschild metric in coordinates $(t, r, \theta, \phi)$, given by 

$$ds^2 = -\left(1 - \frac{2m}{r}\right)c^2 dt^2 + \left(1 - \frac{2m}{r}\right)^{-1} dr^2 + r^2(d\theta^2 + \sin^2\theta d\phi^2)$$

For light rays, the spacetime interval is zero ($ds^2 = 0$). Due to spherical symmetry, we can restrict the motion to the equatorial plane ($\theta = \pi/2, d\theta = 0$). The metric simplifies to:

$$0 = -\left(1 - \frac{2m}{r}\right)c^2 dt^2 + \left(1 - \frac{2m}{r}\right)^{-1} dr^2 + r^2 d\phi^2 \quad \dots (1)$$



\subsection*{2. Constraints for Null Geodesics}
For light rays, the spacetime interval is zero ($ds^2 = 0$). Due to spherical symmetry, we can restrict the motion to the equatorial plane ($\theta = \pi/2, d\theta = 0$). The metric simplifies to:
\[
0 = -\left(1 - \frac{2m}{r}\right)c^2 dt^2 + \left(1 - \frac{2m}{r}\right)^{-1} dr^2 + r^2 d\phi^2 \quad \dots (1)
\]

\subsection*{3. Constants of Motion}
Because the metric is independent of $t$ and $\phi$, we have two conserved quantities along the geodesic, associated with an affine parameter $\lambda$:
\begin{enumerate}
    \item \textbf{Energy ($e$):} From the Killing vector $\xi = \partial_t$:
    \[ \left(1 - \frac{2m}{r}\right) \frac{dt}{d\lambda} = e \]
    \item \textbf{Angular Momentum ($L$):} From the Killing vector $\eta = \partial_\phi$:
    \[ r^2 \frac{d\phi}{d\lambda} = L \]
\end{enumerate}

\subsection*{4. The Radial Equation}
Substituting the expressions for $\frac{dt}{d\lambda}$ and $\frac{d\phi}{d\lambda}$ back into Equation (1):
\[
0 = -\left(1 - \frac{2m}{r}\right)c^2 \left[ \frac{e}{1 - 2m/r} \right]^2 + \left(1 - \frac{2m}{r}\right)^{-1} \left( \frac{dr}{d\lambda} \right)^2 + r^2 \left( \frac{L}{r^2} \right)^2
\]
Multiplying through by $(1 - 2m/r)$ and rearranging for the radial derivative:
\[
\left( \frac{dr}{d\lambda} \right)^2 = e^2 c^2 - \frac{L^2}{r^2} \left( 1 - \frac{2m}{r} \right) \quad \dots (2)
\]

\subsection*{5. Variable Substitution (Binet Transformation)}
To find the shape of the orbit $u(\phi)$, we define $u = 1/r$. Using the chain rule:
\[
\frac{dr}{d\lambda} = \frac{dr}{d\phi} \frac{d\phi}{d\lambda} = \frac{dr}{d\phi} \left( \frac{L}{r^2} \right) = \frac{dr}{d\phi} (L u^2)
\]
Since $r = 1/u$, then $\frac{dr}{d\phi} = -\frac{1}{u^2} \frac{du}{d\phi}$. Substituting this in:
\[
\frac{dr}{d\lambda} = \left( -\frac{1}{u^2} \frac{du}{d\phi} \right) (L u^2) = -L \frac{du}{d\phi}
\]

\subsection*{6. The Orbit Equation}
Substitute $\frac{dr}{d\lambda} = -L \frac{du}{d\phi}$ and $1/r = u$ into Equation (2):
\[
\left( -L \frac{du}{d\phi} \right)^2 = e^2 c^2 - L^2 u^2 (1 - 2mu)
\]
Divide the entire equation by $L^2$:
\[
\left( \frac{du}{d\phi} \right)^2 = \frac{e^2 c^2}{L^2} - u^2 + 2mu^3 \quad \dots (3)
\]

\subsection*{7. The Final Binet Form}
Differentiate Equation (3) with respect to $\phi$ using the chain rule ($\frac{d}{d\phi} [u^n] = n u^{n-1} \frac{du}{d\phi}$):
\[
2 \left( \frac{du}{d\phi} \right) \left( \frac{d^2u}{d\phi^2} \right) = 0 - 2u \left( \frac{du}{d\phi} \right) + 6mu^2 \left( \frac{du}{d\phi} \right)
\]
Dividing both sides by $2 \frac{du}{d\phi}$ (assuming the path is not a perfect circle where $du/d\phi = 0$):
\[
\frac{d^2u}{d\phi^2} + u = 3mu^2
\]
Substituting $m = \frac{GM}{c^2}$ back in, we obtain the Binet equation for light:
\[
\boxed{\frac{d^2u}{d\phi^2} + u = \frac{3GM}{c^2} u^2}
\]

In natural units (where $G = c = 1$ and the Schwarzschild radius $r_s = 2M = 1$), the motion of a photon is governed by the geodesic equation.

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





