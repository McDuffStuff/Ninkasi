#pragma once
// The operating system's "open file" and "save file" dialogs.

#include <filesystem>
#include <optional>
#include <vector>

namespace ninkasi::ui {

// Lets the brewer pick one or more files whose names match `pattern`
// (e.g. "*.json"). Blocks until they choose; empty if they cancel. On Linux
// this uses zenity or kdialog, and returns nothing if neither is installed.
std::vector<std::filesystem::path> open_files_dialog(const char* title, const char* filter_name, const char* pattern);

// Lets the brewer choose where to save a file, starting with
// `suggested_name` and asking before replacing an existing file. `extension`
// ("csv") is added if they don't type one. nullopt if they cancel (or, on
// Linux, if neither zenity nor kdialog is installed).
std::optional<std::filesystem::path> save_file_dialog(const char* title, const char* filter_name, const char* pattern,
                                                      const char* extension, const char* suggested_name);

} // namespace ninkasi::ui
