# IRIS by imari
### A real time non-rotating Shwarzchild black hole renderer
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

## GPU accelerated raytracing with Metalcpp