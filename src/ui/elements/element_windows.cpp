#include "ui/elements/element_windows.h"

#include "ui/element_style.h"
#include "ui/widgets.h"

#include <imgui_stdlib.h>

#include <algorithm>
#include <cstdlib>
#include <string_view>

namespace ninkasi::ui {

namespace {

constexpr const char* kOpenWindowsKey = "floor.open_elements";

void label_cell(const char* label) {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::TableNextColumn();
}

void wrapped_note(const char* text) {
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", text);
    ImGui::PopTextWrapPos();
}

} // namespace

ElementWindows::ElementWindows(app::FloorService& floor, data::SettingsRepository& settings,
                               const calc::UnitPreferences& units, float ui_scale)
    : floor_(floor), settings_(settings), units_(units), ui_scale_(ui_scale) {
    // Saved as comma-separated element ids, e.g. "3,7".
    const std::string saved = settings_.get_or(kOpenWindowsKey, "");
    const char* cursor = saved.c_str();
    while (*cursor != '\0') {
        char* end = nullptr;
        const long long id = std::strtoll(cursor, &end, 10);
        if (end == cursor) break;
        if (floor_.find(id)) windows_.push_back(Window{.element_id = id});
        cursor = (*end == ',') ? end + 1 : end;
    }
}

void ElementWindows::open(std::int64_t element_id) {
    for (Window& window : windows_) {
        if (window.element_id == element_id) {
            window.focus = true;
            return;
        }
    }
    windows_.push_back(Window{.element_id = element_id, .focus = true});
    save_open_windows();
}

void ElementWindows::draw() {
    bool changed = false;
    for (std::size_t i = 0; i < windows_.size();) {
        // Draw from a copy: the window can change the element while drawing it.
        const domain::Element* element = floor_.find(windows_[i].element_id);
        const bool keep = element && draw_window(windows_[i], domain::Element(*element));
        if (keep) {
            ++i;
        } else {
            windows_.erase(windows_.begin() + static_cast<std::ptrdiff_t>(i));
            changed = true;
        }
    }
    if (changed) save_open_windows();
}

bool ElementWindows::draw_window(Window& window, const domain::Element& element) {
    // New windows open near the middle of the app, staggered so they don't stack exactly.
    const float stagger = static_cast<float>(element.id % 6) * 28.0f * ui_scale_;
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter() + ImVec2(stagger, stagger), ImGuiCond_FirstUseEver,
                            {0.5f, 0.5f});
    ImGui::SetNextWindowSize(ImVec2(440.0f, 360.0f) * ui_scale_, ImGuiCond_FirstUseEver);

    if (window.focus) {
        ImGui::SetNextWindowFocus();
        window.focus = false;
    }
    if (window.undock) {
        ImGui::SetNextWindowDockID(0, ImGuiCond_Always);
        window.undock = false;
    }
    if (window.popped_out) {
        // Keep this window in its own OS window even when it overlaps the main one.
        ImGuiWindowClass window_class;
        window_class.ViewportFlagsOverrideSet = ImGuiViewportFlags_NoAutoMerge;
        ImGui::SetNextWindowClass(&window_class);
    }

    ImGuiWindowFlags flags = ImGuiWindowFlags_None;
    if (window.maximized) {
        ImGui::SetNextWindowPos(window.maximized_pos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(window.maximized_size, ImGuiCond_Always);
        flags |= ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoDocking;
    } else if (window.restore) {
        ImGui::SetNextWindowPos(window.restore_pos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(window.restore_size, ImGuiCond_Always);
        window.restore = false;
    }

    // "###" keeps the window's identity (position, docking) stable across renames.
    bool open = true;
    const std::string title = element.name + "###element_" + std::to_string(element.id);
    if (ImGui::Begin(title.c_str(), &open, flags)) {
        draw_window_controls(window, element);
        if (ImGui::BeginTabBar("##tabs")) {
            if (ImGui::BeginTabItem("Overview")) {
                draw_overview_tab(window, element);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Units")) {
                draw_units_tab(element);
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }
    ImGui::End();
    return open;
}

void ElementWindows::draw_window_controls(Window& window, const domain::Element& element) {
    // A dot in the equipment's accent color, then its type.
    const float radius = 5.0f * ui_scale_;
    const float line_height = ImGui::GetTextLineHeight();
    const ImVec2 cursor = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddCircleFilled(cursor + ImVec2(radius, line_height * 0.5f), radius,
                                                ImGui::ColorConvertFloat4ToU32(element_accent(element.type)));
    ImGui::Dummy({radius * 2.0f, line_height});
    ImGui::SameLine();
    ImGui::TextDisabled("%s", domain::info(element.type).name.data());

    // Window controls, right-aligned on the same line.
    const char* pop_label = window.popped_out ? "Return to main window" : "Pop out";
    const char* size_label = window.maximized ? "Restore" : "Maximize";
    const ImGuiStyle& style = ImGui::GetStyle();
    const float controls_width = ImGui::CalcTextSize(pop_label).x + ImGui::CalcTextSize(size_label).x +
                                 style.FramePadding.x * 4.0f + style.ItemSpacing.x;
    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, ImGui::GetContentRegionAvail().x - controls_width));

    if (ImGui::SmallButton(pop_label)) {
        if (window.popped_out) {
            window.popped_out = false;
            if (window.maximized) {
                window.maximized = false;
                window.restore = true;
            }
        } else {
            window.popped_out = true;
            window.undock = ImGui::IsWindowDocked();
        }
    }
    ImGui::SetItemTooltip("%s", window.popped_out ? "Put this window back inside Ninkasi's main window"
                                                  : "Give this window its own window, e.g. for a second monitor");

    ImGui::SameLine();
    ImGui::BeginDisabled(ImGui::IsWindowDocked() && !window.maximized);
    if (ImGui::SmallButton(size_label)) {
        if (window.maximized) {
            window.maximized = false;
            window.restore = true;
        } else {
            maximize(window);
        }
    }
    ImGui::EndDisabled();
    if (ImGui::IsWindowDocked()) ImGui::SetItemTooltip("Undock the window to maximize it.");

    ImGui::Separator();
}

void ElementWindows::draw_overview_tab(Window& window, const domain::Element& element) {
    const domain::ElementTypeInfo& type = domain::info(element.type);
    const calc::UnitPreferences units = units_.with_overrides(element.unit_overrides);

    if (ImGui::BeginTable("##overview", 2, ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("##label", ImGuiTableColumnFlags_WidthFixed, 100.0f * ui_scale_);
        ImGui::TableSetupColumn("##value", ImGuiTableColumnFlags_WidthStretch);

        label_cell("Name");
        if (!window.editing_name) window.name_edit = element.name;
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputText("##name", &window.name_edit);
        window.editing_name = ImGui::IsItemActive();
        // A blank name isn't saved; the field just shows the old name again.
        if (ImGui::IsItemDeactivatedAfterEdit() && window.name_edit.find_first_not_of(" \t") != std::string::npos) {
            floor_.rename(element.id, window.name_edit);
        }

        label_cell("Type");
        ImGui::TextUnformatted(type.name.data());

        if (type.is_vessel) {
            label_cell("Capacity");
            double capacity_l = element.capacity_l.value_or(0.0);
            ImGui::SetNextItemWidth(120.0f * ui_scale_);
            if (input_quantity("##capacity", capacity_l, units.unit_for(calc::Dimension::Volume))) {
                floor_.set_capacity(element.id, capacity_l > 0.0 ? std::optional<double>(capacity_l) : std::nullopt);
            }

            label_cell("Contents");
            ImGui::TextUnformatted("Empty");
        }
        ImGui::EndTable();
    }

    ImGui::Spacing();
    wrapped_note("Batches, logs and tasks for this equipment arrive with batch tracking (Phase 4).");
}

void ElementWindows::draw_units_tab(const domain::Element& element) {
    wrapped_note("Units for this equipment only. \"Brewery default\" follows the Units menu.");
    ImGui::Spacing();
    if (!ImGui::BeginTable("##units", 2, ImGuiTableFlags_SizingStretchProp)) return;
    ImGui::TableSetupColumn("##dimension", ImGuiTableColumnFlags_WidthFixed, 100.0f * ui_scale_);
    ImGui::TableSetupColumn("##unit", ImGuiTableColumnFlags_WidthStretch);

    for (calc::Dimension dimension : calc::kAllDimensions) {
        label_cell(calc::name(dimension).data());

        const std::optional<calc::Unit> chosen = element.unit_overrides[calc::index_of(dimension)];
        const std::string default_label =
            "Brewery default (" + std::string(calc::symbol(units_.unit_for(dimension))) + ")";
        const std::string preview = chosen ? unit_label(*chosen) : default_label;

        ImGui::PushID(static_cast<int>(calc::index_of(dimension)));
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::BeginCombo("##unit", preview.c_str())) {
            if (ImGui::Selectable(default_label.c_str(), !chosen)) {
                floor_.set_unit_override(element.id, dimension, std::nullopt);
            }
            for (calc::Unit unit : calc::units_of(dimension)) {
                if (ImGui::Selectable(unit_label(unit).c_str(), chosen == unit)) {
                    floor_.set_unit_override(element.id, dimension, unit);
                }
            }
            ImGui::EndCombo();
        }
        ImGui::PopID();
    }
    ImGui::EndTable();
}

void ElementWindows::maximize(Window& window) {
    window.restore_pos = ImGui::GetWindowPos();
    window.restore_size = ImGui::GetWindowSize();

    // Fill Ninkasi's main window; a popped-out window fills its own monitor.
    const ImGuiViewport* main = ImGui::GetMainViewport();
    window.maximized_pos = main->WorkPos;
    window.maximized_size = main->WorkSize;
    if (window.popped_out) {
        const ImVec2 center = window.restore_pos + window.restore_size * 0.5f;
        for (const ImGuiPlatformMonitor& monitor : ImGui::GetPlatformIO().Monitors) {
            const ImVec2 far_corner = monitor.MainPos + monitor.MainSize;
            if (center.x >= monitor.MainPos.x && center.y >= monitor.MainPos.y && center.x < far_corner.x &&
                center.y < far_corner.y) {
                window.maximized_pos = monitor.WorkPos;
                window.maximized_size = monitor.WorkSize;
                break;
            }
        }
    }
    window.maximized = true;
}

void ElementWindows::save_open_windows() {
    std::string ids;
    for (const Window& window : windows_) {
        if (!ids.empty()) ids += ',';
        ids += std::to_string(window.element_id);
    }
    settings_.set(kOpenWindowsKey, ids);
}

} // namespace ninkasi::ui
