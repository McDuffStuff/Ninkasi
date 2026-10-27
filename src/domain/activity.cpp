#include "domain/activity.h"

#include <cstdio>
#include <cstdlib>

namespace ninkasi::domain {

namespace {

struct TokenKind {
    std::string_view key;
    calc::Measure measure;
};

constexpr TokenKind kTokenKinds[] = {
    {"volume", calc::Measure::Volume},
    {"gravity", calc::Measure::Gravity},
    {"temperature", calc::Measure::Temperature},
};

std::string token(std::string_view key, double value) {
    char text[64];
    std::snprintf(text, sizeof text, "{%.*s:%.10g}", static_cast<int>(key.size()), key.data(), value);
    return text;
}

// The formatted quantity for one token's key and number, or nullopt if it
// isn't a token.
std::optional<std::string> render_token(std::string_view key, std::string_view number,
                                        const calc::UnitPreferences& units) {
    const std::string owned(number);  // strtod needs a null-terminated string
    char* end = nullptr;
    const double value = std::strtod(owned.c_str(), &end);
    if (owned.empty() || *end != '\0') return std::nullopt;
    for (const TokenKind& kind : kTokenKinds) {
        if (kind.key == key) return calc::format(value, units.unit_for(kind.measure));
    }
    return std::nullopt;
}

} // namespace

std::string volume_token(double volume_l) {
    return token("volume", volume_l);
}

std::string gravity_token(double sg) {
    return token("gravity", sg);
}

std::string temperature_token(double temp_c) {
    return token("temperature", temp_c);
}

std::string render_quantities(std::string_view text, const calc::UnitPreferences& units) {
    std::string result;
    std::size_t pos = 0;
    while (pos < text.size()) {
        const std::size_t open = text.find('{', pos);
        if (open == std::string_view::npos) {
            result += text.substr(pos);
            break;
        }
        result += text.substr(pos, open - pos);

        const std::size_t close = text.find('}', open);
        const std::size_t colon = text.find(':', open);
        std::optional<std::string> rendered;
        if (close != std::string_view::npos && colon < close) {
            rendered = render_token(text.substr(open + 1, colon - open - 1),
                                    text.substr(colon + 1, close - colon - 1), units);
        }
        if (rendered) {
            result += *rendered;
            pos = close + 1;
        } else {
            result += '{';
            pos = open + 1;
        }
    }
    return result;
}

} // namespace ninkasi::domain
