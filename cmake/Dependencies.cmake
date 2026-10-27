# Third-party dependencies, downloaded at configure time with FetchContent.
# Nothing needs to be installed on the system besides a compiler and CMake
# (plus X11/Wayland/OpenGL dev headers on Linux — see README).

include(FetchContent)

# --- GLFW: windowing + input ------------------------------------------------
set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)
# Match our static MSVC runtime (GLFW defaults to the DLL runtime).
set(USE_MSVC_RUNTIME_LIBRARY_DLL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        3.4
    GIT_SHALLOW    TRUE
)

# --- Dear ImGui (docking branch: dockable + pop-out OS windows) --------------
# ImGui has no CMakeLists of its own; we build it ourselves below.
FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.91.9b-docking
    GIT_SHALLOW    TRUE
)

# --- SQLite amalgamation -----------------------------------------------------
FetchContent_Declare(sqlite
    URL https://www.sqlite.org/2025/sqlite-amalgamation-3490100.zip
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

# --- doctest (header-only test framework) ------------------------------------
# SOURCE_SUBDIR points at a directory with no CMakeLists so FetchContent only
# downloads it; doctest's own CMakeLists is too old for CMake 4.
FetchContent_Declare(doctest
    GIT_REPOSITORY https://github.com/doctest/doctest.git
    GIT_TAG        v2.4.11
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  do-not-build
)

# --- nlohmann/json (header-only): recipe storage and BeerJSON import ----------
FetchContent_Declare(json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SOURCE_SUBDIR  do-not-build
)

FetchContent_MakeAvailable(glfw imgui sqlite doctest json)

find_package(OpenGL REQUIRED)
find_package(Threads REQUIRED)

# --- Targets for dependencies without usable CMake builds --------------------
add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
    ${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp
)
target_include_directories(imgui PUBLIC
    ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends ${imgui_SOURCE_DIR}/misc/cpp)
target_link_libraries(imgui PUBLIC glfw OpenGL::GL)

add_library(sqlite3 STATIC ${sqlite_SOURCE_DIR}/sqlite3.c)
target_include_directories(sqlite3 PUBLIC ${sqlite_SOURCE_DIR})
target_compile_definitions(sqlite3 PRIVATE
    SQLITE_DQS=0                 # reject "double-quoted" string literals
    SQLITE_DEFAULT_FOREIGN_KEYS=1
    SQLITE_OMIT_LOAD_EXTENSION
)
target_link_libraries(sqlite3 PUBLIC Threads::Threads)

add_library(nlohmann_json INTERFACE)
target_include_directories(nlohmann_json INTERFACE ${json_SOURCE_DIR}/include)

add_library(doctest INTERFACE)
target_include_directories(doctest INTERFACE ${doctest_SOURCE_DIR})
# Include real std headers instead of doctest's forward declarations, which
# newer MSVC versions warn about.
target_compile_definitions(doctest INTERFACE DOCTEST_CONFIG_USE_STD_HEADERS)
