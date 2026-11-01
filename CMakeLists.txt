cmake_minimum_required(VERSION 3.20)
project(RiffedMenu C CXX)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "GTA:SA is 32-bit. Configure with -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-i686.cmake")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

set(IMGUI_DIR   ${CMAKE_SOURCE_DIR}/external/imgui)
set(MINHOOK_DIR ${CMAKE_SOURCE_DIR}/external/minhook)

add_library(minhook STATIC
    ${MINHOOK_DIR}/src/buffer.c
    ${MINHOOK_DIR}/src/hook.c
    ${MINHOOK_DIR}/src/trampoline.c
    ${MINHOOK_DIR}/src/hde/hde32.c)
target_include_directories(minhook PUBLIC ${MINHOOK_DIR}/include)

add_library(imgui STATIC
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_dx9.cpp
    ${IMGUI_DIR}/backends/imgui_impl_win32.cpp)
target_include_directories(imgui PUBLIC ${IMGUI_DIR} ${IMGUI_DIR}/backends)
target_compile_definitions(imgui PUBLIC IMGUI_DISABLE_OBSOLETE_FUNCTIONS)

add_library(RiffedMenu SHARED
    src/main.cpp
    src/log.cpp
    src/game.cpp
    src/features.cpp
    src/hooks.cpp
    src/menu.cpp)
target_link_libraries(RiffedMenu PRIVATE imgui minhook d3d9 dinput8 dxguid dwmapi gdi32 imm32)
target_compile_options(RiffedMenu PRIVATE -Wall -Wextra -Wno-unused-parameter -Wno-cast-function-type)

# Fully static runtime -> the .asi only depends on system DLLs Wine already ships.
target_link_options(RiffedMenu PRIVATE -static -static-libgcc -static-libstdc++ -Wl,--enable-stdcall-fixup)

set_target_properties(RiffedMenu PROPERTIES PREFIX "" OUTPUT_NAME "RiffedMenu" SUFFIX ".asi")
