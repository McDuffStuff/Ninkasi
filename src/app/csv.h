#pragma once
// Writing CSV files for spreadsheets, and reading the CSV files other
// programs write (point-of-sale exports).

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

// The separator a CSV file uses, judged from its first line: a semicolon or a
// tab if there are more of either than commas (outside quotes), as some
// countries' spreadsheets write; otherwise a comma.
char guess_csv_separator(std::string_view text);

// Reads CSV text as spreadsheets and point-of-sale systems write it (RFC
// 4180): fields separated by `separator`, quoted when they hold one, a quote
// or a line break, with "" for a quote inside quotes; lines end in CRLF or
// LF. A UTF-8 byte order mark is skipped and blank lines are left out.
std::vector<std::vector<std::string>> parse_csv(std::string_view text, char separator = ',');

} // namespace ninkasi::app
