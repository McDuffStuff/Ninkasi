#pragma once
// Where Ninkasi keeps its files on disk.

#include <filesystem>

namespace ninkasi::app {

// Per-user data directory, created if it does not exist:
//   Windows: %APPDATA%\Ninkasi
//   Linux:   $XDG_DATA_HOME/ninkasi, or ~/.local/share/ninkasi
std::filesystem::path user_data_dir();

} // namespace ninkasi::app
