# dethrace — Audio Backend (Replace miniaudio with SDL Audio)

## Project Overview

dethrace is a reimplementation of Carmageddon (1997) using the BRender-v1.3.2 library. The original shipped as 32-bit x86 (little-endian). We are porting to SDL audio.

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
