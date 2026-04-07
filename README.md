# Photon - Adaptive Hybrid Ray Tracer

A production-grade adaptive ray tracing/path tracing system in **C17** that enables
graphically demanding games to run on low-end hardware (GTX 1060 6GB, no RT cores).

## Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                        Application (main.c)                      │
│  Window management • Input handling • Camera • Debug overlay     │
├─────────────────────────────────────────────────────────────────┤
│                      Renderer API (renderer.h)                   │
│  rt_renderer_create • rt_render_frame • rt_set_quality_preset    │
├──────────┬──────────┬──────────┬──────────┬────────────────────┤
│   Scene  │  Budget  │ Denoise  │  Probes  │  Vulkan Backend    │
│ Meshes   │ Adaptive │ À-Trous  │ DDGI-    │  G-Buffer          │
│ BVH      │ Ray      │ Temporal │ inspired │  Compute dispatch  │
│ TLAS/BLAS│ Budget   │ SVGF     │ SH L2    │  Pipelines         │
├──────────┴──────────┴──────────┴──────────┴────────────────────┤
│                    Core Systems                                  │
│  BVH (SAH) • Ray Packets (SSE) • PBR Materials • Camera/Frustum │
│  Memory Arenas • SIMD Math • Morton Codes • SH Evaluation        │
└─────────────────────────────────────────────────────────────────┘
```

## Frame Pipeline

1. **CPU**: Update scene transforms, refit BVH
2. **GPU**: Rasterize G-buffer (MRT: albedo, normals, roughness, motion, depth)
3. **GPU**: Shadow ray tracing (compute shader)
4. **GPU**: Reflection ray tracing (compute, selective by roughness)
5. **GPU**: GI ray tracing (quarter resolution, probe-assisted)
6. **GPU**: Spatial denoising (À-Trous wavelet, 5 iterations)
7. **GPU**: Temporal denoising (reprojection + variance clipping)
8. **GPU**: Composite all layers + TAA + tonemapping

## Building

### Prerequisites
- C17 compiler (GCC 9+, Clang 10+, MSVC 2019+)
- CMake 3.16+
- Optional: Vulkan SDK, GLFW 3.3+, glslangValidator

### Quick Build
```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --config Release
```

The project compiles in **headless stub mode** without any external dependencies.
Install GLFW and Vulkan SDK for full functionality.

### Windows (MSVC)
```powershell
mkdir build; cd build
cmake .. -G "Visual Studio 17 2022" -A x64
cmake --build . --config Release
```

### Linux (GCC)
```bash
mkdir build && cd build
cmake .. -DCMAKE_C_COMPILER=gcc -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

## Project Structure

```
Photon/
├── CMakeLists.txt              # Build system
├── README.md                   # This file
├── include/                    # Public headers
│   ├── photon_types.h          # Core type definitions
│   ├── renderer.h              # Public API
│   ├── bvh.h                   # BVH construction & traversal
│   ├── ray.h                   # Ray structures & generation
│   ├── material.h              # PBR materials & textures
│   ├── camera.h                # Camera & frustum culling
│   ├── scene.h                 # Scene management
│   ├── budget.h                # Adaptive ray budget
│   ├── denoise.h               # Denoising control
│   ├── probe.h                 # Irradiance probe grid
│   ├── vulkan_backend.h        # GPU backend
│   ├── math_util.h             # SIMD math utilities
│   └── memory.h                # Memory allocators
├── src/                        # Implementation
│   ├── main.c                  # Entry point & main loop
│   ├── renderer.c              # Renderer pipeline
│   ├── bvh.c                   # SAH BVH builder
│   ├── ray.c                   # Ray operations
│   ├── material.c              # PBR BRDF & textures
│   ├── camera.c                # Camera system
│   ├── scene.c                 # Scene graph
│   ├── budget.c                # Ray budget manager
│   ├── denoise.c               # Denoise state
│   ├── probe.c                 # Irradiance probes
│   ├── vulkan_backend.c        # Vulkan/stub backend
│   ├── math_util.c             # SIMD math
│   ├── memory.c                # Arena & pool allocators
│   └── stb_image.h             # Image loading (stub)
├── shaders/                    # GLSL shaders
│   ├── bvh_common.glsl         # Shared BVH traversal
│   ├── gbuffer.vert            # G-buffer vertex shader
│   ├── gbuffer.frag            # G-buffer fragment shader
│   ├── shadow_trace.comp       # Shadow ray tracing
│   ├── reflection_trace.comp   # Reflection ray tracing
│   ├── gi_trace.comp           # GI ray tracing
│   ├── denoise_spatial.comp    # À-Trous spatial denoiser
│   ├── denoise_temporal.comp   # Temporal denoiser
│   └── composite.comp          # Final composition
└── config/
    └── quality_presets.ini      # Quality settings
```

## Controls

| Key | Action |
|-----|--------|
| W/A/S/D | Move camera |
| Q/E | Up/Down |
| 1-5 | Quality presets (Potato → Ultra) |
| F1-F10 | Debug visualization modes |
| F11 | Toggle stats overlay |
| F12 | Screenshot |
| P | Pause |
| ESC | Quit |

## Quality Presets

| Preset | Ray Budget | Shadow | Reflect | GI | Target FPS |
|--------|-----------|--------|---------|-----|------------|
| Potato | 1M | 0.25x | Off | Off | 30 |
| Low | 2M | 0.5x | 0.25x | 0.25x | 30 |
| Medium | 4M | 1.0x | 0.5x | 0.5x | 45 |
| High | 8M | 1.0x | 1.0x | 0.5x | 60 |
| Ultra | 16M | 1.0x | 1.0x | 1.0x | 60 |

## Key Technologies

- **SAH BVH** with binned construction, iterative (no recursion), 64-byte cache-line aligned nodes
- **SIMD ray-AABB** intersection (SSE4, 4 boxes per test)
- **Möller-Trumbore** ray-triangle with vectorized packets
- **À-Trous wavelet** spatial denoising with edge-stopping functions
- **Temporal reprojection** with variance clipping (anti-ghosting)
- **DDGI-inspired** irradiance probes with L2 spherical harmonics
- **Adaptive ray budget** with per-tile priority and debt borrowing
- **Checkerboard rendering** for reduced ray count
- **ACES tonemapping** with TAA resolve

## License

This project is provided for educational and research purposes.
