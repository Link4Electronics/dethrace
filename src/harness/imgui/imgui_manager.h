#ifndef IMGUI_MANAGER_H
#define IMGUI_MANAGER_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    void (*toggle_fullscreen)(void);
    void (*quit_game)(void);
    int (*get_freeze_timer)(void);
    void (*set_freeze_timer)(int v);
    int (*get_freeze_powerups)(void);
    void (*set_freeze_powerups)(int v);
    int (*get_cheating)(void);
    void (*set_cheating)(int v);
    void (*toggle_timer_freeze)(void);
    void (*toggle_powerup_freeze)(void);
    void (*toggle_invulnerability)(void);
    void (*toggle_flying)(void);
    void (*total_repair)(void);
    void (*increment_lap)(void);
    void (*more_time)(void);
    void (*earn_dosh)(void);
    void (*lose_dosh)(void);
    void (*give_powerup)(int n);
    int (*get_game_type)(void);
} ImGuiManager_Callbacks;

void ImGuiManager_Init(void* window, void* renderer, int is_opengl, ImGuiManager_Callbacks* callbacks);
void ImGuiManager_ProcessEvent(const void* event);
void ImGuiManager_Render(void);
void ImGuiManager_Shutdown(void);
int ImGuiManager_WantsCaptureMouse(void);
int ImGuiManager_WantsCaptureKeyboard(void);
int ImGuiManager_IsVisible(void);
void ImGuiManager_SetVisible(int visible);

#ifdef __cplusplus
}
#endif

#endif
