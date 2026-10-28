#include "ui/display.h"

#include "calc/color.h"
#include "calc/common.h"
#include "domain/activity.h"

#include <cstdio>

namespace ninkasi::ui {

std::string time_text(domain::Timestamp time) {
    const domain::LocalTime local = domain::to_local(time);
    char text[32];
    std::snprintf(text, sizeof text, "%04d-%02d-%02d %02d:%02d", local.year, local.month, local.day, local.hour,
                  local.minute);
    return text;
}

std::string date_text(domain::Date date) {
    return domain::to_iso8601(date);
}

std::string due_text(domain::Date due, domain::Date today) {
    switch (domain::days_between(today, due)) {
    case 0:  return "Today";
    case 1:  return "Tomorrow";
    case -1: return "Yesterday";
    default: return date_text(due);
    }
}

std::string ago_text(domain::Timestamp then, domain::Timestamp now) {
    const long long minutes = std::chrono::duration_cast<std::chrono::minutes>(now - then).count();
    char text[32];
    if (minutes < 60) {
        std::snprintf(text, sizeof text, "%lld min", minutes < 0 ? 0 : minutes);
    } else if (minutes < 48 * 60) {
        std::snprintf(text, sizeof text, "%lld h", minutes / 60);
    } else {
        std::snprintf(text, sizeof text, "%lld d", minutes / (24 * 60));
    }
    return text;
}

std::string number_text(double value, int decimals, std::string_view suffix) {
    if (!calc::is_valid(value)) return "--";
    char text[64];
    std::snprintf(text, sizeof text, "%.*f", decimals, value);
    return std::string(text) + std::string(suffix);
}

ImVec4 beer_color(double srm) {
    const calc::Rgb rgb = calc::srm_to_rgb(calc::is_valid(srm) ? srm : 4.0);
    return {rgb.r, rgb.g, rgb.b, 1.0f};
}

std::string rendered(std::string_view text, const calc::UnitPreferences& units) {
    return domain::render_quantities(text, units);
}

void table_row(const char* label) {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s", label);
    ImGui::TableNextColumn();
}

} // namespace ninkasi::ui
