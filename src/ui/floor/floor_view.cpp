#include "ui/floor/floor_view.h"

#include "ui/element_style.h"
#include "ui/elements/element_windows.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace ninkasi::ui {

namespace {

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

} // namespace

FloorView::FloorView(app::FloorService& floor, ElementWindows& windows, data::SettingsRepository& settings,
                     const calc::UnitPreferences& units, float ui_scale)
    : floor_(floor), windows_(windows), settings_(settings), units_(units), ui_scale_(ui_scale) {
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

std::optional<std::int64_t> FloorView::hit_test(ImVec2 screen) const {
    // Later elements are drawn on top, so test them first.
    const std::vector<domain::Element>& elements = floor_.elements();
    for (auto it = elements.rbegin(); it != elements.rend(); ++it) {
        const domain::Rect& rect = displayed_rect(*it);
        const ImVec2 min = to_screen(rect.x, rect.y);
        const ImVec2 max = to_screen(rect.x + rect.width, rect.y + rect.height);
        if (screen.x >= min.x && screen.x < max.x && screen.y >= min.y && screen.y < max.y) return it->id;
    }
    return std::nullopt;
}

bool FloorView::over_resize_handle(const domain::Rect& rect, ImVec2 screen) const {
    const ImVec2 corner = to_screen(rect.x + rect.width, rect.y + rect.height);
    const float handle = 12.0f * ui_scale_;
    return screen.x >= corner.x - handle && screen.x <= corner.x && screen.y >= corner.y - handle &&
           screen.y <= corner.y;
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
                                         : "Click equipment for details · drag to pan · scroll to zoom");
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
                drag_ = over_resize_handle(element->rect, io.MousePos) ? Drag::Resize : Drag::Move;
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
            if (edit_mode_ && element && over_resize_handle(element->rect, io.MousePos)) {
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
            drag_rect_.width = std::max(domain::kMinElementSize, drag_start_rect_.width + dx);
            drag_rect_.height = std::max(domain::kMinElementSize, drag_start_rect_.height + dy);
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
        windows_.open(*pressed_element_);
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
    const domain::Rect& rect = displayed_rect(element);
    const ImVec2 min = to_screen(rect.x, rect.y);
    const ImVec2 max = to_screen(rect.x + rect.width, rect.y + rect.height);
    const ImVec2 canvas_end = canvas_pos_ + canvas_size_;
    if (max.x < canvas_pos_.x || max.y < canvas_pos_.y || min.x > canvas_end.x || min.y > canvas_end.y) return;

    const domain::ElementTypeInfo& type = domain::info(element.type);
    const bool selected = selected_ == element.id;
    const float px = static_cast<float>(scale());
    const ImVec4 accent = element_accent(element.type);
    const ImVec4& surface = style_color(ImGuiCol_WindowBg);
    const ImVec4& border = style_color(ImGuiCol_Border);
    const float rounding = 6.0f * px;

    // Body, then an accent strip along the top (clipped so it keeps the
    // body's rounded corners).
    draw_list->AddRectFilled(min, max, to_u32(mix(surface, accent, 0.16f)), rounding);
    draw_list->PushClipRect(min, {max.x, min.y + 5.0f * px}, true);
    draw_list->AddRectFilled(min, max, to_u32(accent), rounding);
    draw_list->PopClipRect();

    const ImVec4 outline = selected ? accent : hovered ? mix(border, accent, 0.6f) : border;
    const float thickness = (selected ? 2.5f : hovered ? 1.75f : 1.0f) * ui_scale_;
    draw_list->AddRect(min, max, to_u32(outline), rounding, ImDrawFlags_None, thickness);

    // Vessels get a level gauge down the right side; it fills once batches exist.
    if (type.is_vessel) {
        const ImVec2 gauge_min(max.x - 16.0f * px, min.y + 14.0f * px);
        const ImVec2 gauge_max(max.x - 8.0f * px, max.y - 10.0f * px);
        if (gauge_max.y - gauge_min.y > 6.0f * px) {
            draw_list->AddRect(gauge_min, gauge_max, to_u32(mix(surface, accent, 0.65f)), 3.0f * px, ImDrawFlags_None,
                               ui_scale_);
        }
    }

    // Labels scale with the zoom and disappear when too small to read.
    const float font_size = ImGui::GetFontSize() * static_cast<float>(zoom_);
    if (font_size >= 7.0f) {
        ImFont* font = ImGui::GetFont();
        const ImU32 text = ImGui::GetColorU32(ImGuiCol_Text);
        const ImU32 muted = ImGui::GetColorU32(ImGuiCol_TextDisabled);
        const ImVec2 pad(8.0f * px, 10.0f * px);
        const float small = font_size * 0.85f;

        draw_list->PushClipRect(min, max, true);
        draw_list->AddText(font, font_size, min + pad, text, element.name.c_str());
        // The type, unless the name already says it ("Keg washer", "Fermenter 2").
        if (!element.name.starts_with(type.name)) {
            draw_list->AddText(font, small, {min.x + pad.x, min.y + pad.y + font_size * 1.2f}, muted, type.name.data());
        }
        if (type.is_vessel && element.capacity_l) {
            const calc::Unit unit =
                units_.with_overrides(element.unit_overrides).unit_for(calc::Measure::Volume);
            const std::string capacity = calc::format(*element.capacity_l, unit);
            draw_list->AddText(font, small, {min.x + pad.x, max.y - pad.y - small}, muted, capacity.c_str());
        }
        draw_list->PopClipRect();
    }

    if (selected && edit_mode_) {
        const float handle = 9.0f * ui_scale_;
        draw_list->AddRectFilled({max.x - handle, max.y - handle}, max, to_u32(accent), 2.0f * ui_scale_);
    }
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

    ImGui::BeginTooltip();
    ImGui::TextUnformatted(element->name.c_str());
    ImGui::TextDisabled("%s", type.name.data());
    if (type.is_vessel) {
        ImGui::Separator();
        const std::string capacity = element->capacity_l
                                         ? calc::format(*element->capacity_l, units.unit_for(calc::Measure::Volume))
                                         : std::string("not set");
        ImGui::Text("Capacity: %s", capacity.c_str());
        ImGui::TextUnformatted("Contents: empty");
    }
    ImGui::Separator();
    ImGui::TextDisabled("%s", edit_mode_ ? "Double-click for details" : "Click for details");
    ImGui::EndTooltip();
}

void FloorView::draw_context_menu() {
    if (!ImGui::BeginPopup(kContextPopup)) return;

    const domain::Element* element = context_element_ ? floor_.find(*context_element_) : nullptr;
    if (element) {
        ImGui::TextDisabled("%s", element->name.c_str());
        ImGui::Separator();
        if (ImGui::MenuItem("Open details")) windows_.open(element->id);
        if (ImGui::MenuItem("Delete...", nullptr, false, edit_mode_)) request_delete(element->id);
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
        ImGui::Text("Delete \"%s\" from the floor?", element->name.c_str());
        ImGui::TextDisabled("Its position, capacity and unit settings will be removed.");
        ImGui::Spacing();
        if (ImGui::Button("Delete")) {
            floor_.remove(delete_id_);
            if (selected_ == delete_id_) selected_.reset();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
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

void FloorView::add_element(domain::ElementType type, FloorPoint center) {
    selected_ = floor_.add(type, center.x, center.y);
    edit_mode_ = true;  // new equipment usually needs moving into place
}

void FloorView::request_delete(std::int64_t id) {
    delete_id_ = id;
    open_delete_popup_ = true;
}

void FloorView::save_view() {
    char text[96];
    std::snprintf(text, sizeof text, "%.2f %.2f %.4f", offset_x_, offset_y_, zoom_);
    settings_.set(kViewKey, text);
}

} // namespace ninkasi::ui
