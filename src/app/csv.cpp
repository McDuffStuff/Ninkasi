#include "app/csv.h"

#include <fstream>
#include <stdexcept>
#include <utility>

namespace ninkasi::app {

std::string csv_field(std::string_view text) {
    if (text.find_first_of(",\"\r\n") == std::string_view::npos) return std::string(text);
    std::string quoted = "\"";
    for (char c : text) {
        if (c == '"') quoted += '"';
        quoted += c;
    }
    return quoted + "\"";
}

std::string csv_row(const std::vector<std::string>& fields) {
    std::string line;
    for (std::size_t i = 0; i < fields.size(); ++i) {
        if (i > 0) line += ',';
        line += csv_field(fields[i]);
    }
    return line + "\r\n";
}

std::string csv_row(std::initializer_list<std::string_view> fields) {
    return csv_row(std::vector<std::string>(fields.begin(), fields.end()));
}

void write_csv_file(const std::filesystem::path& path, std::string_view text) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << "\xEF\xBB\xBF" << text;
    if (!file) {
        const std::u8string where = path.u8string();
        throw std::runtime_error("Couldn't write " + std::string(where.begin(), where.end()) + ".");
    }
}

char guess_csv_separator(std::string_view text) {
    int commas = 0;
    int semicolons = 0;
    int tabs = 0;
    bool quoted = false;
    for (const char c : text) {
        if (c == '"') quoted = !quoted;
        if (quoted) continue;
        if (c == '\n') break;
        commas += c == ',';
        semicolons += c == ';';
        tabs += c == '\t';
    }
    if (semicolons > commas && semicolons >= tabs) return ';';
    if (tabs > commas) return '\t';
    return ',';
}

std::vector<std::vector<std::string>> parse_csv(std::string_view text, char separator) {
    if (text.starts_with("\xEF\xBB\xBF")) text.remove_prefix(3);
    std::vector<std::vector<std::string>> rows;
    std::vector<std::string> row;
    std::string field;
    bool quoted = false;
    bool row_has_content = false;
    const auto end_row = [&] {
        row.push_back(std::move(field));
        field.clear();
        if (row_has_content || row.size() > 1) rows.push_back(std::move(row));
        row.clear();
        row_has_content = false;
    };
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (quoted) {
            if (c == '"') {
                if (i + 1 < text.size() && text[i + 1] == '"') {
                    field += '"';  // "" inside quotes is a quote
                    ++i;
                } else {
                    quoted = false;
                }
            } else {
                field += c;
            }
            continue;
        }
        if (c == '"') {
            quoted = true;
            row_has_content = true;
        } else if (c == separator) {
            row.push_back(std::move(field));
            field.clear();
        } else if (c == '\n') {
            end_row();
        } else if (c == '\r') {
            if (i + 1 < text.size() && text[i + 1] == '\n') continue;  // CRLF ends at the LF
            end_row();
        } else {
            field += c;
            row_has_content = true;
        }
    }
    if (!field.empty() || !row.empty() || row_has_content) end_row();
    return rows;
}

} // namespace ninkasi::app
