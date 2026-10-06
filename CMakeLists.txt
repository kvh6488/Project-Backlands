cmake_minimum_required(VERSION 3.14)
project(ProjectBacklands CXX)

# Fix for CMake 3.30+ crashing on raylib 5.0's older cmake_minimum_required(2.8)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE INTERNAL "")

if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()
if(POLICY CMP0169)
    cmake_policy(SET CMP0169 OLD)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# 1. Fetch Raylib
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    raylib
    URL https://github.com/raysan5/raylib/archive/refs/tags/5.0.tar.gz
)
FetchContent_MakeAvailable(raylib)

# 2. Fetch ImGui (ImGui does not have a CMakeLists.txt, so we populate it manually)
FetchContent_Declare(
    imgui
    URL https://github.com/ocornut/imgui/archive/refs/heads/docking.zip
)
FetchContent_GetProperties(imgui)
if(NOT imgui_POPULATED)
    FetchContent_Populate(imgui)
endif()

# 3. Fetch rlImGui (Bridge between Raylib and ImGui)
FetchContent_Declare(
    rlimhui
    GIT_REPOSITORY https://github.com/raylib-extras/rlImGui.git
    GIT_TAG main
)
FetchContent_GetProperties(rlimhui)
if(NOT rlimhui_POPULATED)
    FetchContent_Populate(rlimhui)
endif()

# Copy assets to the build directory so the executable can find them
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/assets DESTINATION ${CMAKE_CURRENT_BINARY_DIR})

# ------------------------------------------------------------------------------
# Shared source lists
# ------------------------------------------------------------------------------
# Both executables compile the same game code; only their entry points differ
# (main.cpp for the game, gtest_main for the tests). Listing the sources once
# means adding a new .cpp is a single edit - the old duplicated lists silently
# broke the test link whenever only one of them was updated.
set(BACKLANDS_GAME_SOURCES
    src/core/application.cpp
    src/world/maze.cpp
    src/world/item_spawner.cpp
    src/world/generators/bsp_generator.cpp
    src/world/generators/loop_generator.cpp
    src/world/generators/prims_generator.cpp
    src/world/generators/tunnel_borer.cpp
    src/world/generators/generator_utils.cpp
    src/world/generators/island_generator.cpp
    src/world/generators/poisson.cpp
    src/world/island.cpp
    src/world/climate.cpp
    src/world/overworld.cpp
    src/entities/player.cpp
    src/items/item_database.cpp
    src/items/crafting_system.cpp
    src/render/maze_renderer.cpp
    src/render/player_renderer.cpp
    src/render/item_renderer.cpp
    src/render/overworld_renderer.cpp
    src/render/step_effects.cpp
    src/states/maze_state.cpp
    src/states/overworld_state.cpp
    src/ui/ui_manager.cpp
)

# Developer tooling: the ImGui panel, the seed table and the console logger.
# Kept as its own list because a release build drops it wholesale - the panel
# is currently gated only at runtime by --dev, so the code still ships.
set(BACKLANDS_DEV_SOURCES
    src/dev/debug_overlay.cpp
)

# ImGui and rlImGui are compiled straight into each target - neither ships a
# CMake target of its own.
set(BACKLANDS_VENDOR_SOURCES
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${rlimhui_SOURCE_DIR}/rlImGui.cpp
)

set(BACKLANDS_INCLUDE_DIRS
    src
    ${imgui_SOURCE_DIR}
    ${rlimhui_SOURCE_DIR}
)

# 4. Define our executable
add_executable(Backlands
    src/core/main.cpp
    ${BACKLANDS_GAME_SOURCES}
    ${BACKLANDS_DEV_SOURCES}
    ${BACKLANDS_VENDOR_SOURCES}
)

# Tell the compiler where to find the header files (.h files)
target_include_directories(Backlands PRIVATE ${BACKLANDS_INCLUDE_DIRS})

# Link Raylib to our executable
target_link_libraries(Backlands PRIVATE raylib)

# Raylib recently renamed IsTextureReady to IsTextureValid. rlImGui expects the new name.
# This define maps it back to the old name so it compiles with Raylib 5.0.
target_compile_definitions(Backlands PRIVATE IsTextureValid=IsTextureReady)

# ==============================================================================
# 5. TESTING SUITE (Google Test)
# ==============================================================================
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/03597a01ee50ed33e9dfd640b249b4be3799d395.zip
)
# For Windows: Prevent GTest from overriding the parent project's MSVC runtime
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# Enable testing globally so VS Code's "Testing" sidebar works
enable_testing()

# Create a brand new, completely separate executable just for running tests.
# Notice we do NOT include main.cpp here, because gtest_main provides its own main()!
add_executable(BacklandsTests
    tests/test_maze.cpp
    tests/test_magic_book.cpp
    tests/test_inventory.cpp
    tests/test_harness.cpp
    tests/test_overworld.cpp
    tests/test_seasons.cpp
    ${BACKLANDS_GAME_SOURCES}
    ${BACKLANDS_DEV_SOURCES}
    ${BACKLANDS_VENDOR_SOURCES}
)

# Tell the test executable where to find our source header files
target_include_directories(BacklandsTests PRIVATE ${BACKLANDS_INCLUDE_DIRS})

# Link Google Test and Raylib to the test executable
target_link_libraries(BacklandsTests PRIVATE
    gtest_main
    raylib
)

# Raylib recently renamed IsTextureReady to IsTextureValid. rlImGui expects the new name.
target_compile_definitions(BacklandsTests PRIVATE IsTextureValid=IsTextureReady)

# Register the executable with CTest so VS Code can magically discover it
include(GoogleTest)
gtest_discover_tests(BacklandsTests)
