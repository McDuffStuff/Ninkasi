// Ninkasi entry point: opens the database, creates the window and runs the
// ImGui frame loop.
//
// Usage: ninkasi [--db <path>]
//   --db  use a specific database file instead of the default one in the
//         user data directory (handy for testing with a scratch brewery).

#include "app/paths.h"
#include "app/session.h"
#include "data/database.h"
#include "data/migrations.h"
#include "ui/app_shell.h"
#include "ui/fonts.h"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <GLFW/glfw3.h>

#include <cstdio>
#include <exception>
#include <filesystem>
#include <string>
#include <string_view>

namespace {

struct Options {
    std::filesystem::path db_path;
};

Options parse_args(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        if (std::string_view(argv[i]) == "--db" && i + 1 < argc) {
            options.db_path = argv[++i];
        }
    }
    if (options.db_path.empty()) {
        options.db_path = ninkasi::app::user_data_dir() / "ninkasi.db";
    }
    return options;
}

void glfw_error_callback(int error, const char* description) {
    std::fprintf(stderr, "GLFW error %d: %s\n", error, description);
}

int run(const Options& options) {
    using namespace ninkasi;

    const std::string db_path = options.db_path.string();
    data::Database db(db_path);
    data::migrate(db);
    app::Session session = app::open_local_session(db);

    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit()) {
        std::fprintf(stderr, "Failed to initialize GLFW\n");
        return 1;
    }

    // OpenGL 3.0 + GLSL 130: the widest-supported combination on Windows/Linux.
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);  // size the window for high-DPI monitors
    glfwWindowHint(GLFW_MAXIMIZED, GLFW_TRUE);
    GLFWwindow* window = glfwCreateWindow(1600, 900, "Ninkasi", nullptr, nullptr);
    if (!window) {
        std::fprintf(stderr, "Failed to create window\n");
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); // vsync

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable; // windows can leave the main window

    // Window layout is saved next to the database. ImGui keeps the pointer,
    // so the string must outlive the context.
    const std::string ini_path = (options.db_path.parent_path() / "imgui.ini").string();
    io.IniFilename = ini_path.c_str();

    // Scale text and spacing to the monitor (1.0 at 96 DPI, 1.5 at 144 DPI...).
    float x_scale = 1.0f, y_scale = 1.0f;
    glfwGetWindowContentScale(window, &x_scale, &y_scale);
    const float ui_scale = x_scale > 0.0f ? x_scale : 1.0f;
    ui::load_fonts(ui_scale);

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 130");

    {
        ui::AppShell shell(db, session, db_path, ui_scale);

        while (!glfwWindowShouldClose(window) && !shell.quit_requested()) {
            glfwPollEvents();
            if (glfwGetWindowAttrib(window, GLFW_ICONIFIED)) {
                glfwWaitEvents(); // don't burn CPU while minimized
                continue;
            }

            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();

            shell.draw();

            ImGui::Render();
            int width = 0, height = 0;
            glfwGetFramebufferSize(window, &width, &height);
            glViewport(0, 0, width, height);
            const ImVec4 bg = ImGui::GetStyle().Colors[ImGuiCol_WindowBg];
            glClearColor(bg.x, bg.y, bg.z, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

            // Render windows that have been dragged out into their own OS windows.
            if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
                GLFWwindow* backup_context = glfwGetCurrentContext();
                ImGui::UpdatePlatformWindows();
                ImGui::RenderPlatformWindowsDefault();
                glfwMakeContextCurrent(backup_context);
            }

            glfwSwapBuffers(window);
        }
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    try {
        return run(parse_args(argc, argv));
    } catch (const std::exception& e) {
        std::fprintf(stderr, "Ninkasi failed to start: %s\n", e.what());
        return 1;
    }
}
