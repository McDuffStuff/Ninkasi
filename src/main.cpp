// Ninkasi entry point: opens the database, creates the window, signs someone
// in and runs the ImGui frame loop.
//
// Usage: ninkasi [--db <path>]
//   --db  use a specific database file instead of the default one in the
//         user data directory (handy for testing with a scratch brewery).

// First on Windows, so GLFW's OpenGL header finds the macros it expects.
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

#include "app/backup_service.h"
#include "app/paths.h"
#include "app/session.h"
#include "app/user_service.h"
#include "data/database.h"
#include "data/migrations.h"
#include "data/settings_repository.h"
#include "ui/app_shell.h"
#include "ui/fonts.h"
#include "ui/sign_in_screen.h"
#include "ui/theme.h"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <implot.h>

#include <GLFW/glfw3.h>

#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using namespace ninkasi;

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
        options.db_path = app::user_data_dir() / "ninkasi.db";
    }
    return options;
}

void glfw_error_callback(int error, const char* description) {
    std::fprintf(stderr, "GLFW error %d: %s\n", error, description);
}

// Each person's window layout (ImGui's ini text), in their own settings.
constexpr const char* kLayoutKey = "ui.layout";

// The ImGui and ImPlot contexts, with their GLFW and OpenGL backends. Each
// person gets a fresh one when they sign in, so their own window layout is
// loaded before the first frame (as ImGui requires) and nothing of the last
// person's windows carries over.
class UiContext {
public:
    UiContext(GLFWwindow* window, float ui_scale, const std::string& layout) {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;  // windows can leave the main window
        // Layouts are kept in the database, one per person, not in imgui.ini.
        io.IniFilename = nullptr;
        if (!layout.empty()) ImGui::LoadIniSettingsFromMemory(layout.data(), layout.size());

        // Charts: time axes in local time, with a 24-hour clock.
        ImPlot::CreateContext();
        ImPlotStyle& plot_style = ImPlot::GetStyle();
        plot_style.UseLocalTime = true;
        plot_style.Use24HourClock = true;

        ui::load_fonts(ui_scale);
        ImGui_ImplGlfw_InitForOpenGL(window, true);
        ImGui_ImplOpenGL3_Init("#version 130");
    }

    ~UiContext() {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImPlot::DestroyContext();
        ImGui::DestroyContext();
    }

    UiContext(const UiContext&) = delete;
    UiContext& operator=(const UiContext&) = delete;

    // The window layout as it is now, to save.
    static std::string layout() {
        std::size_t size = 0;
        const char* text = ImGui::SaveIniSettingsToMemory(&size);
        return std::string(text, size);
    }
};

// Draws frames until `frame` returns false or the window is closed.
void run_frames(GLFWwindow* window, const std::function<bool()>& frame) {
    ImGuiIO& io = ImGui::GetIO();
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        if (glfwGetWindowAttrib(window, GLFW_ICONIFIED)) {
            glfwWaitEvents();  // don't burn CPU while minimized
            continue;
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        const bool keep_going = frame();

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
        if (!keep_going) return;
    }
}

// Shows the sign-in screen until someone signs in, or nullopt if the window
// is closed first. It wears the theme and text size of whoever signed in last.
std::optional<app::Session> sign_in(GLFWwindow* window, data::Database& db, app::UserService& users,
                                    float ui_scale) {
    ui::Theme theme = ui::Theme::Dark;
    float text_size = 1.0f;
    if (const std::optional<std::int64_t> last = users.last_signed_in()) {
        data::SettingsRepository theirs(db, *last);
        theme = ui::theme_from_name(theirs.get_or(ui::kThemeKey, "dark"));
        text_size = ui::text_size_from_setting(theirs.get_or(ui::kTextSizeKey, "1"));
    }
    UiContext ui(window, ui_scale * text_size, "");
    ui::apply_theme(theme, ui_scale * text_size);

    ui::SignInScreen screen(users, ui_scale * text_size);
    std::optional<app::Session> session;
    run_frames(window, [&] {
        session = screen.draw();
        return !session;
    });
    return session;
}

// The window layout someone starts with: their own, or else the one saved
// in imgui.ini beside the database before people signed in, if there is one.
std::string starting_layout(data::SettingsRepository& personal, const std::filesystem::path& db_path) {
    if (std::optional<std::string> saved = personal.get(kLayoutKey)) return *saved;
    std::ifstream file(db_path.parent_path() / "imgui.ini", std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

int run(const Options& options) {
    // A backup the brewer asked to restore replaces the data before it's opened.
    std::string startup_notice;
    try {
        if (const std::optional<std::string> restored = app::restore_pending_backup(options.db_path)) {
            startup_notice = *restored;
        }
    } catch (const std::exception& e) {
        startup_notice = std::string("The backup couldn't be restored, so nothing has changed. ") + e.what();
    }

    const std::string db_path = options.db_path.string();
    data::Database db(db_path);
    data::migrate(db);
    app::UserService users(db);

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
    glfwSwapInterval(1);  // vsync

    // Scale text and spacing to the monitor (1.0 at 96 DPI, 1.5 at 144 DPI...).
    float x_scale = 1.0f, y_scale = 1.0f;
    glfwGetWindowContentScale(window, &x_scale, &y_scale);
    const float ui_scale = x_scale > 0.0f ? x_scale : 1.0f;

    // Each pass is one person's time in Ninkasi, from signing in to signing out.
    // A one-person brewery without a password goes straight in.
    while (!glfwWindowShouldClose(window)) {
        const std::optional<app::Session> session =
            users.sign_in_needed() ? sign_in(window, db, users, ui_scale) : users.only_person();
        if (!session) break;  // the window was closed at the sign-in screen

        data::SettingsRepository personal(db, session->user_id);
        // The interface is built again, without signing in, when the person
        // changes their text size: everything is sized from it.
        bool greet = true;
        bool signed_out = false;
        while (true) {
            const float scale = ui_scale * ui::text_size_from_setting(personal.get_or(ui::kTextSizeKey, "1"));
            UiContext ui(window, scale, starting_layout(personal, options.db_path));
            ui::AppShell shell(db, users, *session, db_path, scale,
                               greet ? std::exchange(startup_notice, {}) : std::string(), greet);

            // Files dropped onto the window go to the shell (e.g. BeerJSON recipes).
            glfwSetWindowUserPointer(window, &shell);
            glfwSetDropCallback(window, [](GLFWwindow* dropped_on, int count, const char** paths) {
                std::vector<std::filesystem::path> files;
                for (int i = 0; i < count; ++i) {
                    files.emplace_back(reinterpret_cast<const char8_t*>(paths[i]));  // GLFW paths are UTF-8
                }
                static_cast<ui::AppShell*>(glfwGetWindowUserPointer(dropped_on))->files_dropped(std::move(files));
            });

            ImGuiIO& io = ImGui::GetIO();
            run_frames(window, [&] {
                shell.draw();
                // ImGui asks for the layout to be saved a few seconds after windows move.
                if (io.WantSaveIniSettings) {
                    personal.set(kLayoutKey, UiContext::layout());
                    io.WantSaveIniSettings = false;
                }
                return !shell.quit_requested() && !shell.sign_out_requested() && !shell.rebuild_requested();
            });
            personal.set(kLayoutKey, UiContext::layout());

            // The shell is about to go away; stop routing drops to it.
            glfwSetDropCallback(window, nullptr);
            glfwSetWindowUserPointer(window, nullptr);
            if (shell.rebuild_requested() && !glfwWindowShouldClose(window)) {
                greet = false;
                continue;
            }
            signed_out = shell.sign_out_requested();
            break;
        }
        if (!signed_out) break;
    }

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}

// Says why Ninkasi stopped. The release build on Windows has no console, so
// it shows a message box too.
void report_failure(const std::string& reason) {
    std::fprintf(stderr, "Ninkasi failed to start: %s\n", reason.c_str());
#ifdef _WIN32
    const std::string message = "Ninkasi couldn't start.\n\n" + reason;
    std::wstring wide(static_cast<std::size_t>(MultiByteToWideChar(CP_UTF8, 0, message.c_str(), -1, nullptr, 0)), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, message.c_str(), -1, wide.data(), static_cast<int>(wide.size()));
    MessageBoxW(nullptr, wide.c_str(), L"Ninkasi", MB_OK | MB_ICONERROR);
#endif
}

} // namespace

int main(int argc, char** argv) {
    try {
        return run(parse_args(argc, argv));
    } catch (const std::exception& e) {
        report_failure(e.what());
        return 1;
    }
}
