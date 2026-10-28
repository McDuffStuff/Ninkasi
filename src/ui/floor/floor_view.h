#pragma once
// The Brewery Floor: a pan-and-zoom canvas showing the equipment where the
// brewer placed it. Tanks show the beer in them with its latest gravity,
// temperature and volume; badges count open tasks.
//
// Viewing (the default): hover for a summary, click for details, right-click
// for actions, drag empty space or middle-drag to pan, scroll to zoom.
// Editing ("Edit layout"): drag to move, drag the corner handle to resize,
// double-click for details, Delete to remove.

#include "app/batch_service.h"
#include "app/floor_service.h"
#include "app/task_service.h"
#include "calc/units.h"
#include "data/settings_repository.h"
#include "domain/element.h"
#include "domain/recipe_math.h"

#include <imgui.h>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>

namespace ninkasi::ui {

class CellarDialogs;
class ElementWindows;

class FloorView {
public:
    FloorView(app::FloorService& floor, app::BatchService& batches, app::TaskService& tasks, ElementWindows& windows,
              CellarDialogs& dialogs, data::SettingsRepository& settings, const calc::UnitPreferences& units,
              const domain::CalcSettings& calc_settings, float ui_scale);
    ~FloorView();  // remembers where the brewer was looking

    FloorView(const FloorView&) = delete;
    FloorView& operator=(const FloorView&) = delete;

    void draw(ImGuiID dockspace_id, bool* open);

    // Called by "Open batch" in the right-click menu.
    std::function<void(std::int64_t batch_id)> on_open_batch;

private:
    struct FloorPoint {
        double x = 0.0;
        double y = 0.0;
    };
    enum class Drag { None, Pan, Move, Resize };

    // Screen pixels per floor unit.
    double scale() const { return zoom_ * ui_scale_; }
    ImVec2 to_screen(double x, double y) const;
    FloorPoint to_world(ImVec2 screen) const;
    // An element's rectangle as drawn: the live one while it is being dragged.
    const domain::Rect& displayed_rect(const domain::Element& element) const;
    // An element's outline on screen. Vessels are drawn as circles.
    struct Shape {
        ImVec2 min;
        ImVec2 max;
        bool round = false;
        ImVec2 center;
        float radius = 0.0f;

        bool contains(ImVec2 point) const;
    };
    Shape shape_of(const domain::Element& element) const;
    // Screen-space box of an element's resize handle (min, max).
    std::pair<ImVec2, ImVec2> resize_handle(const domain::Element& element) const;
    std::optional<std::int64_t> hit_test(ImVec2 screen) const;
    bool over_resize_handle(const domain::Element& element, ImVec2 screen) const;

    void draw_toolbar();
    void draw_canvas();
    void handle_mouse(bool canvas_hovered, std::optional<std::int64_t> hovered);
    void finish_drag();
    void handle_keyboard();
    void draw_background(ImDrawList* draw_list) const;
    void draw_element(ImDrawList* draw_list, const domain::Element& element, bool hovered) const;
    void draw_labels(ImDrawList* draw_list, const domain::Element& element, const Shape& shape) const;
    void draw_task_badge(ImDrawList* draw_list, const domain::Element& element, const Shape& shape) const;
    void draw_empty_hint(ImDrawList* draw_list) const;
    void draw_tooltip(std::optional<std::int64_t> hovered);
    void draw_context_menu();
    void draw_element_actions(const domain::Element& element);
    void draw_delete_confirmation();

    void zoom_at(ImVec2 screen, double factor);
    void fit_to_view();
    void add_element(domain::ElementType type, FloorPoint center);
    void request_delete(std::int64_t id);
    void save_view();

    app::FloorService& floor_;
    app::BatchService& batches_;
    app::TaskService& tasks_;
    ElementWindows& windows_;
    CellarDialogs& dialogs_;
    data::SettingsRepository& settings_;
    const calc::UnitPreferences& units_;
    const domain::CalcSettings& calc_settings_;
    float ui_scale_;

    // The floor position at the canvas's top-left corner, and the zoom.
    double offset_x_ = -40.0;
    double offset_y_ = -40.0;
    double zoom_ = 1.0;
    bool fit_requested_ = false;
    ImVec2 canvas_pos_{};
    ImVec2 canvas_size_{};

    bool edit_mode_ = false;
    std::optional<std::int64_t> selected_;

    // The mouse drag in progress, if any.
    Drag drag_ = Drag::None;
    ImGuiMouseButton drag_button_ = ImGuiMouseButton_Left;
    std::int64_t drag_id_ = 0;
    domain::Rect drag_start_rect_;
    domain::Rect drag_rect_;
    ImVec2 drag_start_mouse_{};
    float drag_distance_ = 0.0f;
    std::optional<std::int64_t> pressed_element_;

    // Hover summary.
    std::optional<std::int64_t> hover_id_;
    double hover_since_ = 0.0;

    // Right-click menu and delete confirmation.
    std::optional<std::int64_t> context_element_;
    FloorPoint context_point_;
    std::int64_t delete_id_ = 0;
    std::optional<std::string> delete_blocker_;  // why it can't be deleted
    bool open_delete_popup_ = false;
};

} // namespace ninkasi::ui
