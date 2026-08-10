#include "imgui_manager.h"

#include "imgui.h"
#include "backends/imgui_impl_opengl3.h"
#include "backends/imgui_impl_vulkan.h"

#if defined(DETHRACE_PLATFORM_SDL3)
#include <SDL3/SDL.h>
#include "backends/imgui_impl_sdl3.h"
#include "backends/imgui_impl_sdlrenderer3.h"
#else
#include <SDL.h>
#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_sdlrenderer2.h"
#endif

static SDL_Window* g_window = NULL;
static SDL_Renderer* g_renderer = NULL;
static int g_is_opengl = 0;
static int g_is_vulkan = 0;
static int g_visible = 1;
static int g_initialized = 0;
static int g_renderer_initialized = 0;
static VkDescriptorPool g_imgui_vk_pool = VK_NULL_HANDLE;

static void DrawMenuBar(void);
extern int g_wireframe_mode;
extern int gWidescreen_mode;
static ImGuiManager_Callbacks g_callbacks;

static bool g_cheat_freeze_time = false;
static bool g_cheat_freeze_powerups = false;
static bool g_cheat_invulnerability = false;
static bool g_cheat_flying = false;

static void InitRendererBackend(void)
{
    if (g_renderer_initialized)
        return;

    if (g_is_opengl)
        ImGui_ImplOpenGL3_Init("#version 140");
    else if (g_is_vulkan)
        ; /* Vulkan backend initialized in ImGuiManager_InitSDL3 */
#if defined(DETHRACE_PLATFORM_SDL3)
    else if (g_renderer)
        ImGui_ImplSDLRenderer3_Init(g_renderer);
#else
    else if (g_renderer)
        ImGui_ImplSDLRenderer2_Init(g_renderer);
#endif

    g_renderer_initialized = 1;
}

// ImGui has no SDL3-GPU backend; without an OpenGL context or SDL_Renderer there
// is nothing to draw with, so the overlay is disabled in sdl3rend mode.
static bool HasRenderBackend(void)
{
    return g_is_opengl || g_is_vulkan || g_renderer != NULL;
}

void ImGuiManager_InitSDL3(void* instance, void* physical_device, void* device,
    void* queue, uint32_t queue_family, void* render_pass,
    uint32_t min_image_count, uint32_t image_count)
{
    if (!g_initialized)
        return;

    g_is_vulkan = 1;
    VkDevice sdl3_device = (VkDevice)device;

    VkDescriptorPoolSize pool_sizes[] = {
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 10 },
    };
    VkDescriptorPoolCreateInfo pool_info = {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pool_info.maxSets = 10;
    pool_info.poolSizeCount = 1;
    pool_info.pPoolSizes = pool_sizes;
    vkCreateDescriptorPool(sdl3_device, &pool_info, NULL, &g_imgui_vk_pool);

    ImGui_ImplVulkan_InitInfo init_info = {};
    init_info.Instance = (VkInstance)instance;
    init_info.PhysicalDevice = (VkPhysicalDevice)physical_device;
    init_info.Device = sdl3_device;
    init_info.QueueFamily = queue_family;
    init_info.Queue = (VkQueue)queue;
    init_info.DescriptorPool = g_imgui_vk_pool;
    init_info.MinImageCount = min_image_count;
    init_info.ImageCount = image_count;
    init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    init_info.RenderPass = (VkRenderPass)render_pass;
    init_info.Subpass = 0;
    ImGui_ImplVulkan_Init(&init_info);

    ImGui_ImplVulkan_CreateFontsTexture();
}

void ImGuiManager_RenderSDL3(void* cmd_buffer)
{
    if (!g_initialized)
        return;
    ImDrawData* draw_data = ImGui::GetDrawData();
    if (!draw_data)
        return;
    VkCommandBuffer cmd = (VkCommandBuffer)cmd_buffer;
    ImGui_ImplVulkan_RenderDrawData(draw_data, cmd);
}

void ImGuiManager_NewFrame(void)
{
    if (!g_initialized || !HasRenderBackend())
        return;

    InitRendererBackend();

#if defined(DETHRACE_PLATFORM_SDL3)
    ImGui_ImplSDL3_NewFrame();
#else
    ImGui_ImplSDL2_NewFrame();
#endif

    if (g_is_opengl)
        ImGui_ImplOpenGL3_NewFrame();
    else if (g_is_vulkan)
        ImGui_ImplVulkan_NewFrame();
#if defined(DETHRACE_PLATFORM_SDL3)
    else
        ImGui_ImplSDLRenderer3_NewFrame();
#else
    else
        ImGui_ImplSDLRenderer2_NewFrame();
#endif

    ImGui::NewFrame();
    DrawMenuBar();
    ImGui::Render();
}

static void DrawMenuBar(void)
{
    if (!g_visible)
        return;

    if (ImGui::BeginMainMenuBar())
    {
        if (ImGui::BeginMenu("Deth"))
        {
            if (ImGui::MenuItem("Hide Menu Bar", "F1"))
                g_visible = 0;
            if (ImGui::MenuItem("Toggle Fullscreen", "Alt+Enter"))
            {
                if (g_callbacks.toggle_fullscreen)
                    g_callbacks.toggle_fullscreen();
            }
            if (ImGui::MenuItem("Quit"))
            {
                if (g_callbacks.quit_game)
                    g_callbacks.quit_game();
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Settings"))
        {
            if (ImGui::BeginMenu("Audio"))
            {
                ImGui::Text("Audio API (Needs reload)");
                if (ImGui::BeginCombo("##AApi", "SDL Audio", 0))
                {
                    ImGui::EndCombo();
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Controller"))
            {
                if (ImGui::Button("Controller Mapping")) { }
                if (ImGui::Button("Input Viewer")) { }
                if (ImGui::Button("Input Viewer Settings")) { }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Graphics"))
            {
                if (ImGui::Button("Advanced Resolution")) { }
                ImGui::BeginGroup();
                ImGui::Text("Internal Resolution: 100%%");
                ImGui::EndGroup();
                ImGui::Text("FPS: Original (20)");
                if (ImGui::Button(" - ##WiiUFPS")) { }
                ImGui::SameLine();
                if (ImGui::Button(" + ##WiiUFPS")) { }
                {
                    bool ws = gWidescreen_mode != 0;
                    if (ImGui::MenuItem("Widescreen (in-race only)", NULL, &ws))
                        gWidescreen_mode = ws ? 1 : 0;
                }
                ImGui::EndMenu();
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Enhancements"))
        {
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Cheats"))
        {
            if (ImGui::MenuItem("Freeze Timer", NULL, &g_cheat_freeze_time))
            {
                if (g_callbacks.toggle_timer_freeze)
                    g_callbacks.toggle_timer_freeze();
            }
            if (ImGui::MenuItem("Invulnerability", NULL, &g_cheat_invulnerability))
            {
                if (g_callbacks.toggle_invulnerability)
                    g_callbacks.toggle_invulnerability();
            }
            if (ImGui::MenuItem("Flying Car", NULL, &g_cheat_flying))
            {
                if (g_callbacks.toggle_flying)
                    g_callbacks.toggle_flying();
            }

            ImGui::Separator();

            if (ImGui::MenuItem("Total Repair"))
            {
                if (g_callbacks.total_repair)
                    g_callbacks.total_repair();
            }
            if (ImGui::MenuItem("Finish Lap"))
            {
                if (g_callbacks.increment_lap)
                    g_callbacks.increment_lap();
            }
            if (ImGui::MenuItem("+30 Seconds"))
            {
                if (g_callbacks.more_time)
                    g_callbacks.more_time();
            }
            if (ImGui::MenuItem("+5000 Credits"))
            {
                if (g_callbacks.earn_dosh)
                    g_callbacks.earn_dosh();
            }
            if (ImGui::MenuItem("-5000 Credits"))
            {
                if (g_callbacks.lose_dosh)
                    g_callbacks.lose_dosh();
            }

            ImGui::Separator();

            if (ImGui::BeginMenu("Give Powerup"))
            {
                if (g_callbacks.give_powerup)
                {
                    int gt = g_callbacks.get_game_type ? g_callbacks.get_game_type() : 0;
                    // Carmageddon full (Steam/US): entries match POWERUP.TXT order
                    if (gt == 1)
                    {
                        struct { const char* name; int idx; } tbl[] = {
                            {"Extra Money", 0},
                            {"Glue Peds", 2},
                            {"Giant Peds", 3},
                            {"Explosive Peds", 4},
                            {"Hot Rod", 5},
                            {"Turbo Peds", 6},
                            {"Invulnerability", 7},
                            {"Free Repairs", 8},
                            {"Instant Repair", 9},
                            {"Freeze Timer", 10},
                            {"Underwater", 11},
                            {"Time Bonus", 12},
                            {"Trash Bodywork", 13},
                            {"Mine", 14},
                            {"Freeze Opponents", 15},
                            {"Freeze Cops", 16},
                            {"Turbo Opponents", 17},
                            {"Turbo Cops", 18},
                            {"Lunar Gravity", 19},
                            {"Pinball Mode", 20},
                            {"Wall Climber", 21},
                            {"Bouncy", 22},
                            {"Jelly Suspension", 23},
                            {"Show Peds on Map", 24},
                            {"E-Bastard Ray", 25},
                            {"Greased Tires", 26},
                            {"Random Powerup", 28},
                            {"Instant Handbrake", 32},
                            {"Immortal Peds", 33},
                            {"Turbo", 34},
                            {"Mega-Turbo", 35},
                            {"Blind Pedestrians", 36},
                            {"Pedestrian Respawn", 37},
                            {"5 Free Recovery Vouchers", 38},
                            {"Granite Car", 39},
                            {"Rock Springs", 40},
                            {"Drugs", 41},
                        };
                        for (auto& e : tbl)
                        {
                            if (ImGui::MenuItem(e.name))
                                g_callbacks.give_powerup(e.idx);
                        }
                    }
                    else
                    {
                        // Fallback for other versions: basic set
                        struct { const char* name; int idx; } tbl[] = {
                            {"Extra Money", 0},
                            {"Glue Peds", 2},
                            {"Giant Peds", 3},
                            {"Explosive Peds", 4},
                            {"Hot Rod", 5},
                            {"Turbo Peds", 6},
                            {"Invulnerability", 7},
                            {"Free Repairs", 8},
                            {"Instant Repair", 9},
                            {"Freeze Timer", 10},
                            {"Underwater", 11},
                            {"Time Bonus", 12},
                        };
                        for (auto& e : tbl)
                        {
                            if (ImGui::MenuItem(e.name))
                                g_callbacks.give_powerup(e.idx);
                        }
                    }
                }
                ImGui::EndMenu();
            }
            if (ImGui::MenuItem("Freeze Powerups", NULL, &g_cheat_freeze_powerups))
            {
                if (g_callbacks.toggle_powerup_freeze)
                    g_callbacks.toggle_powerup_freeze();
            }

            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Developer Tools"))
        {
            bool wf = g_wireframe_mode != 0;
            if (ImGui::MenuItem("Wireframe", NULL, &wf))
                g_wireframe_mode = wf ? 1 : 0;
            ImGui::EndMenu();
        }

        ImGui::EndMainMenuBar();
    }
}

void ImGuiManager_Init(void* window, void* renderer, int is_opengl, ImGuiManager_Callbacks* callbacks)
{
    if (g_initialized)
        return;

    g_window = (SDL_Window*)window;
    g_renderer = (SDL_Renderer*)renderer;
    g_is_opengl = is_opengl;

    if (callbacks)
        g_callbacks = *callbacks;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = NULL;

    ImGui::StyleColorsDark();

#if defined(DETHRACE_PLATFORM_SDL3)
    if (g_is_opengl)
        ImGui_ImplSDL3_InitForOpenGL(g_window, NULL);
    else if (g_renderer)
        ImGui_ImplSDL3_InitForSDLRenderer(g_window, g_renderer);
#else
    if (g_is_opengl)
        ImGui_ImplSDL2_InitForOpenGL(g_window, NULL);
    else if (g_renderer)
        ImGui_ImplSDL2_InitForSDLRenderer(g_window, g_renderer);
#endif

    g_renderer_initialized = 0;
    g_initialized = 1;
}

void ImGuiManager_ProcessEvent(const void* event)
{
    if (!g_initialized || !HasRenderBackend())
        return;

#if defined(DETHRACE_PLATFORM_SDL3)
    ImGui_ImplSDL3_ProcessEvent((const SDL_Event*)event);
#else
    ImGui_ImplSDL2_ProcessEvent((const SDL_Event*)event);
#endif
}

void ImGuiManager_Render(void)
{
    if (!g_initialized)
        return;

    // For Vulkan, NewFrame is called separately (ImGuiManager_NewFrame),
    // and RenderVulkan is called from the VK external render callback.
    if (g_is_vulkan)
        return;

    ImGuiManager_NewFrame();

    if (g_is_opengl)
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    else
#if defined(DETHRACE_PLATFORM_SDL3)
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), g_renderer);
#else
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), g_renderer);
#endif
}

void ImGuiManager_Shutdown(void)
{
    if (!g_initialized)
        return;

    if (g_renderer_initialized)
    {
        if (g_is_opengl)
            ImGui_ImplOpenGL3_Shutdown();
        else if (g_is_vulkan)
            ImGui_ImplVulkan_Shutdown();
#if defined(DETHRACE_PLATFORM_SDL3)
        else
            ImGui_ImplSDLRenderer3_Shutdown();
#else
        else
            ImGui_ImplSDLRenderer2_Shutdown();
#endif
    }

#if defined(DETHRACE_PLATFORM_SDL3)
    ImGui_ImplSDL3_Shutdown();
#else
    ImGui_ImplSDL2_Shutdown();
#endif

    if (g_imgui_vk_pool != VK_NULL_HANDLE) {
        // We need the VkDevice to destroy the pool, but we don't store it.
        // The shutdown should happen after device destruction.
        // For simplicity, the pool leaks.
    }

    ImGui::DestroyContext();
    g_initialized = 0;
}

int ImGuiManager_WantsCaptureMouse(void)
{
    if (!g_initialized || !g_visible)
        return 0;
    ImGuiIO& io = ImGui::GetIO();
    return io.WantCaptureMouse;
}

int ImGuiManager_WantsCaptureKeyboard(void)
{
    if (!g_initialized || !g_visible)
        return 0;
    ImGuiIO& io = ImGui::GetIO();
    return io.WantCaptureKeyboard;
}

int ImGuiManager_IsVisible(void)
{
    return g_visible;
}

void ImGuiManager_SetVisible(int visible)
{
    g_visible = visible;
}
