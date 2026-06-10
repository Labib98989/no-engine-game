# No Engine Game — build recipe per docs/research.md §1.3 / docs/build-plan.md §2.
# Four front-ends onto one pure core:
#   neg_sim       STATIC   deterministic core. Zero SDL, zero float.
#   neg           WIN32    the game (SDL3 + SDL3_image + nlohmann_json + miniaudio)
#   neg_tests     CONSOLE  doctest unit tests   — links neg_sim only
#   neg_headless  CONSOLE  scripted runner      — links neg_sim only
# neg_tests/neg_headless linking only neg_sim is what physically enforces the
# determinism boundary: an SDL include or float in src/sim/ becomes a build error.
cmake_minimum_required(VERSION 3.25)
project(neg C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/$<CONFIG>)

option(ENABLE_ASAN "Build with AddressSanitizer" OFF)

find_package(SDL3 CONFIG REQUIRED)
find_package(SDL3_image CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

# ---- house compile flags (our code only; third_party stays untouched) -------
add_library(neg_flags INTERFACE)
if(MSVC)
    target_compile_options(neg_flags INTERFACE
        /W4 /permissive- /Zc:preprocessor /utf-8 /fp:strict
        $<$<CONFIG:Debug>:/Od /Zi>
        $<$<CONFIG:Release>:/O2>)
    if(ENABLE_ASAN)
        target_compile_options(neg_flags INTERFACE /fsanitize=address)
    endif()
endif()

# ---- third_party -------------------------------------------------------------
add_library(miniaudio STATIC third_party/miniaudio/miniaudio.c)
target_include_directories(miniaudio PUBLIC third_party/miniaudio)

# ---- neg_sim: the deterministic core ------------------------------------------
add_library(neg_sim STATIC
    src/sim/input_commit.cpp
    src/sim/fighter.cpp
    src/sim/clash.cpp
    src/sim/combo.cpp
    src/sim/chardata.cpp
    src/sim/simulation.cpp
)
target_include_directories(neg_sim PUBLIC src)
target_link_libraries(neg_sim PRIVATE neg_flags)

# ---- neg: the game -------------------------------------------------------------
add_executable(neg WIN32
    src/main.cpp
    src/platform/app.cpp
    src/platform/input.cpp
    src/render/font5x7.cpp
    src/render/draw.cpp
    src/data/loader.cpp
    src/audio/audio.cpp
)
target_link_libraries(neg PRIVATE
    neg_flags neg_sim SDL3::SDL3 SDL3_image::SDL3_image
    nlohmann_json::nlohmann_json miniaudio)

# Stage SDL3.dll (and friends) + assets next to the exe.
add_custom_command(TARGET neg POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_RUNTIME_DLLS:neg> $<TARGET_FILE_DIR:neg>
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/assets $<TARGET_FILE_DIR:neg>/assets
    COMMAND_EXPAND_LISTS)

# ---- neg_tests: doctest, links neg_sim only -------------------------------------
add_executable(neg_tests
    tests/test_main.cpp
    tests/test_fixed.cpp
    tests/test_beatclock.cpp
    tests/test_clash.cpp
    tests/test_combo.cpp
    tests/test_sim.cpp
)
target_include_directories(neg_tests PRIVATE third_party/doctest)
target_link_libraries(neg_tests PRIVATE neg_flags neg_sim)

# ---- neg_headless: scripted runner, links neg_sim only ---------------------------
add_executable(neg_headless tools/headless.cpp)
target_link_libraries(neg_headless PRIVATE neg_flags neg_sim)

# ---- test harness -----------------------------------------------------------------
enable_testing()
add_test(NAME unit COMMAND neg_tests)
add_test(NAME headless_example
    COMMAND neg_headless ${CMAKE_SOURCE_DIR}/replays/script_example.txt --verify)
add_test(NAME headless_combo_cap
    COMMAND neg_headless ${CMAKE_SOURCE_DIR}/replays/script_combo_cap.txt --verify)
add_test(NAME headless_steal
    COMMAND neg_headless ${CMAKE_SOURCE_DIR}/replays/script_steal.txt --verify)
