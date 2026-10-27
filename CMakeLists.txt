cmake_minimum_required(VERSION 3.24)
project(Ninkasi VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Link the C/C++ runtime statically on MSVC so the exe is self-contained.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

option(NINKASI_BUILD_TESTS "Build the unit test executable" ON)

include(cmake/Dependencies.cmake)
include(cmake/EmbedFile.cmake)

# Compiler settings for Ninkasi's own code (not third-party libraries).
add_library(ninkasi_options INTERFACE)
if(MSVC)
    target_compile_options(ninkasi_options INTERFACE /W4 /permissive- /utf-8)
    target_compile_definitions(ninkasi_options INTERFACE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(ninkasi_options INTERFACE -Wall -Wextra -Wpedantic)
endif()

# ---------------------------------------------------------------------------
# ninkasi_calc: brewing math and units. Pure functions with no dependencies,
# so it can never reach into the database or the UI.
# ---------------------------------------------------------------------------
add_library(ninkasi_calc STATIC
    src/calc/alcohol.cpp
    src/calc/bitterness.cpp
    src/calc/color.cpp
    src/calc/extract.cpp
    src/calc/gravity.cpp
    src/calc/units.cpp
    src/calc/volumes.cpp
    src/calc/water.cpp
    src/calc/yeast.cpp
)
target_include_directories(ninkasi_calc PUBLIC src)
target_link_libraries(ninkasi_calc PUBLIC ninkasi_options)

# ---------------------------------------------------------------------------
# ninkasi_core: everything else that is not UI (domain, data, app services).
# Kept separate so tests can link it without a window or GL context.
# ---------------------------------------------------------------------------
add_library(ninkasi_core STATIC
    src/domain/element.cpp
    src/domain/recipe.cpp
    src/domain/recipe_math.cpp
    src/data/audit_log.cpp
    src/data/database.cpp
    src/data/element_repository.cpp
    src/data/ingredient_repository.cpp
    src/data/migrations.cpp
    src/data/recipe_json.cpp
    src/data/recipe_repository.cpp
    src/data/settings_repository.cpp
    src/app/calc_settings.cpp
    src/app/floor_service.cpp
    src/app/ingredient_library.cpp
    src/app/paths.cpp
    src/app/recipe_service.cpp
    src/app/session.cpp
    src/app/unit_settings.cpp
)
target_link_libraries(ninkasi_core PUBLIC ninkasi_calc sqlite3 PRIVATE nlohmann_json)

# ---------------------------------------------------------------------------
# ninkasi: the desktop application.
# ---------------------------------------------------------------------------
# The UI font is compiled into the executable so nothing needs to ship beside it.
set(NINKASI_FONT_SOURCE ${CMAKE_BINARY_DIR}/generated/roboto_medium.cpp)
ninkasi_embed_file(${imgui_SOURCE_DIR}/misc/fonts/Roboto-Medium.ttf ${NINKASI_FONT_SOURCE} kRobotoMediumTtf)

add_executable(ninkasi
    src/main.cpp
    src/ui/app_shell.cpp
    src/ui/element_style.cpp
    src/ui/elements/element_windows.cpp
    src/ui/floor/floor_view.cpp
    src/ui/fonts.cpp
    src/ui/theme.cpp
    src/ui/widgets.cpp
    ${NINKASI_FONT_SOURCE}
)
target_link_libraries(ninkasi PRIVATE ninkasi_core imgui)
target_compile_definitions(ninkasi PRIVATE
    NINKASI_VERSION="${PROJECT_VERSION}"
    IMGUI_DEFINE_MATH_OPERATORS  # ImVec2 + ImVec2, ImVec2 * float, ...
)

# Release builds on Windows are GUI apps (no console window); Debug keeps the
# console so log output and crashes are visible.
if(WIN32)
    set_target_properties(ninkasi PROPERTIES WIN32_EXECUTABLE $<CONFIG:Release>)
    if(MSVC)
        target_link_options(ninkasi PRIVATE $<$<CONFIG:Release>:/ENTRY:mainCRTStartup>)
    endif()
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(NINKASI_BUILD_TESTS)
    enable_testing()
    add_executable(ninkasi_tests
        tests/test_main.cpp
        tests/app/test_floor_service.cpp
        tests/app/test_recipe_service.cpp
        tests/app/test_unit_settings.cpp
        tests/calc/test_alcohol.cpp
        tests/calc/test_bitterness.cpp
        tests/calc/test_color.cpp
        tests/calc/test_extract.cpp
        tests/calc/test_gravity.cpp
        tests/calc/test_units.cpp
        tests/calc/test_volumes.cpp
        tests/calc/test_water.cpp
        tests/calc/test_yeast.cpp
        tests/data/test_database.cpp
        tests/data/test_recipe_json.cpp
        tests/data/test_settings.cpp
        tests/domain/test_element.cpp
        tests/domain/test_recipe_math.cpp
    )
    target_include_directories(ninkasi_tests PRIVATE tests)
    target_link_libraries(ninkasi_tests PRIVATE ninkasi_core doctest)
    add_test(NAME ninkasi_tests COMMAND ninkasi_tests)
endif()
