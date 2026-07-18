# dethrace — 64-bit Big-Endian Porting Guide

## Project Overview

dethrace is a reimplementation of Carmageddon (1997) using the BRender-v1.3.2 library. The original shipped as 32-bit x86 (little-endian). We are porting to PowerPC 64-bit big-endian while preserving correctness on x86_64.

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
      glrend/       — OpenGL driver (video.c, v1model.c, devpixmp.c)
      softrend/     — software rasterizer (faceops.c, convert.c)
    x86emu/         — x86 fpu emulation for non-x86 targets
```

## Byte-Order Model

### br_colour (BR_PMT_RGBX_888 / RGBA_8888)

```c
// colour.h
typedef br_uint_32 br_colour;
#define BR_COLOUR_RGB(r,g,b) (((r)<<16) | ((g)<<8) | (b))
// Value = 0x00RRGGBB as a uint32

#define BR_RED(c)   (((c) >> 16) & 0xFF)
#define BR_GRN(c)   (((c) >> 8) & 0xFF)
#define BR_BLU(c)   ((c) & 0xFF)
```

`br_colour` is a native `uint32_t`. The `BR_RED/GRN/BLU` macros extract byte fields from the integer VALUE, not from memory — they work identically on LE and BE.

### BR_PMT_RGBX_888 memory layout (4 bytes per pixel)

| Platform | Memory bytes (low→high) | uint32 value |
|----------|------------------------|--------------|
| LE       | `[B, G, R, X]`         | `0x00RRGGBB` |
| BE       | `[0, R, G, B]`         | `0x00RRGGBB` |

Both produce the same integer value via `*(uint32*)ptr`. The file stores bytes as `[B, G, R, X]` (original LE order) and `pmfile.c` swaps on read for BE.

### BR_PMT_RGB_565 memory layout (2 bytes per pixel)

| Platform | Memory bytes (low→high) | uint16 value |
|----------|------------------------|--------------|
| LE       | `[lo, hi]`             | `0xRRRRRGGGGGBBBBB` |
| BE       | `[hi, lo]`             | `0xRRRRRGGGGGBBBBB` |

Bit layout within the uint16 value (not bytes): bits 0-4 = B, bits 5-10 = G, bits 11-15 = R. This matches `GL_UNSIGNED_SHORT_5_6_5`.

### Indexed (BR_PMT_INDEX_8)

1 byte per pixel — no endianness concerns for pixel data. Palette entries are `br_colour` (uint32) values.

## The `BrSwap32` Fix (commit e4fd35c)

**File:** `lib/BRender-v1.3.2/core/fw/bswap.c`

```c
// BROKEN on PPC64 (unsigned long = 64-bit):
//   union { unsigned long l; unsigned char c[4]; } u;
//   On BE, c[0..3] aliases the HIGH 4 bytes of the 64-bit value,
//   which are all zero for 32-bit inputs — swap was a no-op.

// FIXED (br_uint_32 = uint32_t):
//   union { br_uint_32 l; unsigned char c[4]; } u;
//   Correctly aliases the 4 bytes of the 32-bit value.
```

**Impact:** `BrSwap32` now works correctly on PPC64 BE. All macros that call it now function as designed.

### Macro callers (defined in `brhton.h`)

| Macro | On LE | On BE | Purpose |
|-------|-------|-------|---------|
| `BrHtoNL(x)` / `BrNtoHL(x)` | calls BrSwap32 | no-op | network (BE) ↔ host |
| `BrLtoHL(x)` / `BrHtoLL(x)` | no-op | calls BrSwap32 | little-endian ↔ host |

On BE, `BrLtoHL` converts LE data to BE and `BrHtoLL` converts BE data to LE. These were silently broken before e4fd35c.

## Datafile I/O and Byte Swapping

### `DfBlockReadBinary` (`core/fw/datafile.c`)

```c
BrFileRead(base, count, size, df->h);
#if !BR_ENDIAN_BIG
BrSwapBlock(base, count, size);  // convert BE file data to LE on LE hosts
#endif
```

The BRender binary datafile format stores multi-byte values in big-endian (network) order. On LE, `BrSwapBlock` converts to LE. On BE, no swap needed — data is already in native order.

`BrSwapBlock` swaps elements of `size` bytes: 2-byte elements get a full byte swap, 4-byte elements get a full byte swap. This is independent of the fixed `BrSwap32`.

### `FopRead_PIXELS` — Double-Swap Bug (Fixed)

**File:** `lib/BRender-v1.3.2/core/pixelmap/pmfile.c:209-223`

```c
// REMOVED: #if BR_ENDIAN_BIG block that swapped 2-byte and 4-byte pixel data
// After reading via DfBlockReadBinary, pixel data is already in host byte order.
// DfBlockReadBinary on BE leaves data as-is (BE), which is native — no swap needed.
// The additional BE swap was a double-swap that corrupted pixel data.
```

**Impact:** Loading screens and menu backgrounds loaded from .PIX files had inverted colors (red↔green). `DfBlockReadBinary` already leaves pixel data in native byte order on all platforms. The extra `#if BR_ENDIAN_BIG` swap in `FopRead_PIXELS` was a double-swap on BE.

## UBO Data (`byteswap_ubo` — Fixed)

**File:** `lib/BRender-v1.3.2/drivers/glrend/video.h:128-140`

```c
// REMOVED: #if BR_ENDIAN_BIG block that swapped every 4-byte word in UBO buffers
// OpenGL spec: "The data in the buffer is stored in the byte order of the client."
// On BE, swapping to LE corrupted ALL UBO data: matrices became garbage floats,
// flag/enum fields became wrong values (e.g. lighting=1 → 0x01000000).
// The GL driver handles any GPU-side endian conversion.
```

**Impact:** The black in-game 3D rendering. `byteswap_ubo` was converting UBO data from native BE to LE on PPC64, violating the GLSL `layout(std140)` spec. This caused:
- All matrix fields (MVP, model_view, normal_matrix) to contain garbage floats → vertices off-screen/culled
- All uint32 flag fields (lighting, uv_source, prelit, num_lights) to have swapped values
- The shader comparison `lighting == 1u` at `brender.vert.glsl:317` would fail because swapped 1 = `0x01000000`

## OpenGL Pixel Type Mappings (`video.c`)

The OpenGL driver maps BRender pixel formats to GL format/type pairs:

| BRender format | GL format | GL type | Notes |
|----------------|-----------|---------|-------|
| BR_PMT_RGBX_888 | GL_BGRA | GL_UNSIGNED_INT_8_8_8_8_REV | works on both LE and BE |
| BR_PMT_RGBA_8888 | GL_BGRA | GL_UNSIGNED_INT_8_8_8_8_REV | works on both LE and BE |
| BR_PMT_RGB_565 | GL_RGB | GL_UNSIGNED_SHORT_5_6_5 | works on both LE and BE |
| BR_PMT_RGB_555 | GL_BGRA | GL_UNSIGNED_SHORT_1_5_5_5_REV | works on both LE and BE |
| BR_PMT_INDEX_8 | GL_RGBA | GL_UNSIGNED_BYTE | expanded to 4 bytes per pixel |

These pairs were chosen so that the native `br_colour` integer value `0x00RRGGBB` produces correct OpenGL output regardless of endianness. The `_REV` suffix on BE is correct because the uint32 VALUE `0x00RRGGBB` stores:
- `GL_UNSIGNED_INT_8_8_8_8_REV` + `GL_BGRA`: R bits 16-23 → GL_RED ✓
- `GL_UNSIGNED_INT_8_8_8_8` (no `_REV`) would give: R bits 8-15 → GL_GREEN (SWAPPED)

## Software Pixel Operations (`cmemloops.c`)

### `_MemPixelSet` / `_MemPixelGet`

| bpp | Operation | Endianness |
|-----|-----------|------------|
| 1 | `*(uint8*)d = (uint8)c` | endian-neutral |
| 2 | `*(uint16*)d = (uint16)c` | writes native uint16 |
| 3 | `d[0]=BLU(c), d[1]=GRN(c), d[2]=RED(c)` | explicit bytes, endian-neutral |
| 4 | `*(uint32*)d = c` | writes native uint32 |

### `_MemFill_A` (bpp=2 case)

```c
MemFill16((uint16*)dest, (uint16)colour, pixels);
```

The `colour` (a `br_colour` = `uint32`) is truncated to `uint16`. The lower 16 bits become the fill value. For paletted pixelmaps (INDEX_8), colour IS a palette index (0-255), so lower 16 bits = correct index. For RGB_565 pixelmaps, the caller must pass a pre-packed 16-bit colour value (not a raw br_colour) — see `PaletteEntry16Bit`.

## Template Query Endian Bug (`BRTV_CONV_COPY`)

**File:** `lib/BRender-v1.3.2/drivers/softrend/renderer.c:195`

```c
// BROKEN on PPC64 BE:
//   ObjectQuery(self->plib, &m, BRT_PARTS_U32);
//   BRTV_CONV_COPY does memcpy(output, &tep->offset, sizeof(output))
//   where tep->offset is a br_uintptr_t (8 bytes) but output is uint32_t (4 bytes).
//   On BE, the first 4 bytes read are the HIGH bytes of the 8-byte value,
//   which are zero when value fits in 32 bits (e.g. 0xC0000100 → high=0x00000000).

// FIXED:
//   m = BR_STATE_CACHE | BR_STATE_OUTPUT | BR_STATE_PRIMITIVE;
```

**Impact:** Without this fix, `ObjectQuery` returns `m=0` on PPC64 BE, so `self->state.valid` never gets primitive/ouput/cache bits. This causes `StateCopyToStored`/`StateCopyFromStored` to skip copying primitive state (filtered by `copy_mask &= src->valid`), so texture and output state is never transferred — sw renderer shows black on 3D models.

**Note:** This is a general BRender BE bug affecting all `BRTV_ABS | BRTV_CONV_COPY` template entries where the source field is `br_uintptr_t` and the output type is smaller. The same pattern exists in `plib.c:44` for `ObjectQuery(self, &n, BRT_PARTS_U32)`.

## Pentrim Scanline Pointer Comparison Fix

**Files:** `lib/BRender-v1.3.2/drivers/pentprim/fti8pizp.c:257,723`

The per-pixel scanline loop compares the current-dest and end-of-scanline pointers using `edi.v > ecx.v` (reading `uint32_t` from the `x86_reg` union). On PPC64 BE, `.v` reads the **high 32 bits** of the pointer — both pointers have identical high bits, so the comparison **always fails** and the loop never exits.

**Fix:** Changed `.v > .v` to `.ptr_v > .ptr_v` (full pointer comparison) in both scanline renderers.

## Sound Fix (PPC64 BE)

**Files:** `src/S3/s3sound.c`, `src/S3/s3_defs.h`, `src/S3/include/s3/s3.h`, `src/harness/CMakeLists.txt`

The sound segfault on PPC64 BE was caused by:
- Calling convention mismatches in S3 library function signatures
- Incorrect `unsigned long` usage where `uint32_t` was needed (struct field widths differ on 64-bit)

**Fix:** Corrected function signatures and removed CMake `NO_SWAP` conditionals that blocked byte-swap logic on big-endian.

## Known Issues on PPC64 BE

### 1. Main Menu Colors (Red↔Green swap)

- **Status:** Fixed
- **Root cause:** Double-swap in `FopRead_PIXELS` (pmfile.c:209-223) on BE.
- **Fix:** Removed the `#if BR_ENDIAN_BIG` byte-swap block from `FopRead_PIXELS`.

### 2. In-Game OpenGL Black Rendering

- **Status:** Fixed
- **Root cause:** `byteswap_ubo` in `video.h:128-140` violating OpenGL spec.
- **Fix:** Made `byteswap_ubo` a no-op on all platforms.

### 3. Palette Data Byte Order

- **Status:** Fixed
- `FopRead_PIXELS` swaps 4-byte palette entries on BE after `DfBlockReadBinary`
- FLIC playback (`flicplay.c:DoColourMap`) writes bytes in platform-specific order
- Palette entries read as `((br_colour*)pixels)[i]` are correct native uint32 values

### 4. Pentrim/Softrend Textures — Missing on 3D Models (PPC64 BE Only)

- **Status:** Fixed
- **Root causes (two independent bugs):**
  1. `ObjectQuery` endian bug in `renderer.c:195` — `BRTV_CONV_COPY` from 64-bit `br_uintptr_t` field to 32-bit output returns zero on BE
  2. `.v` vs `.ptr_v` pointer comparison in `fti8pizp.c:257,723` — scanline loop never exits on 64-bit BE
- **Fix:** Hardcoded parts mask in `renderer.c:195`; changed `.v > .v` to `.ptr_v > .ptr_v` in `fti8pizp.c`

### 5. Sound — Segfault on Playback (PPC64 BE Only)

- **Status:** Fixed
- **Root cause:** Calling convention mismatches and `unsigned long`/`uint32_t` width issues in S3 audio library.
- **Fix:** Corrected function signatures and removed CMake `NO_SWAP` conditionals.

### 6. Stationary/Scenario Car Black Faces on Software Renderer (PPC64 BE Only)

- **Status:** Open — minor
- **Symptoms:** Some stationary/scenario car models have black faces (hood/ceiling). Changes with viewing angle/distance — behavior resembles lighting or shadow being applied differently. Player car and opponent cars render correctly. Renders fine on OpenGL (same PPC64 BE system). Works fine on x86_64 on both renderers.
- **Suspected cause:** Likely a shade table, lighting, or face culling issue specific to the pentrim software rasterizer on BE. Possible areas:
  - Shade table generation or byte ordering in `allsys.c`/`utility.c`
  - `faceops.c` — software triangle rasterizer color computation
  - Shade table indexing in the pentrim scanline renderers
  - Back-face culling with different winding detection on BE
- **Files to check:**
  - `src/DETHRACE/pc-all/allsys.c` — `PDSetPalette`, shade table setup
  - `src/DETHRACE/common/utility.c` — `PaletteEntry16Bit`, shade generation
  - `lib/BRender-v1.3.2/drivers/softrend/faceops.c` — color replication
  - `lib/BRender-v1.3.2/drivers/pentprim/fti8pizp.c` — shade table lookup in scanline

### 7. Electro-Bastard Ray — Not Rendered on OpenGL

- **Status:** Fixed
- **Root cause:** Three independent bugs:
  1. `DrawLine3DThroughBRender` in `spark.c:197` called `BrModelUpdate(gLine_model, BR_MODU_VERTEX_POSITIONS)` which only updates vertex positions in the prepared model. The material has `BR_MATF_PRELIT`, so colours come from `vertex_colours` in the stored geometry — but colours were never copied. The prepared model's group `vertex_colours[v2]` stayed at initial 0 (black), making the triangle invisible on GL.
  2. `RenderProximityRays` (pedestrn.c) called `DrawLine3D` without calling `SetLineModelCols` first, so the model's vertex colours were stale/uninitialized. The spark path (`ReplaySparks`) did call it, but the ray path didn't.
  3. `RenderProximityRays` didn't reparent `gLine_actor` under `pCamera` before `BrZbSceneRenderAdd`. The vertices are in camera-space (transformed via `DRMatrix34TApplyP`), but the actor remained under `gDont_render_actor`, so the camera-space coordinates got double-transformed by the world→view pipeline → triangle off-screen.
- **Fix:** 
  1. Changed `BR_MODU_VERTEX_POSITIONS` to `BR_MODU_VERTEX_POSITIONS | BR_MODU_VERTEX_COLOURS` in `spark.c:197` so vertex colours propagate to stored geometry.
  2. Added `SetLineModelCols(1)` call in `RenderProximityRays` (`pedestrn.c:3915`) so the electro-bastard ray's BRender path has valid vertex colours.
  3. Added `BrActorRemove/Add(pCamera)` reparenting in `RenderProximityRays` (`pedestrn.c:3937-3942, 3988-3993`) matching the pattern used in `ReplaySparks` (spark.c:484-496).

### 8. Cockpit White Screen + Broken Mirror on OpenGL (3DFX Path — Cross-Platform)

- **Status:** Fixed
- **Symptoms:** On levels with fog/DepthEffect active, the cockpit view showed an all-white screen instead of the cockpit interior. The rearview mirror was invisible on fog maps (and later on all maps during development). A lightblue/cyan flash appeared in the windscreen area on damage.
- **Root cause:** Three bugs in the 3DFX overlay rendering path (`graphics.c, devpixmp.c`):
  1. **Sub-pixelmap rectangleFill row stride bug** — The GL driver's `rectangleFill` (`devpixmp.c:342-346`) wrote `px16[y * self->pm_width + x]` but for sub-pixelmaps `pm_width` is the sub-rect width, not the parent's row stride. This wrote pixels at wrong buffer locations → **corrupted lockedPixels, view distance clipping artifacts, cyan flash**.
  2. **No-op sub-pixelmap flush in CFWS** — `ConditionallyFillWithSky`'s `BrPixelmapFlush(pPixelmap)` on a sub-pixelmap (`gRender_screen`) was a no-op because `asBack.possiblyDirty` was never managed on sub-pixelmaps. The fog/sky fill stayed in `lockedPixels` but was never uploaded to the overlay → **white screen foreground**.
  3. **Mirror fill covering mirror 3D scene** — `ConditionallyFillWithSky(gRearview_screen)` filled the mirror area of `lockedPixels` (with fog color) just before the mirror 3D scene, but the subsequent flush uploaded the filled overlay ON TOP of the mirror scene → **mirror invisible on fog maps**.
- **Fix (three parts):**
  - **devpixmp.c rectangleFill row stride fix**: Changed inner loops to use `(y + pm_base_y) * (pm_row_bytes / Bpp) + (x + pm_base_x)` instead of `y * pm_width + x`, correctly handling sub-pixelmap base offsets and parent row stride.
  - **devpixmp.c sub-pixelmap flush safety**: Added early return in `BrPixelmapFlush` for sub-pixelmaps — the parent always handles flushing.
  - **graphics.c**: Added `BrPixelmapFlush(gBack_screen)` after `PDUnlockRealBackScreen(1)` and before the 3D scene loop, ensuring the sky/fog fill (and any 2D content) is always uploaded to the overlay regardless of whether CFWS's internal flush was a no-op.
  - **graphics.c mirror**: Removed `ConditionallyFillWithSky(gRearview_screen)` from the mirror block — the buffer is already freshly reset to transparent purple by the preceding flush, so the mirror area is transparent without an explicit fill. The mirror 3D scene renders ON TOP of the overlay.
  - **graphics.c cockpit**: Re-enabled the 3DFX cockpit `CopyStripImage` call (was wrapped in `#if 0`).
- **Files changed:**
  - `src/DETHRACE/common/graphics.c`
  - `lib/BRender-v1.3.2/drivers/glrend/devpixmp.c`

### 9. Z-Fighting on Distant Geometry (OpenGL — GPU-Dependent)

- **Status:** Not a code bug (cross-platform, GPU-dependent)
- **Symptoms:** Z-fighting / depth-flickering on distant geometry when using the OpenGL driver. Objects render correctly when viewed up close.
- **Root cause:** BRender's projection matrix depth precision interacts differently with different GPU drivers. Integrated GPUs (e.g. Vega 8 GCN5) show it; discrete GPUs (e.g. AMD Caicos) may not due to different depth buffer precision characteristics. Not an endianness or code logic issue.
- **Workaround:** None in BRender itself — would require modifying the projection matrix depth range to improve precision at distance.

## Critical Rules (Do Not Break x86_64)

1. **Unconditional `BrSwap32` calls in game code:**
   - `loading.c:ReadU32/ReadS32/WriteU32L` use `#if BR_ENDIAN_BIG` guards
   - `loading.c:2950,2971` use `#if !BR_ENDIAN_BIG` guards
   - These must remain conditional

2. **`datafile.c:DfBlockReadBinary` `BrSwapBlock` call:**
   - Uses `#if !BR_ENDIAN_BIG` — only runs on LE
   - Must NOT be enabled on BE

3. **OpenGL format/type pairs in `video.c`:**
   - Current `GL_UNSIGNED_INT_8_8_8_8_REV` works correctly on both LE and BE
   - Must not be reverted to the old endian-conditional approach

4. **`BR_ENDIAN_BIG` / `BR_ENDIAN_LITTLE` preprocessor:**
   - These are defined by the BRender cmake build system based on target architecture
   - Do NOT add platform-specific `#ifdef __powerpc__` or similar — use these macros

5. **`x86emu` FPU emulation:**
   - When `BR_DO_NOT_USE_X86_FPU` or `BR_HAS_X86EMU` is defined, the x86 FPU instructions in BRender are emulated via the `x86emu/` library
   - This is required on PPC64 for BRender's software rasterizer

## Files Requiring Endianness Care

| File | What to watch |
|------|---------------|
| `flicplay.c` | `DoColourMap`/`DoColour256` byte writes |
| `allsys.c` | `Copy8BitTo16BitPixelmap`, `Double8BitTo16BitPixelmap`, `PDSetPalette` |
| `utility.c` | `PaletteEntry16Bit`, `PaletteOf16Bits`, BMP file I/O |
| `graphics.c` | `DRSetPaletteEntries`, vertex data extraction at line 3591 |
| `pmfile.c` | `FopRead_PIXELS` BE swap (potential double-swap bug) |
| `pmdisp.c` / `pmnull.c` | Pixelmap device dispatch |
| `dossys.c` | `PDSetPaletteEntries`, ASCII table swap |
| `win95sys.c` | `Win32SetPaletteEntries` |
| `devpixmp.c` | `rectangleCopyTo` — palette expansion for indexed textures |
| `faceops.c` | Software triangle rasterizer, colour replication |
| `v1dbfile.c` | Model file loading, struct_read with BrNtoHL |
| `fti8pizp.c` | Pentrim scanline renderer — `.v` vs `.ptr_v` pointer comparison |

## Testing

- **CI:** GitHub Actions. Tests run on x86_64 only. Changes must compile on x86_64 without warnings.
- **Manual testing on PPC64:** Use QEMU user mode (`qemu-ppc64le` or `qemu-ppc64`) or a POWER9/10 system.
- **Key test scenarios:**
  - Main menu renders with correct colors
  - In-game 3D rendering produces visible geometry (not black)
  - HUD/text overlay renders correctly
  - FLIC cutscenes play with correct colors
  - Palette transitions (fade, drugs power-up) animate smoothly
  - Electro-bastard ray weapon effect visible on OpenGL

## Build System

CMake-based. Target architecture is detected automatically. Key variables:
- `BR_ENDIAN_BIG` / `BR_ENDIAN_LITTLE` — set via `compile_definitions` in CMake
- `BR_HAS_X86EMU` — enables x86 FPU emulation for non-x86 targets
- Build with `-DBRETHR=ON` for original BRender or the bundled copy

## Future Work: Audio Backend (Replace miniaudio with SDL Audio)

### Current Architecture

The `AudioBackend_*` API (defined in `src/harness/include/harness/audio.h`) provides a 15-function abstraction over the audio system:

- **SFX:** `AudioBackend_Init/UnInit`, `AudioBackend_AllocateSampleTypeStruct`, `AudioBackend_PlaySample`, `AudioBackend_SoundIsPlaying`, `AudioBackend_StopSample`, `AudioBackend_SetVolume`, `AudioBackend_SetPan`, `AudioBackend_SetFrequency`, `AudioBackend_SetVolumeSeparate`
- **Music (CD Audio):** `AudioBackend_InitCDA/UnInitCDA`, `AudioBackend_PlayCDA/StopCDA`, `AudioBackend_CDAIsPlaying`, `AudioBackend_SetCDAVolume`
- **Streaming (Smacker video):** `AudioBackend_StreamOpen/Write/Close`

Current implementation: `src/harness/audio/miniaudio.c` (uses miniaudio single-header lib + stb_vorbis for OGG).
Stub implementation: `src/harness/audio/null.c`.

### Files

| File | Purpose |
|------|---------|
| `src/harness/include/harness/audio.h` | `AudioBackend_*` abstraction header |
| `src/harness/audio/miniaudio.c` | miniaudio backend (active when `DETHRACE_SOUND_ENABLED=ON`) |
| `src/harness/audio/null.c` | Stub backend (active when `DETHRACE_SOUND_ENABLED=OFF`) |
| `lib/miniaudio/include/miniaudio/miniaudio.h` | Single-header miniaudio v0.11.25 |
| `lib/miniaudio/CMakeLists.txt` | INTERFACE library |
| `lib/stb/` | stb_vorbis for OGG decoding (used by miniaudio) |
| `src/S3/` | Game-level audio management (talks to `AudioBackend_*`) |
| `src/harness/CMakeLists.txt:54-59` | Conditional miniaudio vs null compilation |

### Replacing with SDL Audio

The `AudioBackend_*` abstraction makes the swap feasible. A new `src/harness/audio/sdl.c` would implement it using SDL2/SDL3 audio.

**SDK differences:**
- **SDL2:** Queue-based model (`SDL_QueueAudio`). No per-stream volume/pan/pitch. Requires manual mixing callback for concurrent sounds. Pitch needs external resampling.
- **SDL3:** Stream-based (`SDL_AudioStream`). Has `SDL_SetAudioStreamGain` (volume) and `SDL_SetAudioStreamFrequencyRatio` (pitch). Closer to miniaudio's model.
- Neither has OGG built-in — keep stb_vorbis for music decoding.
- Neither has built-in pan — manual L/R channel mixing required.

**Required symbols to add to `sdl2_syms.h` / `sdl3_syms.h`:**
- SDL2: `OpenAudioDevice`, `CloseAudioDevice`, `PauseAudioDevice`, `QueueAudio`, `GetQueuedAudioSize`, `ClearQueuedAudio`, `LoadWAV`, `FreeWAV`, `BuildAudioCVT`, `ConvertAudio`
- SDL3: `OpenAudioDevice`, `CloseAudioDevice`, `PauseAudioDevice`, `ResumeAudioDevice`, `LoadWAV`, `FreeWAV`, `CreateAudioStream`, `DestroyAudioStream`, `BindAudioStream`, `UnbindAudioStream`, `PutAudioStreamData`, `GetAudioStreamData`, `GetAudioStreamAvailable`, `SetAudioStreamGain`, `SetAudioStreamFrequencyRatio`, `ClearAudioStream`

## Future Work: Vulkan Render Driver

### Scope

A Vulkan driver would be a new BRender device driver alongside `glrend/` and `softrend/`. It would need to implement the same device pixelmap interface that GL uses.

### GL Driver Files to Port

| GL File | Lines | Vulkan Equivalent |
|---------|-------|-------------------|
| `lib/BRender-v1.3.2/drivers/glrend/video.c` | ~1000 | VkInstance/device/swapchain, SPIR-V pipelines, render passes, framebuffers |
| `lib/BRender-v1.3.2/drivers/glrend/video.h` | ~150 | Vulkan equivalents of GL state (descriptor sets, pipeline layouts) |
| `lib/BRender-v1.3.2/drivers/glrend/v1buffer.c` | ~500 | VkBuffer/memory management, descriptor pools |
| `lib/BRender-v1.3.2/drivers/glrend/v1model.c` | ~600 | Command buffer generation, indexed draws for each model |
| `lib/BRender-v1.3.2/drivers/glrend/devpixmp.c` | ~500 | VkImage staging, blit commands, layout transitions, flush |
| `lib/BRender-v1.3.2/drivers/glrend/renderer.c` | ~200 | Primary command buffer, sync (semaphores/fences), dynamic state |
| `lib/BRender-v1.3.2/drivers/glrend/ext_procs.c` | ~20 | Present queue submission, swapchain acquire |
| `lib/BRender-v1.3.2/drivers/glrend/brender.vert.glsl` | | SPIR-V vertex shader |
| `lib/BRender-v1.3.2/drivers/glrend/brender.frag.glsl` | | SPIR-V fragment shader |

### New Dependencies

- Vulkan SDK (headers, loader, validation layers)
- SPIR-V toolchain: compile GLSL to SPIR-V at build time or bundle pre-compiled `.spv` binaries
- SDL Vulkan surface symbols: `SDL_Vulkan_LoadLibrary`, `SDL_Vulkan_GetInstanceExtensions`, `SDL_Vulkan_CreateSurface`

### CMake Wiring

New directory: `lib/BRender-v1.3.2/drivers/vkrend/`. New device identifier would be `"vkrend"`. Add a build option akin to `DETHRACE_VULKAN=ON` that links against Vulkan::Vulkan and compiles the vkrend sources.

### Runtime Config

In `dethrace.ini`, `Emulate3DFX` would use value `2` to select Vulkan. In `allsys.c`, the init path would check `harness_game_config.opengl_3dfx_mode >= 2` to create the Vulkan device via `BrDevBeginVar` with the `"vkrend"` driver identifier.

### Architecture Notes

- BRender's rendering model: single pass, indexed triangles, 2 UBOs (model-view + shade table), one combined image sampler per material, prelit or lit with flat shading.
- The GL driver uses `GL_UNSIGNED_INT_8_8_8_8_REV` + `GL_BGRA` for pixel uploads — Vulkan equivalent would be `VK_FORMAT_B8G8R8A8_UNORM` with `VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL`.
- The software renderer's shade tables and palettes are not used by the GL driver — the GL driver does all lighting in shaders. Vulkan would follow the same approach.
- The `byteswap_ubo` fix (removed endian-conditional swap in UBO uploads) applies equally to Vulkan push constants / uniform buffers. Vulkan spec: "The data in the buffer is stored in the byte order of the client."

### Development Milestones

1. Get a single triangle on screen via Vulkan WSI (SDL surface + swapchain)
2. Port `brender.vert/frag` to SPIR-V, render a BRender model with correct MVP transform
3. Implement texture upload (BR_PMT_RGBX_888 → VK_FORMAT_B8G8R8A8_UNORM)
4. Implement pixelmap flush (staging buffer → image)
5. Pixelmap fill/copy operations (rectangleFill, rectangleCopyTo)
6. Scene begin/end (clear depth, set dynamic viewport)
7. Swap buffers (present with semaphore sync)
8. 3D overlay compositing (sub-pixelmap handling, purple-key like the GL driver)
