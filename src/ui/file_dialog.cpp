#include "ui/file_dialog.h"

#include <algorithm>
#include <string>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <commdlg.h>
#else
#include <sys/wait.h>

#include <array>
#include <cstdio>
#endif

namespace ninkasi::ui {

#ifdef _WIN32

namespace {

std::wstring widen(const char* text) {
    const int length = MultiByteToWideChar(CP_UTF8, 0, text, -1, nullptr, 0);
    std::wstring wide(static_cast<std::size_t>(length > 0 ? length - 1 : 0), L'\0');
    if (length > 1) MultiByteToWideChar(CP_UTF8, 0, text, -1, wide.data(), length);
    return wide;
}

} // namespace

std::vector<std::filesystem::path> open_files_dialog(const char* title, const char* filter_name, const char* pattern) {
    // Filter format: "Name\0pattern\0All files\0*.*\0\0".
    std::wstring filter = widen(filter_name);
    filter.push_back(L'\0');
    filter += widen(pattern);
    filter.push_back(L'\0');
    filter += L"All files";
    filter.push_back(L'\0');
    filter += L"*.*";
    filter.push_back(L'\0');
    const std::wstring wide_title = widen(title);

    std::wstring buffer(32768, L'\0');
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof dialog;
    dialog.lpstrFilter = filter.c_str();
    dialog.lpstrFile = buffer.data();
    dialog.nMaxFile = static_cast<DWORD>(buffer.size());
    dialog.lpstrTitle = wide_title.c_str();
    dialog.Flags = OFN_ALLOWMULTISELECT | OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&dialog)) return {};

    // One file: its full path. Several: the folder, then each file name.
    std::vector<std::wstring> parts;
    for (const wchar_t* part = buffer.c_str(); *part != L'\0'; part += wcslen(part) + 1) parts.emplace_back(part);
    if (parts.size() == 1) return {std::filesystem::path(parts[0])};

    std::vector<std::filesystem::path> files;
    for (std::size_t i = 1; i < parts.size(); ++i) files.push_back(std::filesystem::path(parts[0]) / parts[i]);
    return files;
}

std::optional<std::filesystem::path> save_file_dialog(const char* title, const char* filter_name, const char* pattern,
                                                      const char* extension, const char* suggested_name) {
    std::wstring filter = widen(filter_name);
    filter.push_back(L'\0');
    filter += widen(pattern);
    filter.push_back(L'\0');
    filter.push_back(L'\0');
    const std::wstring wide_title = widen(title);
    const std::wstring wide_extension = widen(extension);

    std::wstring buffer(32768, L'\0');
    const std::wstring suggested = widen(suggested_name);
    suggested.copy(buffer.data(), std::min(suggested.size(), buffer.size() - 1));
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof dialog;
    dialog.lpstrFilter = filter.c_str();
    dialog.lpstrFile = buffer.data();
    dialog.nMaxFile = static_cast<DWORD>(buffer.size());
    dialog.lpstrTitle = wide_title.c_str();
    dialog.lpstrDefExt = wide_extension.c_str();
    dialog.Flags = OFN_EXPLORER | OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetSaveFileNameW(&dialog)) return std::nullopt;
    return std::filesystem::path(buffer.c_str());
}

#else

std::vector<std::filesystem::path> open_files_dialog(const char* title, const char* filter_name, const char* pattern) {
    const std::string commands[] = {
        std::string("zenity --file-selection --multiple --separator='\n' --title='") + title + "' --file-filter='" +
            filter_name + " | " + pattern + "' 2>/dev/null",
        std::string("kdialog --getopenfilename --multiple --separate-output . '") + pattern + "' 2>/dev/null",
    };
    for (const std::string& command : commands) {
        FILE* pipe = popen(command.c_str(), "r");
        if (!pipe) continue;
        std::string output;
        std::array<char, 4096> chunk{};
        while (fgets(chunk.data(), static_cast<int>(chunk.size()), pipe)) output += chunk.data();
        const int status = pclose(pipe);
        constexpr int kCommandNotFound = 127;
        if (WIFEXITED(status) && WEXITSTATUS(status) == kCommandNotFound) continue;  // try the next tool

        std::vector<std::filesystem::path> files;
        std::size_t start = 0;
        while (start < output.size()) {
            const std::size_t end = output.find('\n', start);
            const std::string line = output.substr(start, end == std::string::npos ? std::string::npos : end - start);
            if (!line.empty()) files.emplace_back(line);
            if (end == std::string::npos) break;
            start = end + 1;
        }
        return files;
    }
    return {};
}

std::optional<std::filesystem::path> save_file_dialog(const char* title, const char* /*filter_name*/,
                                                      const char* pattern, const char* extension,
                                                      const char* suggested_name) {
    const std::string commands[] = {
        std::string("zenity --file-selection --save --confirm-overwrite --title='") + title + "' --filename='" +
            suggested_name + "' 2>/dev/null",
        std::string("kdialog --getsavefilename '") + suggested_name + "' '" + pattern + "' 2>/dev/null",
    };
    for (const std::string& command : commands) {
        FILE* pipe = popen(command.c_str(), "r");
        if (!pipe) continue;
        std::string output;
        std::array<char, 4096> chunk{};
        while (fgets(chunk.data(), static_cast<int>(chunk.size()), pipe)) output += chunk.data();
        const int status = pclose(pipe);
        constexpr int kCommandNotFound = 127;
        if (WIFEXITED(status) && WEXITSTATUS(status) == kCommandNotFound) continue;  // try the next tool
        while (!output.empty() && (output.back() == '\n' || output.back() == '\r')) output.pop_back();
        if (output.empty()) return std::nullopt;
        std::filesystem::path path(output);
        if (!path.has_extension()) path += std::string(".") + extension;
        return path;
    }
    return std::nullopt;
}

#endif

} // namespace ninkasi::ui
