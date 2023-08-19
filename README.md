<div align="center">
   <h1>Arkose Renderer</h1>
   <h3><i>A flexible rendering engine for real-time graphics R&D</i></h3>
</div>

![Header image](/assets/demo/demo1.jpg)

*Arkose Renderer* tries to make it as simple as possible for you to write graphics features while still keeping it expressive enough to allow you to do graphics R&D.

It can support multiple rendering backends and abstracts the specifics with a coarse granularity (i.e. not 1:1 with APIs) which focuses more on logical groupings than hardware specifics, while staying performant. It is based on modern rendering APIs and heavily relies on features such as bindless textures and ray tracing.

Arkose Renderer is very much a rendering engine and *not* a game engine, but I have some ambition to over time make it into more of a general purpose system with physics, audio, and gameplay scripting.

<img src="assets/demo/demo3.jpg" width="49%"></img>
<img src="assets/demo/demo2.jpg" width="49%"></img>

## Incomplete list of features

This list is not complete, it's just a showcase of various features that are implemented, presented in no significant order.

### Engine features

 - Async asset loading (for some resource types)
 - Custom asset system for all common asset types
 - Offline image block compression (BC5 & BC7)
 - Simple animation engine for e.g. skeletal meshes
 - Custom task graph implementation (work/job system)
 - Shader hot-reloading with support for `#include`s
 - Suite of editor tools, gizmos, and debug rendering
 - Tight integration with CPU & GPU profiling tools
 - Physics (work-in-progress)

### Rendering features & techniques

 - Real-time ray tracing
 - Bindless texture support
 - Skeletal mesh skinning and rendering
 - Meshlet based visibility buffer rendering with meshlet culling
 - GPU driven rendering, with object-level culling (still supported as part of the old path, but visibility buffer is now the main rendering path)
 - A realistic camera model, with focus and exposure controls familiar to photographers
 - Depth of field, respecting the realistic camera model paramer such as aperture size and focal length
 - Dynamic Diffuse Global Illumination (DDGI) – a probe based global illumination solution with infinite light bounces.
 - Ray traced reflections with denoising
 - Temporal Anti-Aliasing (TAA)

 > **DISCLAIMER:** There is still a long list of features to add, many of them quite basic. The philosophy with this project has always been to work on whatever I feel like at that point in time, so there is no real concept of natural order here or minimum viable product. We're not in production after all :^) Some of these obvious basic features that I can think of right now are: light culling to make the rendering tiled or clustered and proper support for transparent objects.

![GUI demo image](/assets/demo/demo-gui.jpg)

## Repository structure

```
Arkose
|-- arkcore         # all reusable arkose code
|   |-- asset       # asset types, serialization, and importing
|   |-- core        # various core features: maths, task graph, etc.
|   |-- physics     # physics definitions
|   |-- rendering   # rendering definitions
|   `-- scene       # scene definitions
|-- arkose          # all arkose engine & runtime code
|   | main.cpp      # application entry point
|   |-- apps        # apps, e.g. a game or a graphics showcase
|   |-- physics     # root for physics code
|   |-- rendering   # root for rendering code
|   |   |-- backend # rendering backend code (RHI) for interfacing with graphics APIs
|   |   `-- nodes   # rendering techniques and features (API agnostic)
|   |-- scene       # scene representation, e.g. scene, camera, lights
|   `-- shaders     # all shader code used by arkose in run-time
|-- assets          # all assets used by the engine & apps
`-- deps            # root for in-tree code dependencies
```

Note that some details are omitted for brevity.

## Setup

 > **DISCLAIMER:** Nothing about Arkose is platform specific but it has only ever been compiled and run by me on *Windows*. Additionally, while we're graphics API agnostic the only fully featured backend is for *Vulkan*. Finally, since much of this engine relies on modern graphics features you will need a fairly modern GPU to handle most of the demos. With all of these things in mind...

Here are some basic steps to get it compiling & running for you:

 1. Download or clone this repository
 1. Download & install the Vulkan SDK from https://vulkan.lunarg.com/
 1. Create project files using CMake for your generator of choice. All third-party dependencies besides Vulkan are either already in-tree or dowloaded by CMake automatically via [FetchContent](https://cmake.org/cmake/help/latest/module/FetchContent.html).

You should now be able to compile and run!

## License

This project is licenced under the [MIT License](https://choosealicense.com/licenses/mit/). See the file `LICENSE` for more information. Third-party dependencies (in `deps/`) and third-party assets (in `assets/`) are potentially licensed under other terms. See their respective directories & files for additional information. Some individual files and functions may also be licensed separately and are marked as such in or near the relevant files.
