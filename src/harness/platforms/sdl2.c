#include <SDL.h>
#include <SDL_vulkan.h>

#include "harness.h"
#include "harness/config.h"
#include "harness/hooks.h"
#include "harness/trace.h"
#include "sdl2_scancode_map.h"
#include "sdl2_syms.h"
#include "imgui/imgui_manager.h"

SDL_COMPILE_TIME_ASSERT(sdl2_platform_requires_SDL2, SDL_MAJOR_VERSION == 2);

static SDL_Window* window;
static SDL_Renderer* renderer;
static SDL_Texture* screen_texture;
static br_uint_32 converted_palette[256];
static br_pixelmap* last_screen_src;

static SDL_GLContext* gl_context;

static int render_width, render_height;

static Uint32 last_frame_time;

static void (*gKeyHandler_func)(void);

// 32 bytes, 1 bit per key. Matches dos executable behavior
static br_uint_32 key_state[8];

static struct {
    int x, y;
    float scale_x, scale_y;
} viewport;

// Callbacks back into original game code
extern void QuitGame(void);
extern br_pixelmap* gBack_screen;
extern int gHarness_sw_widescreen;
extern br_uint_32 gI_am_cheating;
extern int gFreeze_powerups;
extern void ToggleTimerFreeze(void);
extern void TogglePowerupFreeze(void);
extern void ToggleInvulnerability(void);
extern void ToggleFlying(void);
extern void TotalRepair(void);
extern void IncrementLap(void);
extern void MoreTime(void);
extern void EarnDosh(void);
extern void LoseDosh(void);
extern void GetPowerup(int pNum);

#ifdef DETHRACE_SDL_DYNAMIC
#ifdef _WIN32
static const char* const possible_locations[] = {
    "SDL2.dll",
};
#elif defined(__APPLE__)
#define SHARED_OBJECT_NAME "libSDL2"
#define SDL2_LIBNAME "libSDL2.dylib"
#define SDL2_FRAMEWORK "SDL2.framework/Versions/A/SDL2"
static const char* const possible_locations[] = {
    "@loader_path/" SDL2_LIBNAME,                     /* MyApp.app/Contents/MacOS/libSDL2_dylib */
    "@loader_path/../Frameworks/" SDL2_FRAMEWORK,     /* MyApp.app/Contents/Frameworks/SDL2_framework */
    "@executable_path/" SDL2_LIBNAME,                 /* MyApp.app/Contents/MacOS/libSDL2_dylib */
    "@executable_path/../Frameworks/" SDL2_FRAMEWORK, /* MyApp.app/Contents/Frameworks/SDL2_framework */
    NULL,                                             /* /Users/username/Library/Frameworks/SDL2_framework */
    "/Library/Frameworks" SDL2_FRAMEWORK,             /* /Library/Frameworks/SDL2_framework */
    SDL2_LIBNAME                                      /* oh well, anywhere the system can see the .dylib (/usr/local/lib or whatever) */
};
#else
#include "elfdlopennote.h"
#ifdef ELF_NOTE_DLOPEN
ELF_NOTE_DLOPEN(
    "SDL2",
    "Platform-specific operations such as creating windows and handling events",
    ELF_NOTE_DLOPEN_PRIORITY_SUGGESTED,
    "libSDL2-2.0.so.0",
    "libSDL2-2.0.so");
#endif
static const char* const possible_locations[] = {
    "libSDL2-2.0.so.0",
    "libSDL2-2.0.so",
};
#endif

static void* sdl2_so;
#endif

#define SDL_NAME "SDL2"
#define OBJECT_NAME sdl2_so
#define SYMBOL_PREFIX SDL2_
#define FOREACH_SDLX_SYM FOREACH_SDL2_SYM

#include "sdl_dyn_common.h"

static void calculate_viewport(int window_width, int window_height) {
    int vp_width, vp_height;
    float target_aspect_ratio;
    float aspect_ratio;

    aspect_ratio = (float)window_width / window_height;
    target_aspect_ratio = (float)gBack_screen->width / gBack_screen->height;

    vp_width = window_width;
    vp_height = window_height;
    if (aspect_ratio != target_aspect_ratio) {
        if (aspect_ratio > target_aspect_ratio) {
            vp_width = window_height * target_aspect_ratio + .5f;
        } else {
            vp_height = window_width / target_aspect_ratio + .5f;
        }
    }
    viewport.x = (window_width - vp_width) / 2;
    viewport.y = (window_height - vp_height) / 2;
    viewport.scale_x = (float)vp_width / gBack_screen->width;
    viewport.scale_y = (float)vp_height / gBack_screen->height;
}

static int SDL2_Harness_SetWindowPos(void* hWnd, int x, int y, int nWidth, int nHeight) {
    // SDL_SetWindowPosition(hWnd, x, y);
    if (nWidth == 320 && nHeight == 200) {
        nWidth = 640;
        nHeight = 400;
    }
    SDL2_SetWindowSize(hWnd, nWidth, nHeight);
    return 0;
}

static void SDL2_Harness_DestroyWindow(void) {
    // SDL2_GL_DeleteContext(context);
    ImGuiManager_Shutdown();
    if (window != NULL) {
        SDL2_DestroyWindow(window);
    }
    SDL2_Quit();
    window = NULL;
}

// Checks whether the `flag_check` is the only modifier applied.
// e.g. is_only_modifier(event.key.keysym.mod, KMOD_ALT) returns true when only the ALT key was pressed
static int is_only_key_modifier(int modifier_flags, int flag_check) {
    return (modifier_flags & flag_check) && (modifier_flags & (KMOD_CTRL | KMOD_SHIFT | KMOD_ALT | KMOD_GUI)) == (modifier_flags & flag_check);
}

static void SDL2_Harness_ProcessWindowMessages(void) {
    static int viewport_initialized = 0;
    SDL_Event event;

    // Initialize viewport on first frame, once gBack_screen exists and we have window dimensions
    if (!viewport_initialized && gBack_screen != NULL && gHarness_window_width > 0 && gHarness_window_height > 0) {
        if (gHarness_window_width != gBack_screen->width || gHarness_window_height != gBack_screen->height) {
            calculate_viewport(gHarness_window_width, gHarness_window_height);
        }
        viewport_initialized = 1;
    }

    while (SDL2_PollEvent(&event)) {
        ImGuiManager_ProcessEvent(&event);

        if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_F1 && event.key.windowID == SDL2_GetWindowID(window)) {
            ImGuiManager_SetVisible(!ImGuiManager_IsVisible());
            continue;
        }

        if (ImGuiManager_WantsCaptureKeyboard() || ImGuiManager_WantsCaptureMouse()) {
            if (event.type == SDL_KEYDOWN || event.type == SDL_KEYUP) {
                continue;
            }
        }

        switch (event.type) {
        case SDL_KEYDOWN:
        case SDL_KEYUP:
            if (event.key.windowID != SDL2_GetWindowID(window)) {
                continue;
            }
            if (event.key.keysym.sym == SDLK_RETURN) {
                if (event.key.type == SDL_KEYDOWN) {
                    if ((event.key.keysym.mod & (KMOD_CTRL | KMOD_SHIFT | KMOD_ALT | KMOD_GUI))) {
                        // Ignore keydown of RETURN when used together with some modifier
                        return;
                    }
                } else if (event.key.type == SDL_KEYUP) {
                    if (is_only_key_modifier(event.key.keysym.mod, KMOD_ALT)) {
                        SDL2_SetWindowFullscreen(window, (SDL2_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN_DESKTOP) ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
                    }
                }
            }

            // Map incoming SDL scancode to PC scan code as used by game code
            int dethrace_scancode = sdl_scancode_map[event.key.keysym.scancode];
            if (dethrace_scancode == 0) {
                LOG_WARN3("unexpected scan code %s (%d)", SDL2_GetScancodeName(event.key.keysym.scancode), event.key.keysym.scancode);
                return;
            }

            if (event.type == SDL_KEYDOWN) {
                key_state[dethrace_scancode >> 5] |= (1 << (dethrace_scancode & 0x1F));
            } else {
                key_state[dethrace_scancode >> 5] &= ~(1 << (dethrace_scancode & 0x1F));
            }
            gKeyHandler_func();
            break;

        case SDL_WINDOWEVENT:
            if (event.window.event == SDL_WINDOWEVENT_RESIZED) {
                gHarness_window_width = event.window.data1;
                gHarness_window_height = event.window.data2;
                calculate_viewport(event.window.data1, event.window.data2);
            }
            break;

        case SDL_QUIT:
            QuitGame();
        }
    }
}

static void SDL2_Harness_SetKeyHandler(void (*handler_func)(void)) {
    gKeyHandler_func = handler_func;
}

static void SDL2_Harness_GetKeyboardState(br_uint_32* buffer) {
    memcpy(buffer, key_state, sizeof(key_state));
}

static int SDL2_Harness_GetMouseButtons(int* pButton1, int* pButton2) {
    if (SDL2_GetMouseFocus() != window) {
        *pButton1 = 0;
        *pButton2 = 0;
        return 0;
    }
    int state = SDL2_GetMouseState(NULL, NULL);
    *pButton1 = state & SDL_BUTTON_LMASK;
    *pButton2 = state & SDL_BUTTON_RMASK;
    return 0;
}

static int SDL2_Harness_GetMousePosition(int* pX, int* pY) {
    int window_width, window_height;
    float lX, lY;

    if (SDL2_GetMouseFocus() != window) {
        return 0;
    }
    SDL2_GetWindowSize(window, &window_width, &window_height);

    SDL2_GetMouseState(pX, pY);
    if (renderer != NULL) {
        // software renderer
        SDL2_RenderWindowToLogical(renderer, *pX, *pY, &lX, &lY);
    } else {
        // hardware renderer
        // handle case where window is stretched larger than the pixel size
        lX = *pX * (640.0f / window_width);
        lY = *pY * (480.0f / window_height);
    }
    *pX = (int)lX;
    *pY = (int)lY;
    return 0;
}

static void limit_fps(void) {
    Uint32 now = SDL2_GetTicks();
    if (last_frame_time != 0) {
        unsigned int frame_time = now - last_frame_time;
        last_frame_time = now;
        if (frame_time < 100) {
            int sleep_time = (1000 / harness_game_config.fps) - frame_time;
            if (sleep_time > 5) {
                gHarness_platform.Sleep(sleep_time);
            }
        }
    }
    last_frame_time = SDL2_GetTicks();
}

static int SDL2_Harness_ShowErrorMessage(char* title, char* message) {
    fprintf(stderr, "%s", message);
    SDL2_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, title, message, window);
    return 0;
}

static void toggle_fullscreen_sdl2(void) {
    SDL2_SetWindowFullscreen(window, (SDL2_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN_DESKTOP) ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
}

static int get_freeze_timer(void) { return harness_game_config.freeze_timer; }
static void set_freeze_timer(int v) { harness_game_config.freeze_timer = v; }
static int get_freeze_powerups(void) { return gFreeze_powerups; }
static void set_freeze_powerups(int v) { gFreeze_powerups = v; }
static int get_cheating(void) { return gI_am_cheating != 0; }
static void set_cheating(int v) { gI_am_cheating = v ? 0xa11ee75d : 0; }
static void toggle_timer_freeze_sdl2(void) { ToggleTimerFreeze(); }
static void toggle_powerup_freeze_sdl2(void) { TogglePowerupFreeze(); }
static void toggle_invulnerability_sdl2(void) { ToggleInvulnerability(); }
static void toggle_flying_sdl2(void) { ToggleFlying(); }
static void total_repair_sdl2(void) { TotalRepair(); }
static void increment_lap_sdl2(void) { IncrementLap(); }
static void more_time_sdl2(void) { MoreTime(); }
static void earn_dosh_sdl2(void) { EarnDosh(); }
static void lose_dosh_sdl2(void) { LoseDosh(); }
static void give_powerup_sdl2(int n) { GetPowerup(n); }
static int get_game_type_sdl2(void) { return harness_game_info.mode; }
static ImGuiManager_Callbacks imgui_callbacks = {
    .toggle_fullscreen = toggle_fullscreen_sdl2,
    .quit_game = QuitGame,
    .get_freeze_timer = get_freeze_timer,
    .set_freeze_timer = set_freeze_timer,
    .get_freeze_powerups = get_freeze_powerups,
    .set_freeze_powerups = set_freeze_powerups,
    .get_cheating = get_cheating,
    .set_cheating = set_cheating,
    .toggle_timer_freeze = toggle_timer_freeze_sdl2,
    .toggle_powerup_freeze = toggle_powerup_freeze_sdl2,
    .toggle_invulnerability = toggle_invulnerability_sdl2,
    .toggle_flying = toggle_flying_sdl2,
    .total_repair = total_repair_sdl2,
    .increment_lap = increment_lap_sdl2,
    .more_time = more_time_sdl2,
    .earn_dosh = earn_dosh_sdl2,
    .lose_dosh = lose_dosh_sdl2,
    .give_powerup = give_powerup_sdl2,
    .get_game_type = get_game_type_sdl2,
};

/*
 * Vulkan callbacks
 */
static void* vkGetInstanceExtensions_fn;
static void* vkCreateSurface_fn;

static void LoadVulkanSymbols(void) {
#ifdef DETHRACE_SDL_DYNAMIC
    vkGetInstanceExtensions_fn = Harness_LoadFunction(sdl2_so, "SDL_Vulkan_GetInstanceExtensions");
    vkCreateSurface_fn = Harness_LoadFunction(sdl2_so, "SDL_Vulkan_CreateSurface");
#else
    vkGetInstanceExtensions_fn = SDL_Vulkan_GetInstanceExtensions;
    vkCreateSurface_fn = SDL_Vulkan_CreateSurface;
#endif
}

static const char** SDL2_Harness_VK_GetInstanceExtensions(uint32_t* count) {
    if (!vkGetInstanceExtensions_fn)
        return NULL;
    static const char* exts[16];
    memset(exts, 0, sizeof(exts));
    {   int (*fn)(SDL_Window*, unsigned int*, const char**) = (void*)vkGetInstanceExtensions_fn;
        if (!fn(window, count, NULL))
            return NULL;
        fn(window, count, exts);
    }
    return exts;
}

static void* SDL2_Harness_VK_CreateSurface(void* instance) {
    if (!vkCreateSurface_fn)
        return NULL;
    void* surface = NULL;
    {   int (*fn)(SDL_Window*, void*, void**) = (void*)vkCreateSurface_fn;
        if (fn(window, instance, &surface))
            return surface;
    }
    return NULL;
}

static void SDL2_Harness_CreateWindow(const char* title, int width, int height, tHarness_window_type window_type) {
    int window_width, window_height;
    Uint32 extra_window_flags;

    render_width = width;
    render_height = height;

    window_width = width;
    window_height = height;

    // special case lores and make a bigger window
    if (width == 320 && height == 200) {
        window_width = 640;
        window_height = 480;
    }

    if (SDL2_Init(SDL_INIT_VIDEO) != 0) {
        LOG_PANIC2("SDL_INIT_VIDEO error: %s", SDL2_GetError());
    }

    extra_window_flags = SDL_WINDOW_RESIZABLE;
    if (harness_game_config.start_full_screen) {
        extra_window_flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
    }

    if (window_type == eWindow_type_vulkan) {

        window = SDL2_CreateWindow(title,
            SDL_WINDOWPOS_CENTERED,
            SDL_WINDOWPOS_CENTERED,
            window_width, window_height,
            extra_window_flags | SDL_WINDOW_VULKAN);

        if (window == NULL) {
            LOG_PANIC2("Failed to create Vulkan window: %s", SDL2_GetError());
        }

    } else if (window_type == eWindow_type_opengl) {

        window = SDL2_CreateWindow(title,
            SDL_WINDOWPOS_CENTERED,
            SDL_WINDOWPOS_CENTERED,
            window_width, window_height,
            extra_window_flags | SDL_WINDOW_OPENGL);

        if (window == NULL) {
            LOG_PANIC2("Failed to create window: %s", SDL2_GetError());
        }

        SDL2_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        SDL2_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL2_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
        gl_context = SDL2_GL_CreateContext(window);

        if (gl_context == NULL) {
            LOG_WARN2("Failed to create OpenGL core profile: %s. Trying OpenGLES...", SDL2_GetError());
            SDL2_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
            SDL2_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
            SDL2_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
            gl_context = SDL2_GL_CreateContext(window);
        }
        if (gl_context == NULL) {
            LOG_PANIC2("Failed to create OpenGL context: %s", SDL2_GetError());
        }
        SDL2_GL_SetSwapInterval(1);

    } else {
        window = SDL2_CreateWindow(title,
            SDL_WINDOWPOS_CENTERED,
            SDL_WINDOWPOS_CENTERED,
            window_width, window_height,
            extra_window_flags);
        if (window == NULL) {
            LOG_PANIC2("Failed to create window: %s", SDL2_GetError());
        }

        renderer = SDL2_CreateRenderer(window, -1, SDL_RENDERER_PRESENTVSYNC);
        if (renderer == NULL) {
            LOG_PANIC2("Failed to create renderer: %s", SDL2_GetError());
        }
        SDL2_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
        SDL2_RenderSetLogicalSize(renderer, render_width, render_height);

        screen_texture = SDL2_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, width, height);
        if (screen_texture == NULL) {
            SDL_RendererInfo info;
            SDL2_GetRendererInfo(renderer, &info);
            for (Uint32 i = 0; i < info.num_texture_formats; i++) {
                LOG_INFO2("%s\n", SDL2_GetPixelFormatName(info.texture_formats[i]));
            }
            LOG_PANIC2("Failed to create screen_texture: %s", SDL2_GetError());
        }
    }

    SDL2_ShowCursor(SDL_DISABLE);

    ImGuiManager_Init(window,
        window_type == eWindow_type_vulkan ? NULL : renderer,
        window_type == eWindow_type_opengl, &imgui_callbacks);

    SDL2_GetWindowSize(window, &gHarness_window_width, &gHarness_window_height);
    viewport.x = 0;
    viewport.y = 0;
    viewport.scale_x = 1;
    viewport.scale_y = 1;
}

static void SDL2_Harness_Swap(br_pixelmap* back_buffer) {
    int i;
    int dest_pitch;
    uint8_t* src_pixels;
    br_uint_32* dest_pixels;

    SDL2_Harness_ProcessWindowMessages();

    if (gl_context != NULL) {
        ImGuiManager_Render();
        SDL2_GL_SwapWindow(window);
    } else if (SDL2_GetWindowFlags(window) & SDL_WINDOW_VULKAN) {
        // VK handles its own presentation and ImGui via external callback
    } else {
        src_pixels = back_buffer->pixels;

        SDL2_LockTexture(screen_texture, NULL, (void**)&dest_pixels, &dest_pitch);
        for (i = 0; i < back_buffer->height * back_buffer->width; i++) {
            *dest_pixels = converted_palette[*src_pixels];
            dest_pixels++;
            src_pixels++;
        }
        SDL2_UnlockTexture(screen_texture);
        SDL2_RenderClear(renderer);
        if (gHarness_sw_widescreen) {
            int win_w, win_h;
            SDL2_GetWindowSize(window, &win_w, &win_h);
            SDL2_RenderSetLogicalSize(renderer, win_w, win_h);
            SDL2_RenderCopy(renderer, screen_texture, NULL, NULL);
            SDL2_RenderSetLogicalSize(renderer, render_width, render_height);
        } else {
            SDL2_RenderCopy(renderer, screen_texture, NULL, NULL);
        }
        ImGuiManager_Render();
        SDL2_RenderPresent(renderer);
        last_screen_src = back_buffer;
    }

    if (!ImGuiManager_IsVisible()) {
        SDL2_ShowCursor(SDL_DISABLE);
    }

    if (harness_game_config.fps != 0) {
        limit_fps();
    }
}

static void SDL2_Harness_PaletteChanged(br_colour entries[256]) {
    int i;
    for (i = 0; i < 256; i++) {
        converted_palette[i] = (0xffu << 24 | BR_RED(entries[i]) << 16 | BR_GRN(entries[i]) << 8 | BR_BLU(entries[i]));
    }
    if (last_screen_src != NULL) {
        SDL2_Harness_Swap(last_screen_src);
    }
}

static void SDL2_Harness_GetViewport(int* x, int* y, float* width_multipler, float* height_multiplier) {
    if (gHarness_gl_viewport_override > 0 && gBack_screen != NULL) {
        *x = 0;
        *y = viewport.y;
        *width_multipler = (float)gHarness_gl_viewport_override / gBack_screen->width;
        *height_multiplier = viewport.scale_y;
    } else {
        *x = viewport.x;
        *y = viewport.y;
        *width_multipler = viewport.scale_x;
        *height_multiplier = viewport.scale_y;
    }
}

static int SDL2_Harness_Platform_Init(tHarness_platform* platform) {
    if (SDL2_LoadSymbols() != 0) {
        return 1;
    }
    platform->ProcessWindowMessages = SDL2_Harness_ProcessWindowMessages;
    platform->Sleep = SDL2_Delay;
    platform->GetTicks = SDL2_GetTicks;
    platform->ShowCursor = SDL2_ShowCursor;
    platform->SetWindowPos = SDL2_Harness_SetWindowPos;
    platform->DestroyWindow = SDL2_Harness_DestroyWindow;
    platform->SetKeyHandler = SDL2_Harness_SetKeyHandler;
    platform->GetKeyboardState = SDL2_Harness_GetKeyboardState;
    platform->GetMousePosition = SDL2_Harness_GetMousePosition;
    platform->GetMouseButtons = SDL2_Harness_GetMouseButtons;
    platform->ShowErrorMessage = SDL2_Harness_ShowErrorMessage;

    platform->CreateWindow_ = SDL2_Harness_CreateWindow;
    platform->Swap = SDL2_Harness_Swap;
    platform->PaletteChanged = SDL2_Harness_PaletteChanged;
    platform->GL_GetProcAddress = SDL2_GL_GetProcAddress;
    platform->GetViewport = SDL2_Harness_GetViewport;

    LoadVulkanSymbols();
    platform->VK_CreateSurface = SDL2_Harness_VK_CreateSurface;
    platform->VK_GetInstanceExtensions = SDL2_Harness_VK_GetInstanceExtensions;
    return 0;
};

const tPlatform_bootstrap SDL2_bootstrap = {
    "sdl2",
    "SDL2 video backend (libsdl.org)",
    ePlatform_cap_software | ePlatform_cap_opengl | ePlatform_cap_vulkan,
    SDL2_Harness_Platform_Init,
};
