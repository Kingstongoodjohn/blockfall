cmake_minimum_required(VERSION 3.16)
project(Blockfall LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Platform-independent game logic (also built into the test program).
set(GAME_SOURCES
    src/game/ai.cpp
    src/game/atlas.cpp
    src/game/building.cpp
    src/game/editing.cpp
    src/game/frontend.cpp
    src/game/game.cpp
    src/game/hud.cpp
    src/game/keybinds.cpp
    src/game/loot.cpp
    src/game/map.cpp
    src/game/player.cpp
    src/game/scene.cpp
    src/game/settings.cpp
    src/game/skins.cpp
    src/game/account.cpp
    src/game/model.cpp
    src/game/charanim.cpp
    src/game/weapons.cpp
    src/game/world.cpp
)
add_library(blockfall_core STATIC ${GAME_SOURCES})
target_include_directories(blockfall_core PUBLIC src)

if(MSVC)
    target_compile_options(blockfall_core PRIVATE /W4 /permissive-)
else()
    target_compile_options(blockfall_core PRIVATE -Wall -Wextra)
endif()

# Logic tests (run on any OS): build with -DBLOCKFALL_TESTS=ON, then run blockfall_tests.
option(BLOCKFALL_TESTS "Build the logic test program" OFF)
if(BLOCKFALL_TESTS)
    add_executable(blockfall_tests tests/tests.cpp)
    target_link_libraries(blockfall_tests PRIVATE blockfall_core)
endif()

if(WIN32)
    add_executable(Blockfall WIN32
        src/platform/main_win32.cpp
        src/platform/input_win32.cpp
        src/platform/renderer_d3d11.cpp
    )
    target_link_libraries(Blockfall PRIVATE blockfall_core d3d11 dxgi d3dcompiler gdi32)
    if(MSVC)
        target_compile_options(Blockfall PRIVATE /W4 /permissive-)
        set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT Blockfall)
    else()
        target_compile_options(Blockfall PRIVATE -Wall -Wextra)
        # MinGW: no extra runtime DLLs needed next to the .exe.
        target_link_options(Blockfall PRIVATE -static -static-libgcc -static-libstdc++)
    endif()
endif()
