#include "app/csv.h"

#include <fstream>
#include <stdexcept>

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

} // namespace ninkasi::app
