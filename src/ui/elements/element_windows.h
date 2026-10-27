#pragma once
// Detail windows for floor equipment. Each can be docked, left floating,
// popped out into its own OS window (so several screens can be used at
// once), or maximized. The open windows are remembered between runs.

#include "app/floor_service.h"
#include "calc/units.h"
#include "data/settings_repository.h"
#include "domain/element.h"

#include <imgui.h>

#include <cstdint>
#include <string>
#include <vector>

namespace ninkasi::ui {

class ElementWindows {
public:
    ElementWindows(app::FloorService& floor, data::SettingsRepository& settings, const calc::UnitPreferences& units,
                   float ui_scale);

    // Opens the element's window, or brings it to the front if already open.
    void open(std::int64_t element_id);
    void draw();

private:
    struct Window {
        std::int64_t element_id = 0;
        bool focus = false;       // bring to the front next frame
        bool popped_out = false;  // in its own OS window
        bool undock = false;      // leave the dockspace next frame
        bool maximized = false;
        bool restore = false;     // return to restore_pos/size next frame
        ImVec2 restore_pos{};
        ImVec2 restore_size{};
        ImVec2 maximized_pos{};
        ImVec2 maximized_size{};
        bool editing_name = false;
        std::string name_edit;
    };

    // Returns false once the brewer closes the window.
    bool draw_window(Window& window, const domain::Element& element);
    void draw_window_controls(Window& window, const domain::Element& element);
    void draw_overview_tab(Window& window, const domain::Element& element);
    void draw_units_tab(const domain::Element& element);
    void maximize(Window& window);
    void save_open_windows();

    app::FloorService& floor_;
    data::SettingsRepository& settings_;
    const calc::UnitPreferences& units_;
    float ui_scale_;
    std::vector<Window> windows_;
};

} // namespace ninkasi::ui
