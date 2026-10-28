#include "ui/date_widgets.h"

#include "ui/display.h"

#include <imgui.h>

#include <algorithm>
#include <cstdio>

namespace ninkasi::ui {

namespace {

constexpr const char* kMonths[] = {"January", "February", "March",     "April",   "May",      "June",
                                   "July",    "August",   "September", "October", "November", "December"};
constexpr const char* kWeekdays[] = {"Su", "Mo", "Tu", "We", "Th", "Fr", "Sa"};

// A small whole-number field without +/- buttons, e.g. for hours.
bool two_digit_field(const char* id, int& value, int min, int max) {
    ImGui::SetNextItemWidth(ImGui::CalcTextSize("000").x + ImGui::GetStyle().FramePadding.x * 2.0f);
    int edited = value;
    if (!ImGui::InputScalar(id, ImGuiDataType_S32, &edited, nullptr, nullptr, "%02d")) return false;
    value = std::clamp(edited, min, max);
    return true;
}

} // namespace

bool date_picker(const char* id, domain::Date& date) {
    ImGui::PushID(id);
    bool picked = false;

    // The month the calendar shows, as year * 12 + month index, kept in the
    // calling window's storage while the popup is open.
    ImGuiStorage* storage = ImGui::GetStateStorage();
    const ImGuiID shown_key = ImGui::GetID("shown_month");

    if (ImGui::Button(date_text(date).c_str())) {
        storage->SetInt(shown_key, date.year * 12 + date.month - 1);
        ImGui::OpenPopup("calendar");
    }

    if (ImGui::BeginPopup("calendar")) {
        int shown = storage->GetInt(shown_key, date.year * 12 + date.month - 1);
        const int year = shown / 12;
        const int month = shown % 12 + 1;
        const float cell = ImGui::GetFrameHeight() * 1.2f;
        const float width = cell * 7.0f + ImGui::GetStyle().CellPadding.x * 14.0f;

        // "<  October 2026  >"
        if (ImGui::ArrowButton("##previous", ImGuiDir_Left)) --shown;
        char title[32];
        std::snprintf(title, sizeof title, "%s %d", kMonths[month - 1], year);
        ImGui::SameLine((width - ImGui::CalcTextSize(title).x) * 0.5f);
        ImGui::TextUnformatted(title);
        ImGui::SameLine(width - ImGui::GetFrameHeight());
        if (ImGui::ArrowButton("##next", ImGuiDir_Right)) ++shown;
        storage->SetInt(shown_key, shown);

        const domain::Date today = domain::today();
        ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, ImVec2(0.5f, 0.5f));
        if (ImGui::BeginTable("##days", 7, ImGuiTableFlags_SizingFixedSame)) {
            for (const char* weekday : kWeekdays) {
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%s", weekday);
            }
            ImGui::TableNextRow();
            for (int blank = domain::weekday({year, month, 1}); blank > 0; --blank) ImGui::TableNextColumn();
            for (int day = 1; day <= domain::days_in_month(year, month); ++day) {
                ImGui::TableNextColumn();
                const domain::Date candidate{year, month, day};
                char label[8];
                std::snprintf(label, sizeof label, "%d", day);
                const bool is_today = candidate == today;
                if (is_today) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_CheckMark));
                if (ImGui::Selectable(label, candidate == date, ImGuiSelectableFlags_None, ImVec2(cell, 0.0f))) {
                    date = candidate;
                    picked = true;
                }
                if (is_today) ImGui::PopStyleColor();
            }
            ImGui::EndTable();
        }
        ImGui::PopStyleVar();

        if (ImGui::SmallButton("Today")) {
            date = today;
            picked = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    ImGui::PopID();
    return picked;
}

bool time_picker(const char* id, domain::Timestamp& time) {
    ImGui::PushID(id);
    domain::LocalTime local = domain::to_local(time);
    bool changed = false;

    domain::Date date{local.year, local.month, local.day};
    if (date_picker("date", date)) {
        local.year = date.year;
        local.month = date.month;
        local.day = date.day;
        changed = true;
    }
    const float tight = ImGui::GetStyle().ItemInnerSpacing.x;
    ImGui::SameLine();
    changed |= two_digit_field("##hour", local.hour, 0, 23);
    ImGui::SameLine(0.0f, tight);
    ImGui::TextUnformatted(":");
    ImGui::SameLine(0.0f, tight);
    changed |= two_digit_field("##minute", local.minute, 0, 59);
    if (changed) time = domain::from_local(local);

    ImGui::SameLine();
    if (ImGui::SmallButton("Now")) {
        time = domain::now();
        changed = true;
    }
    ImGui::PopID();
    return changed;
}

} // namespace ninkasi::ui
