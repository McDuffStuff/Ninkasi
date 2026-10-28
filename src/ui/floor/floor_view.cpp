#include "ui/floor/floor_view.h"

#include "calc/common.h"
#include "ui/cellar/batch_views.h"
#include "ui/cellar/cellar_dialogs.h"
#include "ui/display.h"
#include "ui/element_style.h"
#include "ui/elements/element_windows.h"
#include "ui/inventory/inventory_views.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace ninkasi::ui {

namespace {

using calc::Measure;

constexpr const char* kViewKey = "floor.view";
constexpr const char* kAddPopup = "add_equipment";
constexpr const char* kContextPopup = "floor_context";
constexpr const char* kDeletePopup = "Delete equipment?";

constexpr double kMinZoom = 0.25;
constexpr double kMaxZoom = 4.0;
constexpr double kGridStep = 50.0;    // floor units between grid lines; every 5th is darker
constexpr float kClickSlop = 4.0f;    // pixels a click may wander before it counts as a drag
constexpr double kHoverDelay = 0.35;  // seconds before the summary appears

ImVec4 mix(const ImVec4& a, const ImVec4& b, float t) {
    return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t};
}

ImU32 to_u32(const ImVec4& color) {
    return ImGui::ColorConvertFloat4ToU32(color);
}

const ImVec4& style_color(ImGuiCol color) {
    return ImGui::GetStyleColorVec4(color);
}

ImVec4 urgency_color(domain::TaskUrgency urgency) {
    switch (urgency) {
    case domain::TaskUrgency::Overdue:  return kErrorColor;
    case domain::TaskUrgency::DueToday: return kWarningColor;
    default:                            return {0.45f, 0.50f, 0.58f, 1.0f};  // slate: something to do, no rush
    }
}

} // namespace

FloorView::FloorView(app::FloorService& floor, app::BatchService& batches, app::TaskService& tasks,
                     const app::InventoryService& inventory, ElementWindows& windows, CellarDialogs& dialogs,
                     data::SettingsRepository& settings, const calc::UnitPreferences& units,
                     const domain::CalcSettings& calc_settings, float ui_scale)
    : floor_(floor),
      batches_(batches),
      tasks_(tasks),
      inventory_(inventory),
      windows_(windows),
      dialogs_(dialogs),
      settings_(settings),
      units_(units),
      calc_settings_(calc_settings),
      ui_scale_(ui_scale) {
    double x = 0.0, y = 0.0, zoom = 0.0;
    const std::optional<std::string> saved = settings_.get(kViewKey);
    if (saved && std::sscanf(saved->c_str(), "%lf %lf %lf", &x, &y, &zoom) == 3 && zoom > 0.0) {
        offset_x_ = x;
        offset_y_ = y;
        zoom_ = std::clamp(zoom, kMinZoom, kMaxZoom);
    } else {
        fit_requested_ = true;  // first run: frame whatever is on the floor
    }
}

FloorView::~FloorView() {
    try {
        save_view();
    } catch (...) {
        // Losing the view position isn't worth failing shutdown over.
    }
}

void FloorView::draw(ImGuiID dockspace_id, bool* open) {
    ImGui::SetNextWindowDockID(dockspace_id, ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Brewery Floor", open, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        draw_toolbar();
        draw_canvas();
        draw_context_menu();
        draw_delete_confirmation();
    }
    ImGui::End();
}

// --- Coordinates and hit testing -------------------------------------------

ImVec2 FloorView::to_screen(double x, double y) const {
    return {canvas_pos_.x + static_cast<float>((x - offset_x_) * scale()),
            canvas_pos_.y + static_cast<float>((y - offset_y_) * scale())};
}

FloorView::FloorPoint FloorView::to_world(ImVec2 screen) const {
    return {offset_x_ + (screen.x - canvas_pos_.x) / scale(), offset_y_ + (screen.y - canvas_pos_.y) / scale()};
}

const domain::Rect& FloorView::displayed_rect(const domain::Element& element) const {
    const bool dragging = (drag_ == Drag::Move || drag_ == Drag::Resize) && element.id == drag_id_;
    return dragging ? drag_rect_ : element.rect;
}

FloorView::Shape FloorView::shape_of(const domain::Element& element) const {
    const domain::Rect& rect = displayed_rect(element);
    Shape shape;
    shape.min = to_screen(rect.x, rect.y);
    shape.max = to_screen(rect.x + rect.width, rect.y + rect.height);
    // Vessels are round: the largest circle centered in their footprint.
    shape.round = domain::info(element.type).is_vessel;
    shape.center = (shape.min + shape.max) * 0.5f;
    shape.radius = std::min(shape.max.x - shape.min.x, shape.max.y - shape.min.y) * 0.5f;
    return shape;
}

bool FloorView::Shape::contains(ImVec2 point) const {
    if (round) {
        const ImVec2 d = point - center;
        return d.x * d.x + d.y * d.y <= radius * radius;
    }
    return point.x >= min.x && point.x < max.x && point.y >= min.y && point.y < max.y;
}

std::pair<ImVec2, ImVec2> FloorView::resize_handle(const domain::Element& element) const {
    const Shape shape = shape_of(element);
    const float size = 12.0f * ui_scale_;
    if (shape.round) {
        // On the rim, toward the bottom-right.
        const ImVec2 rim = shape.center + ImVec2(0.7071f, 0.7071f) * shape.radius;
        return {rim - ImVec2(size, size) * 0.5f, rim + ImVec2(size, size) * 0.5f};
    }
    return {shape.max - ImVec2(size, size), shape.max};  // inside the bottom-right corner
}

std::optional<std::int64_t> FloorView::hit_test(ImVec2 screen) const {
    // Later elements are drawn on top, so test them first.
    const std::vector<domain::Element>& elements = floor_.elements();
    for (auto it = elements.rbegin(); it != elements.rend(); ++it) {
        if (shape_of(*it).contains(screen)) return it->id;
        // A round element's handle pokes outside it; it still belongs to it.
        if (edit_mode_ && selected_ == it->id && over_resize_handle(*it, screen)) return it->id;
    }
    return std::nullopt;
}

bool FloorView::over_resize_handle(const domain::Element& element, ImVec2 screen) const {
    const auto [min, max] = resize_handle(element);
    return screen.x >= min.x && screen.x <= max.x && screen.y >= min.y && screen.y <= max.y;
}

// --- Toolbar ----------------------------------------------------------------

void FloorView::draw_toolbar() {
    ImGui::Checkbox("Edit layout", &edit_mode_);

    ImGui::SameLine();
    if (ImGui::Button("Add equipment")) ImGui::OpenPopup(kAddPopup);
    if (ImGui::BeginPopup(kAddPopup)) {
        for (const domain::ElementTypeInfo& type : domain::element_types()) {
            if (ImGui::MenuItem(type.name.data())) add_element(type.type, to_world(canvas_pos_ + canvas_size_ * 0.5f));
        }
        ImGui::EndPopup();
    }

    ImGui::SameLine();
    if (ImGui::Button("Fit")) fit_requested_ = true;
    ImGui::SetItemTooltip("Zoom to show all equipment");
    ImGui::SameLine();
    if (ImGui::Button("100%")) zoom_at(canvas_pos_ + canvas_size_ * 0.5f, 1.0 / zoom_);
    ImGui::SameLine();
    ImGui::TextDisabled("%.0f%%", zoom_ * 100.0);

    ImGui::SameLine(0.0f, 16.0f * ui_scale_);
    ImGui::TextDisabled("%s", edit_mode_ ? "Drag to move · drag the corner to resize · Delete removes"
                                         : "Click for details · right-click for actions · drag to pan · scroll to zoom");
}

// --- Canvas -----------------------------------------------------------------

void FloorView::draw_canvas() {
    canvas_pos_ = ImGui::GetCursorScreenPos();
    canvas_size_ = ImGui::GetContentRegionAvail();
    canvas_size_.x = std::max(canvas_size_.x, 50.0f);
    canvas_size_.y = std::max(canvas_size_.y, 50.0f);

    // One invisible button covers the canvas and captures the mouse while a
    // button is held, so drags keep working outside it.
    ImGui::InvisibleButton("##floor_canvas", canvas_size_,
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight |
                               ImGuiButtonFlags_MouseButtonMiddle);
    const bool canvas_hovered = ImGui::IsItemHovered();

    if (fit_requested_) {
        fit_to_view();
        fit_requested_ = false;
    }

    const ImGuiIO& io = ImGui::GetIO();
    std::optional<std::int64_t> hovered;
    if (canvas_hovered && drag_ == Drag::None) hovered = hit_test(io.MousePos);
    if (canvas_hovered && io.MouseWheel != 0.0f) zoom_at(io.MousePos, std::pow(1.15, io.MouseWheel));

    handle_mouse(canvas_hovered, hovered);
    handle_keyboard();

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    draw_list->PushClipRect(canvas_pos_, canvas_pos_ + canvas_size_, true);
    draw_background(draw_list);
    for (const domain::Element& element : floor_.elements()) {
        draw_element(draw_list, element, hovered == element.id);
    }
    if (floor_.elements().empty()) draw_empty_hint(draw_list);
    draw_list->PopClipRect();

    draw_tooltip(hovered);
}

void FloorView::handle_mouse(bool canvas_hovered, std::optional<std::int64_t> hovered) {
    const ImGuiIO& io = ImGui::GetIO();

    if (canvas_hovered && drag_ == Drag::None) {
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            drag_button_ = ImGuiMouseButton_Left;
            drag_start_mouse_ = io.MousePos;
            drag_distance_ = 0.0f;
            pressed_element_ = hovered;
            selected_ = hovered;

            const domain::Element* element = hovered ? floor_.find(*hovered) : nullptr;
            if (element && edit_mode_) {
                drag_ = over_resize_handle(*element, io.MousePos) ? Drag::Resize : Drag::Move;
                drag_id_ = element->id;
                drag_start_rect_ = drag_rect_ = element->rect;
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) windows_.open(element->id);
            } else {
                drag_ = Drag::Pan;  // a click without movement opens the element (see finish_drag)
            }
        } else if (ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) {
            drag_ = Drag::Pan;
            drag_button_ = ImGuiMouseButton_Middle;
            drag_start_mouse_ = io.MousePos;
            drag_distance_ = 0.0f;
            pressed_element_.reset();
        } else if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            context_element_ = hovered;
            context_point_ = to_world(io.MousePos);
            if (hovered) selected_ = hovered;
            ImGui::OpenPopup(kContextPopup);
        }
    }

    if (drag_ == Drag::None) {
        if (hovered) {
            const domain::Element* element = floor_.find(*hovered);
            if (edit_mode_ && element && over_resize_handle(*element, io.MousePos)) {
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNWSE);
            } else {
                ImGui::SetMouseCursor(edit_mode_ ? ImGuiMouseCursor_ResizeAll : ImGuiMouseCursor_Hand);
            }
        }
        return;
    }

    if (ImGui::IsMousePosValid()) {
        drag_distance_ += std::abs(io.MouseDelta.x) + std::abs(io.MouseDelta.y);
        const double dx = (io.MousePos.x - drag_start_mouse_.x) / scale();
        const double dy = (io.MousePos.y - drag_start_mouse_.y) / scale();
        switch (drag_) {
        case Drag::Pan:
            offset_x_ -= io.MouseDelta.x / scale();
            offset_y_ -= io.MouseDelta.y / scale();
            if (drag_distance_ >= kClickSlop) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
            break;
        case Drag::Move:
            drag_rect_.x = drag_start_rect_.x + dx;
            drag_rect_.y = drag_start_rect_.y + dy;
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
            break;
        case Drag::Resize:
            if (const domain::Element* element = floor_.find(drag_id_); element && domain::info(element->type).is_vessel) {
                // Round elements stay round: one size, following the drag diagonally.
                const double diameter = std::min(drag_start_rect_.width, drag_start_rect_.height) + (dx + dy) / 2.0;
                drag_rect_.width = drag_rect_.height = std::max(domain::kMinElementSize, diameter);
            } else {
                drag_rect_.width = std::max(domain::kMinElementSize, drag_start_rect_.width + dx);
                drag_rect_.height = std::max(domain::kMinElementSize, drag_start_rect_.height + dy);
            }
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNWSE);
            break;
        case Drag::None:
            break;
        }
    }

    if (!ImGui::IsMouseDown(drag_button_)) finish_drag();
}

void FloorView::finish_drag() {
    const bool was_click = drag_distance_ < kClickSlop;
    if (drag_ == Drag::Move || drag_ == Drag::Resize) {
        // A click with a little jitter shouldn't nudge the element.
        if (!was_click && !(drag_rect_ == drag_start_rect_) && floor_.find(drag_id_)) {
            floor_.set_rect(drag_id_, drag_rect_);
        }
    } else if (drag_ == Drag::Pan && was_click && !edit_mode_ && pressed_element_ &&
               drag_button_ == ImGuiMouseButton_Left) {
        open_element(*pressed_element_);
    }
    drag_ = Drag::None;
    drag_id_ = 0;
    pressed_element_.reset();
}

void FloorView::handle_keyboard() {
    if (!ImGui::IsWindowFocused() || ImGui::GetIO().WantTextInput) return;
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) selected_.reset();
    if (edit_mode_ && selected_ &&
        (ImGui::IsKeyPressed(ImGuiKey_Delete) || ImGui::IsKeyPressed(ImGuiKey_Backspace))) {
        request_delete(*selected_);
    }
}

// --- Drawing ----------------------------------------------------------------

void FloorView::draw_background(ImDrawList* draw_list) const {
    const ImVec4& surface = style_color(ImGuiCol_WindowBg);
    const ImVec4& ink = style_color(ImGuiCol_Text);
    const ImVec2 end = canvas_pos_ + canvas_size_;
    draw_list->AddRectFilled(canvas_pos_, end, to_u32(mix(surface, ink, 0.03f)));

    if (kGridStep * scale() < 8.0) return;  // too dense to help
    const ImU32 minor = to_u32(mix(surface, ink, 0.07f));
    const ImU32 major = to_u32(mix(surface, ink, 0.13f));

    for (auto i = static_cast<long long>(std::floor(offset_x_ / kGridStep));; ++i) {
        const float x = to_screen(static_cast<double>(i) * kGridStep, 0.0).x;
        if (x > end.x) break;
        draw_list->AddLine({x, canvas_pos_.y}, {x, end.y}, i % 5 == 0 ? major : minor);
    }
    for (auto i = static_cast<long long>(std::floor(offset_y_ / kGridStep));; ++i) {
        const float y = to_screen(0.0, static_cast<double>(i) * kGridStep).y;
        if (y > end.y) break;
        draw_list->AddLine({canvas_pos_.x, y}, {end.x, y}, i % 5 == 0 ? major : minor);
    }
}

void FloorView::draw_element(ImDrawList* draw_list, const domain::Element& element, bool hovered) const {
    const Shape shape = shape_of(element);
    const ImVec2 min = shape.min;
    const ImVec2 max = shape.max;
    const ImVec2 canvas_end = canvas_pos_ + canvas_size_;
    if (max.x < canvas_pos_.x || max.y < canvas_pos_.y || min.x > canvas_end.x || min.y > canvas_end.y) return;

    const bool selected = selected_ == element.id;
    const float px = static_cast<float>(scale());
    const ImVec4& surface = style_color(ImGuiCol_WindowBg);
    const ImVec4& border = style_color(ImGuiCol_Border);
    const bool out_of_service = domain::holds_beer(element.type) &&
                                batches_.condition(element.id) == domain::VesselCondition::OutOfService;
    // Equipment that's out of service is drawn grayed out.
    const ImVec4 accent = out_of_service ? style_color(ImGuiCol_TextDisabled) : element_accent(element.type);
    const ImVec4 outline = selected ? accent : hovered ? mix(border, accent, 0.6f) : border;
    const float thickness = (selected ? 2.5f : hovered ? 1.75f : 1.0f) * ui_scale_;

    if (shape.round) {
        // Tanks: a body with an accent-colored rim.
        const float rim = 5.0f * px;
        draw_list->AddCircleFilled(shape.center, shape.radius, to_u32(mix(surface, accent, 0.16f)));
        draw_list->AddCircle(shape.center, shape.radius - rim * 0.5f, to_u32(accent), 0, rim);
        draw_list->AddCircle(shape.center, shape.radius, to_u32(outline), 0, thickness);
    } else {
        // Body, then an accent strip along the top (clipped so it keeps the
        // body's rounded corners).
        const float rounding = 6.0f * px;
        draw_list->AddRectFilled(min, max, to_u32(mix(surface, accent, 0.16f)), rounding);
        draw_list->PushClipRect(min, {max.x, min.y + 5.0f * px}, true);
        draw_list->AddRectFilled(min, max, to_u32(accent), rounding);
        draw_list->PopClipRect();
        draw_list->AddRect(min, max, to_u32(outline), rounding, ImDrawFlags_None, thickness);
    }

    draw_labels(draw_list, element, shape);
    draw_task_badge(draw_list, element, shape);

    if (selected && edit_mode_) {
        const auto [handle_min, handle_max] = resize_handle(element);
        draw_list->AddRectFilled(handle_min, handle_max, to_u32(accent), 2.0f * ui_scale_);
    }
}

void FloorView::draw_labels(ImDrawList* draw_list, const domain::Element& element, const Shape& shape) const {
    // Labels scale with the zoom and disappear when too small to read.
    const float font_size = ImGui::GetFontSize() * static_cast<float>(zoom_);
    if (font_size < 7.0f) return;

    const domain::ElementTypeInfo& type = domain::info(element.type);
    const calc::UnitPreferences units = units_.with_overrides(element.unit_overrides);
    ImFont* font = ImGui::GetFont();
    const ImU32 text = ImGui::GetColorU32(ImGuiCol_Text);
    const ImU32 muted = ImGui::GetColorU32(ImGuiCol_TextDisabled);
    const float small = font_size * 0.85f;
    const float px = static_cast<float>(scale());

    struct Line {
        std::string text;
        float size;
        ImU32 color;
    };
    std::vector<Line> lines{{element.name, font_size, text}};

    const std::optional<app::VesselContents> held = batches_.contents(element.id);
    if (const domain::Batch* batch = held ? batches_.find_active(held->batch_id) : nullptr) {
        // The beer, its latest gravity and temperature, and how much there is.
        lines.push_back({domain::label(*batch), small, text});
        std::string status;
        if (const domain::Reading* reading = batch->latest_gravity()) {
            status = calc::format(*reading->gravity_sg, units.unit_for(Measure::Gravity));
        } else if (const std::optional<double> og = batch->og()) {
            status = "OG " + calc::format(*og, units.unit_for(Measure::Gravity));
        }
        if (const domain::Reading* reading = batch->latest_temperature()) {
            if (!status.empty()) status += " · ";
            status += calc::format(*reading->temperature_c, units.unit_for(Measure::Temperature));
        }
        if (!status.empty()) lines.push_back({status, small, muted});
        lines.push_back({calc::format(held->volume_l, units.unit_for(Measure::Volume)), small, muted});
    } else {
        // The type, unless the name already says it ("Keg washer", "Fermenter 2").
        if (!element.name.starts_with(type.name)) lines.push_back({std::string(type.name), small, muted});
        if (type.is_vessel && element.capacity_l) {
            lines.push_back({calc::format(*element.capacity_l, units.unit_for(Measure::Volume)), small, muted});
        }
        if (domain::holds_beer(element.type)) {
            const domain::VesselCondition condition = batches_.condition(element.id);
            if (condition == domain::VesselCondition::Dirty) {
                lines.push_back({"Needs cleaning", small, to_u32(kWarningColor)});
            } else if (condition == domain::VesselCondition::OutOfService) {
                lines.push_back({"Out of service", small, to_u32(kErrorColor)});
            }
        }
        if (element.type == domain::ElementType::DryStorage || element.type == domain::ElementType::ColdStorage) {
            // How many items are kept here, and how many are running low.
            int kept = 0;
            int low = 0;
            for (const app::ItemStock& entry : inventory_.stock()) {
                if (entry.item.location_id != element.id) continue;
                ++kept;
                if (entry.low() || entry.on_hand < 0.0) ++low;
            }
            if (kept > 0) lines.push_back({std::to_string(kept) + (kept == 1 ? " item" : " items"), small, muted});
            if (low > 0) lines.push_back({std::to_string(low) + " low", small, to_u32(kWarningColor)});
        }
        if (element.type == domain::ElementType::Brewhouse) {
            const std::size_t waiting = batches_.pending_brews(element.id).size();
            if (waiting == 1) {
                lines.push_back({"Knock-out to log", small, to_u32(kWarningColor)});
            } else if (waiting > 1) {
                lines.push_back({std::to_string(waiting) + " knock-outs to log", small, to_u32(kWarningColor)});
            }
        }
    }

    if (shape.round) {
        // Centered in the circle, clipped to its middle so text stays inside.
        float height = 0.0f;
        for (const Line& line : lines) height += line.size * 1.2f;
        const float half_width = shape.radius * 0.85f;
        draw_list->PushClipRect(shape.center - ImVec2(half_width, shape.radius),
                                shape.center + ImVec2(half_width, shape.radius), true);
        float y = shape.center.y - height * 0.5f;
        for (const Line& line : lines) {
            const float width = font->CalcTextSizeA(line.size, FLT_MAX, 0.0f, line.text.c_str()).x;
            draw_list->AddText(font, line.size, {shape.center.x - width * 0.5f, y}, line.color, line.text.c_str());
            y += line.size * 1.2f;
        }
        draw_list->PopClipRect();
    } else {
        const ImVec2 pad(8.0f * px, 10.0f * px);
        draw_list->PushClipRect(shape.min, shape.max, true);
        float y = shape.min.y + pad.y;
        for (const Line& line : lines) {
            draw_list->AddText(font, line.size, {shape.min.x + pad.x, y}, line.color, line.text.c_str());
            y += line.size * 1.2f;
        }
        draw_list->PopClipRect();
    }
}

void FloorView::draw_task_badge(ImDrawList* draw_list, const domain::Element& element, const Shape& shape) const {
    int open = 0;
    for (const domain::Task& task : tasks_.open_tasks()) {
        if (task.element_id == element.id) ++open;
    }
    if (open == 0) return;

    // A numbered dot at the top-right: on the rim of a tank, on the corner of a box.
    const ImVec2 at = shape.round ? shape.center + ImVec2(0.7071f, -0.7071f) * shape.radius
                                  : ImVec2(shape.max.x, shape.min.y);
    // Grows with the equipment, within limits so it stays readable and unobtrusive.
    const ImVec2 extent = shape.max - shape.min;
    const float half_size = shape.round ? shape.radius : std::min(extent.x, extent.y) * 0.5f;
    const float radius = std::clamp(half_size * 0.13f, 8.0f * ui_scale_, 15.0f * ui_scale_);
    const domain::TaskUrgency urgency = tasks_.urgency_for(element.id, domain::today());
    draw_list->AddCircleFilled(at, radius, to_u32(urgency_color(urgency)));
    draw_list->AddCircle(at, radius, to_u32(style_color(ImGuiCol_WindowBg)), 0, 1.5f * ui_scale_);
    const std::string count = open > 9 ? "9+" : std::to_string(open);
    const float size = radius * 1.3f;
    const ImVec2 text_size = ImGui::GetFont()->CalcTextSizeA(size, FLT_MAX, 0.0f, count.c_str());
    draw_list->AddText(ImGui::GetFont(), size, at - text_size * 0.5f, IM_COL32_WHITE, count.c_str());
}

void FloorView::draw_empty_hint(ImDrawList* draw_list) const {
    const char* lines[] = {
        "Your brewery floor is empty.",
        "Use \"Add equipment\" or right-click to place tanks, storage and stations.",
    };
    const ImU32 muted = ImGui::GetColorU32(ImGuiCol_TextDisabled);
    const float line_height = ImGui::GetTextLineHeightWithSpacing();
    float y = canvas_pos_.y + canvas_size_.y * 0.5f - line_height;
    for (const char* line : lines) {
        const float width = ImGui::CalcTextSize(line).x;
        draw_list->AddText({canvas_pos_.x + (canvas_size_.x - width) * 0.5f, y}, muted, line);
        y += line_height;
    }
}

void FloorView::draw_tooltip(std::optional<std::int64_t> hovered) {
    if (!hovered || drag_ != Drag::None) {
        hover_id_.reset();
        return;
    }
    if (hover_id_ != hovered) {
        hover_id_ = hovered;
        hover_since_ = ImGui::GetTime();
    }
    const domain::Element* element = floor_.find(*hovered);
    if (!element || ImGui::GetTime() - hover_since_ < kHoverDelay) return;

    const domain::ElementTypeInfo& type = domain::info(element->type);
    const calc::UnitPreferences units = units_.with_overrides(element->unit_overrides);
    const calc::Unit volume = units.unit_for(Measure::Volume);
    const calc::Unit gravity = units.unit_for(Measure::Gravity);
    const std::optional<app::VesselContents> held = batches_.contents(element->id);
    const domain::Batch* batch = held ? batches_.find_active(held->batch_id) : nullptr;

    ImGui::BeginTooltip();
    ImGui::TextUnformatted(element->name.c_str());
    ImGui::TextDisabled("%s", type.name.data());

    if (batch) {
        ImGui::Separator();
        const float swatch = ImGui::GetTextLineHeight();
        ImGui::ColorButton("##beer", beer_color(domain::calculate(batch->recipe, calc_settings_).srm),
                           ImGuiColorEditFlags_NoTooltip, ImVec2(swatch, swatch));
        ImGui::SameLine();
        ImGui::TextUnformatted(domain::label(*batch).c_str());
        ImGui::TextDisabled("%s · %s", domain::name(batch->status).data(), batch_age_text(*batch).c_str());
        std::string fill = calc::format(held->volume_l, volume);
        if (element->capacity_l) fill += " of " + calc::format(*element->capacity_l, volume);
        ImGui::TextUnformatted(fill.c_str());

        const std::optional<double> og = batch->og();
        const domain::Reading* latest = batch->latest_gravity();
        if (og && latest) {
            const double abv = calc::abv(*og, *latest->gravity_sg, calc_settings_.abv_formula);
            ImGui::Text("OG %s · now %s (%s ago) · %s ABV", calc::format(*og, gravity).c_str(),
                        calc::format(*latest->gravity_sg, gravity).c_str(),
                        ago_text(latest->at, domain::now()).c_str(), number_text(abv * 100.0, 1, " %").c_str());
        } else if (og) {
            ImGui::Text("OG %s", calc::format(*og, gravity).c_str());
        }
        if (const domain::Reading* warmth = batch->latest_temperature()) {
            ImGui::Text("%s", calc::format(*warmth->temperature_c, units.unit_for(Measure::Temperature)).c_str());
        }
    } else if (type.is_vessel) {
        ImGui::Separator();
        ImGui::Text("Capacity: %s",
                    element->capacity_l ? calc::format(*element->capacity_l, volume).c_str() : "not set");
        if (domain::holds_beer(element->type)) {
            ImGui::Text("Empty · %s", domain::name(batches_.condition(element->id)).data());
        }
        for (const domain::Brew& brew : batches_.pending_brews(element->id)) {
            if (const domain::Batch* waiting = batches_.find_active(brew.batch_id)) {
                ImGui::TextColored(kWarningColor, "Knock-out to log:");
                ImGui::SameLine();
                ImGui::Text("%s, brewed %s", domain::label(*waiting).c_str(), time_text(brew.started_at).c_str());
            }
        }
    }

    // Storage: what's running low.
    if (element->type == domain::ElementType::DryStorage || element->type == domain::ElementType::ColdStorage) {
        bool header = false;
        for (const app::ItemStock& entry : inventory_.stock()) {
            if (entry.item.location_id != element->id || !(entry.low() || entry.on_hand < 0.0)) continue;
            if (!header) {
                ImGui::Separator();
                ImGui::TextColored(kWarningColor, "Low stock");
                header = true;
            }
            ImGui::Text("%s: %s", entry.item.name.c_str(), stock_text(entry.item, entry.on_hand, units_).c_str());
        }
    }

    // Open tasks, most pressing first.
    const std::vector<domain::Task> tasks = tasks_.open_for(element->id);
    if (!tasks.empty()) {
        ImGui::Separator();
        const domain::Date today = domain::today();
        for (std::size_t i = 0; i < tasks.size() && i < 4; ++i) {
            const domain::Task& task = tasks[i];
            if (task.due) {
                const ImVec4 color = domain::urgency(task, today) == domain::TaskUrgency::Upcoming
                                         ? style_color(ImGuiCol_TextDisabled)
                                         : urgency_color(domain::urgency(task, today));
                ImGui::TextColored(color, "%s", due_text(*task.due, today).c_str());
                ImGui::SameLine();
            }
            ImGui::TextUnformatted(task.title.c_str());
        }
        if (tasks.size() > 4) ImGui::TextDisabled("and %zu more", tasks.size() - 4);
    }
    ImGui::Separator();
    const bool station = element->type == domain::ElementType::RecipeStation;
    ImGui::TextDisabled("%s", edit_mode_ ? "Double-click for details"
                              : station  ? "Click to open the recipe library"
                                         : "Click for details · right-click for actions");
    ImGui::EndTooltip();
}

// --- Menus and dialogs --------------------------------------------------------

void FloorView::draw_context_menu() {
    if (!ImGui::BeginPopup(kContextPopup)) return;

    const domain::Element* element = context_element_ ? floor_.find(*context_element_) : nullptr;
    if (element) {
        // A copy: the actions can change the floor while the menu is drawn.
        const domain::Element copy = *element;
        ImGui::TextDisabled("%s", copy.name.c_str());
        ImGui::Separator();
        if (copy.type == domain::ElementType::RecipeStation) {
            if (ImGui::MenuItem("Open recipe library")) open_element(copy.id);
        }
        if (ImGui::MenuItem("Open details")) windows_.open(copy.id);
        draw_element_actions(copy);
        ImGui::Separator();
        if (ImGui::MenuItem("Delete...", nullptr, false, edit_mode_)) request_delete(copy.id);
        if (!edit_mode_) ImGui::SetItemTooltip("Turn on Edit layout to delete equipment.");
    } else {
        if (ImGui::BeginMenu("Add here")) {
            for (const domain::ElementTypeInfo& type : domain::element_types()) {
                if (ImGui::MenuItem(type.name.data())) add_element(type.type, context_point_);
            }
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("Fit to view")) fit_requested_ = true;
    }
    ImGui::Separator();
    ImGui::MenuItem("Edit layout", nullptr, &edit_mode_);
    ImGui::EndPopup();
}

void FloorView::draw_element_actions(const domain::Element& element) {
    if (element.type == domain::ElementType::Brewhouse) {
        ImGui::Separator();
        if (ImGui::MenuItem("Log a brew...")) dialogs_.log_brew(element.id);
        for (const domain::Brew& brew : batches_.pending_brews(element.id)) {
            const domain::Batch* batch = batches_.find_active(brew.batch_id);
            if (!batch) continue;
            ImGui::PushID(static_cast<int>(brew.id));
            if (ImGui::MenuItem(("Log knock-out of " + domain::label(*batch) + "...").c_str())) {
                dialogs_.knock_out(brew.id);
            }
            ImGui::PopID();
        }
    }

    if (domain::holds_beer(element.type)) {
        ImGui::Separator();
        if (const std::optional<app::VesselContents> held = batches_.contents(element.id)) {
            if (ImGui::MenuItem("Log reading...")) dialogs_.log_reading(held->batch_id, element.id);
            if (ImGui::MenuItem("Transfer...")) dialogs_.transfer(element.id);
            if (ImGui::MenuItem("Package...")) dialogs_.package(element.id);
            if (ImGui::MenuItem("Correct volume...")) dialogs_.correct_volume(element.id);
            if (ImGui::MenuItem("Dump...")) dialogs_.dump(element.id);
            if (ImGui::MenuItem("Open batch") && on_open_batch) on_open_batch(held->batch_id);
        } else {
            const domain::VesselCondition condition = batches_.condition(element.id);
            const auto set = [&](domain::VesselCondition to) { batches_.set_condition(element.id, to, domain::now()); };
            if (ImGui::MenuItem("Clean", nullptr, condition == domain::VesselCondition::Clean)) {
                set(domain::VesselCondition::Clean);
            }
            if (ImGui::MenuItem("Needs cleaning", nullptr, condition == domain::VesselCondition::Dirty)) {
                set(domain::VesselCondition::Dirty);
            }
            if (ImGui::MenuItem("Out of service", nullptr, condition == domain::VesselCondition::OutOfService)) {
                set(domain::VesselCondition::OutOfService);
            }
        }
    }

    // Notes go on the equipment's log; a recipe station has none.
    if (element.type == domain::ElementType::RecipeStation) return;
    const std::optional<app::VesselContents> held = batches_.contents(element.id);
    if (ImGui::MenuItem("Add note...")) {
        dialogs_.add_note(held ? std::optional<std::int64_t>(held->batch_id) : std::nullopt, element.id);
    }
}

void FloorView::draw_delete_confirmation() {
    // Requests can come from inside the context menu, where OpenPopup would
    // use the menu's ID scope, so the modal is opened here instead.
    if (open_delete_popup_) {
        ImGui::OpenPopup(kDeletePopup);
        open_delete_popup_ = false;
    }

    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, {0.5f, 0.5f});
    if (!ImGui::BeginPopupModal(kDeletePopup, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;

    if (const domain::Element* element = floor_.find(delete_id_)) {
        if (delete_blocker_) {
            // In use: it can't go until it's emptied.
            ImGui::Text("\"%s\" can't be deleted.", element->name.c_str());
            ImGui::TextDisabled("%s", delete_blocker_->c_str());
            ImGui::Spacing();
            if (ImGui::Button("OK") || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
        } else {
            ImGui::Text("Delete \"%s\" from the floor?", element->name.c_str());
            ImGui::TextDisabled("Its position, capacity, unit settings and tasks will be removed.");
            ImGui::Spacing();
            if (ImGui::Button("Delete")) {
                floor_.remove(delete_id_);
                tasks_.refresh();  // its tasks went with it
                if (selected_ == delete_id_) selected_.reset();
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
        }
    } else {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

// --- View and actions -------------------------------------------------------

void FloorView::zoom_at(ImVec2 screen, double factor) {
    // Keep the floor point under `screen` fixed while zooming.
    const FloorPoint anchor = to_world(screen);
    zoom_ = std::clamp(zoom_ * factor, kMinZoom, kMaxZoom);
    offset_x_ = anchor.x - (screen.x - canvas_pos_.x) / scale();
    offset_y_ = anchor.y - (screen.y - canvas_pos_.y) / scale();
}

void FloorView::fit_to_view() {
    const std::vector<domain::Element>& elements = floor_.elements();
    if (elements.empty()) {
        zoom_ = 1.0;
        offset_x_ = offset_y_ = -40.0;
        return;
    }

    double left = elements.front().rect.x, top = elements.front().rect.y;
    double right = left, bottom = top;
    for (const domain::Element& element : elements) {
        left = std::min(left, element.rect.x);
        top = std::min(top, element.rect.y);
        right = std::max(right, element.rect.x + element.rect.width);
        bottom = std::max(bottom, element.rect.y + element.rect.height);
    }

    constexpr double kMargin = 60.0;
    const double width = right - left + 2.0 * kMargin;
    const double height = bottom - top + 2.0 * kMargin;
    zoom_ = std::clamp(std::min(canvas_size_.x / (width * ui_scale_), canvas_size_.y / (height * ui_scale_)),
                       kMinZoom, kMaxZoom);
    offset_x_ = (left + right) / 2.0 - canvas_size_.x / (2.0 * scale());
    offset_y_ = (top + bottom) / 2.0 - canvas_size_.y / (2.0 * scale());
}

void FloorView::open_element(std::int64_t id) {
    const domain::Element* element = floor_.find(id);
    if (element && element->type == domain::ElementType::RecipeStation && on_open_recipes) {
        on_open_recipes();
    } else {
        windows_.open(id);
    }
}

void FloorView::add_element(domain::ElementType type, FloorPoint center) {
    selected_ = floor_.add(type, center.x, center.y);
    edit_mode_ = true;  // new equipment usually needs moving into place
}

void FloorView::request_delete(std::int64_t id) {
    delete_id_ = id;
    delete_blocker_ = floor_.removal_blocker(id);
    open_delete_popup_ = true;
}

void FloorView::save_view() {
    char text[96];
    std::snprintf(text, sizeof text, "%.2f %.2f %.4f", offset_x_, offset_y_, zoom_);
    settings_.set(kViewKey, text);
}

} // namespace ninkasi::ui
