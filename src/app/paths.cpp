#include "app/paths.h"

#include <cstdlib>

namespace ninkasi::app {

namespace fs = std::filesystem;

namespace {

fs::path env_path(const char* name) {
    const char* value = std::getenv(name);
    return (value && *value) ? fs::path(value) : fs::path();
}

} // namespace

fs::path user_data_dir() {
    fs::path dir;
#ifdef _WIN32
    dir = env_path("APPDATA");
    dir = dir.empty() ? fs::current_path() / "Ninkasi" : dir / "Ninkasi";
#else
    if (auto xdg = env_path("XDG_DATA_HOME"); !xdg.empty()) {
        dir = xdg / "ninkasi";
    } else if (auto home = env_path("HOME"); !home.empty()) {
        dir = home / ".local" / "share" / "ninkasi";
    } else {
        dir = fs::current_path() / "ninkasi";
    }
#endif
    fs::create_directories(dir);
    return dir;
}

} // namespace ninkasi::app
