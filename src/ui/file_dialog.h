#pragma once
// The operating system's "open file" dialog.

#include <filesystem>
#include <vector>

namespace ninkasi::ui {

// Lets the brewer pick one or more files whose names match `pattern`
// (e.g. "*.json"). Blocks until they choose; empty if they cancel. On Linux
// this uses zenity or kdialog, and returns nothing if neither is installed.
std::vector<std::filesystem::path> open_files_dialog(const char* title, const char* filter_name, const char* pattern);

} // namespace ninkasi::ui
