# dethrace — SDL3 GPU backend Porting Guide

## Project Overview

dethrace is a reimplementation of Carmageddon (1997) using the BRender-v1.3.2 library. The original shipped as 32-bit x86 (little-endian). We are porting to SDL3 GPU backend over old vkrend.

## Code Structure

```
src/
  DETHRACE/
    common/         — game logic, menus, rendering helpers
    pc-all/         — shared platform layer (allsys.c)
    pc-dos/         — DOS platform layer
    pc-win95/       — Win32 platform layer
    pd/             — platform abstraction (DR* API)
  harness/          — SDL2 + OpenGL rendering harness
  S3/               — audio (Smacker/S3)
  library_brender.h — BRender API declarations

lib/
  BRender-v1.3.2/
    core/
      fw/           — framework (bswap.c, brhton.h, datafile.c)
      pixelmap/     — pixelmap ops (cmemloops.c, pmfile.c, pmmem.c)
      inc/          — headers (colour.h)
    drivers/
      commonrend/  - shaders, devpixmp
      glrend/       — OpenGL driver (video.c, v1model.c, devpixmp.c)
      sdl3rend/    — SDL3GPU driver (old Vulkan driver) (video.c, renderer.c, devpmgpu.c)
      softrend/     — software rasterizer (faceops.c, convert.c)
    x86emu/         — x86 fpu emulation for non-x86 targets
```


## SDL3 GPU backend:
Requirement: a thin backend-ops interface (like the existing device callback struct) so GL stays immediate-mode while VK/SDL3-GPU stay command-buffer based. Keep `video.c`/`video.h` per-backend.
- SDL3 GPU is Vulkan-shaped (`SDL_AcquireGPUCommandBuffer` → `SDL_BeginGPURenderPass` → bind → `SDL_DrawGPUIndexedPrimitives` → submit; in-flight frames, `SDL_UploadToGPUBuffer`, push constants, dynamic ring buffers), so ~80% of `sdl3rend`'s structure carries over.
- SDL3 GPU accepts SPIR-V (`SDL_GPU_SHADERFORMAT_SPIRV`), so `drivers/commonrend/*.glsl` can be used directly (`##ifdef SDL3GPU` / `##ifdef GL_ES` / `##ifdef GL_CORE` markers, filtered via `cmake/FilterShader.cmake`); swapchain/surface/present/fence boilerplate in `sdl3rend/video.c` (~800 lines) is replaced by SDL3 GPU.
- Remove the current SDL2/SDL3 window wiring in the game (`src/`) and wire the SDL3 GPU renderer instead. dethrace already uses SDL3 for windows/input.

- Later rename BREND_DRIVER_SDL3REND to just BREND_DRIVER_SDL3 only after use confirms SDL3 GPU is finally working
