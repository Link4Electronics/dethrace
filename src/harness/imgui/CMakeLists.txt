include(FetchContent)

FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.91.8
)
set(FETCHCONTENT_QUIET OFF)
FetchContent_MakeAvailable(imgui)

set(IMGUI_SRC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
)

set(IMGUI_BACKEND_DIR ${imgui_SOURCE_DIR}/backends)

if(DETHRACE_PLATFORM_SDL3)
    set(IMGUI_SDL_BACKEND_SRC ${IMGUI_BACKEND_DIR}/imgui_impl_sdl3.cpp)
    set(IMGUI_SDLRENDERER_BACKEND_SRC ${IMGUI_BACKEND_DIR}/imgui_impl_sdlrenderer3.cpp)
    set(IMGUI_SDL_RENDERER_TYPE sdl3)
else()
    set(IMGUI_SDL_BACKEND_SRC ${IMGUI_BACKEND_DIR}/imgui_impl_sdl2.cpp)
    set(IMGUI_SDLRENDERER_BACKEND_SRC ${IMGUI_BACKEND_DIR}/imgui_impl_sdlrenderer2.cpp)
    set(IMGUI_SDL_RENDERER_TYPE sdl2)
endif()

set(IMGUI_OPENGL3_BACKEND_SRC ${IMGUI_BACKEND_DIR}/imgui_impl_opengl3.cpp)

add_library(imgui_manager STATIC
    ${IMGUI_SRC}
    ${IMGUI_SDL_BACKEND_SRC}
    ${IMGUI_OPENGL3_BACKEND_SRC}
    ${IMGUI_SDLRENDERER_BACKEND_SRC}
    imgui_manager.cpp
)

target_include_directories(imgui_manager
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE
    ${imgui_SOURCE_DIR}
    ${IMGUI_BACKEND_DIR}
    ${CMAKE_SOURCE_DIR}/lib/BRender-v1.3.2/drivers/glrend/include
)

target_compile_definitions(imgui_manager PRIVATE
    IMGUI_IMPL_OPENGL_LOADER_GLAD
    IMGUI_USER_CONFIG="imconfig_bigendian.h"
)

if(DETHRACE_PLATFORM_SDL3)
    target_compile_definitions(imgui_manager PRIVATE DETHRACE_PLATFORM_SDL3)
    target_link_libraries(imgui_manager PRIVATE SDL3::SDL3)
else()
    target_compile_definitions(imgui_manager PRIVATE DETHRACE_PLATFORM_SDL2)
    target_link_libraries(imgui_manager PRIVATE SDL2::SDL2)
endif()
