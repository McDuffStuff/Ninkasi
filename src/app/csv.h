#pragma once
// Writing CSV files for spreadsheets.

#include <filesystem>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

namespace ninkasi::app {

// A field, quoted if it holds a comma, quote or line break.
std::string csv_field(std::string_view text);
// Fields joined into a line ending in CRLF, quoting each as needed.
std::string csv_row(const std::vector<std::string>& fields);
std::string csv_row(std::initializer_list<std::string_view> fields);
// Writes `text` to `path` as UTF-8 with a byte order mark, so spreadsheets
// read accented and other non-ASCII characters correctly. Throws
// std::runtime_error if it can't.
void write_csv_file(const std::filesystem::path& path, std::string_view text);

} // namespace ninkasi::app
