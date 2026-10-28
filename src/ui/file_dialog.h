#pragma once
// The operating system's "open file", "save file" and "choose folder"
// dialogs, and showing a folder in its file browser.

#include <filesystem>
#include <optional>
#include <vector>

namespace ninkasi::ui {

// Lets the brewer pick one or more files whose names match `pattern`
// (e.g. "*.json", or several separated by spaces: "*.png *.jpg"). Blocks
// until they choose; empty if they cancel. On Linux
// this uses zenity or kdialog, and returns nothing if neither is installed.
std::vector<std::filesystem::path> open_files_dialog(const char* title, const char* filter_name, const char* pattern);

// Lets the brewer choose where to save a file, starting with
// `suggested_name` and asking before replacing an existing file. `extension`
// ("csv") is added if they don't type one. nullopt if they cancel (or, on
// Linux, if neither zenity nor kdialog is installed).
std::optional<std::filesystem::path> save_file_dialog(const char* title, const char* filter_name, const char* pattern,
                                                      const char* extension, const char* suggested_name);

// Lets the brewer choose a folder, starting in `start` if it exists. nullopt
// if they cancel (or, on Linux, if neither zenity nor kdialog is installed).
std::optional<std::filesystem::path> choose_folder_dialog(const char* title, const std::filesystem::path& start);

// Opens `folder` in Explorer (or the desktop's file manager on Linux).
void show_folder(const std::filesystem::path& folder);

} // namespace ninkasi::ui
