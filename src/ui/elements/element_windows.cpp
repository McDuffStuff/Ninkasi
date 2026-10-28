#include "ui/elements/element_windows.h"

#include "calc/common.h"
#include "ui/cellar/batch_views.h"
#include "ui/display.h"
#include "ui/element_style.h"
#include "ui/widgets.h"

#include <imgui_stdlib.h>

#include <algorithm>
#include <cstdlib>
#include <string_view>

namespace ninkasi::ui {

namespace {

using calc::Measure;

constexpr const char* kOpenWindowsKey = "floor.open_elements";

constexpr ImGuiTableFlags kListFlags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                                       ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_PadOuterX;

void label_cell(const char* label) {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::TableNextColumn();
}

ImVec4 condition_color(domain::VesselCondition condition) {
    switch (condition) {
    case domain::VesselCondition::Clean:        return kGoodColor;
    case domain::VesselCondition::Dirty:        return kWarningColor;
    case domain::VesselCondition::OutOfService: return kErrorColor;
    }
    return kGoodColor;
}

// A swatch of the beer's color, one text line high.
void beer_swatch(double srm) {
    const float size = ImGui::GetTextLineHeight();
    ImGui::ColorButton("##beer", beer_color(srm), ImGuiColorEditFlags_NoTooltip, ImVec2(size, size));
}

} // namespace

ElementWindows::ElementWindows(app::FloorService& floor, app::BatchService& batches, const app::TaskService& tasks,
                               CellarDialogs& dialogs, TaskViews& task_views, InventoryViews& inventory_views,
                               PackagingViews& packaging_views, data::SettingsRepository& settings,
                               const calc::UnitPreferences& units, const domain::CalcSettings& calc_settings,
                               float ui_scale)
    : floor_(floor),
      batches_(batches),
      tasks_(tasks),
      dialogs_(dialogs),
      task_views_(task_views),
      inventory_views_(inventory_views),
      packaging_views_(packaging_views),
      settings_(settings),
      units_(units),
      calc_settings_(calc_settings),
      ui_scale_(ui_scale) {
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
    // A recipe station only has its name; everything else has tabs of detail.
    const ImVec2 size = element.type == domain::ElementType::RecipeStation ? ImVec2(480.0f, 200.0f)
                                                                           : ImVec2(640.0f, 540.0f);
    ImGui::SetNextWindowSize(size * ui_scale_, ImGuiCond_FirstUseEver);

    const bool raise = window.focus;
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
    const bool expanded = ImGui::Begin(title.c_str(), &open, flags);
    if (raise) raise_os_window();
    if (expanded) {
        if (!window.loaded || window.batch_revision != batches_.revision() ||
            window.task_revision != tasks_.revision()) {
            refresh(window);
        }
        const calc::UnitPreferences units = units_.with_overrides(element.unit_overrides);
        draw_window_controls(window, element);
        if (element.type == domain::ElementType::RecipeStation) {
            // Just its name: the recipe library is what it's for.
            draw_overview_tab(window, element);
        } else if (ImGui::BeginTabBar("##tabs")) {
            // Inventory opens on the stock.
            const bool inventory = element.type == domain::ElementType::Inventory;
            if (inventory && ImGui::BeginTabItem("Stock")) {
                inventory_views_.draw_stock();
                ImGui::EndTabItem();
            }
            if (inventory && ImGui::BeginTabItem("Invoices")) {
                inventory_views_.draw_invoices();
                ImGui::EndTabItem();
            }
            if (inventory && ImGui::BeginTabItem("In and out")) {
                inventory_views_.draw_log();
                ImGui::EndTabItem();
            }

            // The keg cleaner opens on its dirty kegs.
            if (element.type == domain::ElementType::KegCleaner) {
                if (ImGui::BeginTabItem("Dirty kegs")) {
                    packaging_views_.draw_dirty_kegs();
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Clean kegs")) {
                    packaging_views_.draw_clean_kegs();
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Keg log")) {
                    packaging_views_.draw_keg_history();
                    ImGui::EndTabItem();
                }
            }
            if (ImGui::BeginTabItem("Overview")) {
                draw_overview_tab(window, element);
                ImGui::EndTabItem();
            }

            // A tank's batch: its readings over time.
            const std::optional<app::VesselContents> held = batches_.contents(element.id);
            if (const domain::Batch* found = held ? batches_.find_active(held->batch_id) : nullptr) {
                const domain::Batch batch = *found;  // deleting a reading reloads the service's batches
                if (ImGui::BeginTabItem("Fermentation")) {
                    const domain::RecipeStats predicted = domain::calculate(batch.recipe, calc_settings_);
                    draw_fermentation_chart(batch, predicted.fg, units, 260.0f * ui_scale_);
                    ImGui::Spacing();
                    if (const std::optional<std::int64_t> remove = draw_readings_table(batch, units)) {
                        batches_.delete_reading(*remove);
                    }
                    ImGui::EndTabItem();
                }
            }

            // A brewhouse: what it has brewed.
            if (element.type == domain::ElementType::Brewhouse) {
                if (ImGui::BeginTabItem("History")) {
                    draw_history_tab(window, units);
                    ImGui::EndTabItem();
                }
            }

            if (ImGui::BeginTabItem("Tasks")) {
                task_views_.draw_element_tasks(element.id);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Log")) {
                if (ImGui::SmallButton("Add note")) dialogs_.add_note(std::nullopt, element.id);
                draw_activity(window.log, units);
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

// --- Overview ---------------------------------------------------------------

void ElementWindows::draw_overview_tab(Window& window, const domain::Element& element) {
    const calc::UnitPreferences units = units_.with_overrides(element.unit_overrides);
    switch (element.type) {
    case domain::ElementType::Brewhouse:
        draw_brewhouse_status(element);
        break;
    case domain::ElementType::Fermenter:
    case domain::ElementType::BriteTank:
        draw_vessel_status(element, units);
        break;
    case domain::ElementType::RecipeStation:
        if (ImGui::Button("Open recipe library") && on_open_recipes) on_open_recipes();
        ImGui::SameLine();
        ImGui::TextDisabled("Design recipes and manage ingredients");
        break;
    default:
        break;
    }
    ImGui::SeparatorText("Equipment");
    draw_equipment_settings(window, element);
}

void ElementWindows::draw_equipment_settings(Window& window, const domain::Element& element) {
    const domain::ElementTypeInfo& type = domain::info(element.type);
    const calc::UnitPreferences units = units_.with_overrides(element.unit_overrides);

    if (!ImGui::BeginTable("##equipment", 2, ImGuiTableFlags_SizingStretchProp)) return;
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
        if (input_quantity("##capacity", capacity_l, units.unit_for(Measure::Volume))) {
            floor_.set_capacity(element.id, capacity_l > 0.0 ? std::optional<double>(capacity_l) : std::nullopt);
        }
    }
    ImGui::EndTable();
}

void ElementWindows::draw_vessel_status(const domain::Element& element, const calc::UnitPreferences& units) {
    const calc::Unit volume = units.unit_for(Measure::Volume);
    const std::optional<app::VesselContents> held = batches_.contents(element.id);
    const domain::Batch* found = held ? batches_.find_active(held->batch_id) : nullptr;

    if (!found) {
        // Empty: whether it's ready for beer, and buttons to change that.
        const domain::VesselCondition condition = batches_.condition(element.id);
        ImGui::TextUnformatted("Empty");
        ImGui::SameLine();
        ImGui::TextColored(condition_color(condition), "· %s", domain::name(condition).data());
        ImGui::Spacing();
        const auto set = [&](domain::VesselCondition to) { batches_.set_condition(element.id, to, domain::now()); };
        if (condition == domain::VesselCondition::Clean) {
            if (ImGui::Button("Needs cleaning")) set(domain::VesselCondition::Dirty);
            ImGui::SameLine();
            if (ImGui::Button("Out of service")) set(domain::VesselCondition::OutOfService);
            ImGui::SetItemTooltip("Under repair or otherwise unavailable: nothing can be put in it");
        } else if (ImGui::Button(condition == domain::VesselCondition::Dirty ? "Mark clean" : "Back in service")) {
            set(domain::VesselCondition::Clean);
        }
        return;
    }

    // Drawn from a copy: the buttons below can reload the service's batches.
    const domain::Batch batch = *found;
    const double contents_l = held->volume_l;
    const domain::RecipeStats predicted = domain::calculate(batch.recipe, calc_settings_);

    beer_swatch(predicted.srm);
    ImGui::SameLine();
    ImGui::TextUnformatted(domain::label(batch).c_str());
    if (!batch.recipe.style.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", batch.recipe.style.c_str());
    }
    ImGui::TextDisabled("%s · %s", domain::name(batch.status).data(), batch_age_text(batch).c_str());

    // How full the tank is.
    std::string fill = calc::format(contents_l, volume);
    float fraction = 1.0f;
    if (element.capacity_l && *element.capacity_l > 0.0) {
        fraction = static_cast<float>(std::clamp(contents_l / *element.capacity_l, 0.0, 1.0));
        fill += " of " + calc::format(*element.capacity_l, volume);
    }
    ImGui::ProgressBar(fraction, ImVec2(-FLT_MIN, 0.0f), fill.c_str());
    ImGui::Spacing();
    draw_batch_figures(batch, predicted, units, calc_settings_.abv_formula);
    ImGui::Spacing();

    if (ImGui::Button("Log reading")) dialogs_.log_reading(batch.id, element.id);
    ImGui::SameLine();
    if (ImGui::Button("Transfer...")) dialogs_.transfer(element.id);
    ImGui::SameLine();
    if (ImGui::Button("Package...")) dialogs_.package(element.id);
    ImGui::SameLine();
    if (ImGui::Button("Open batch") && on_open_batch) on_open_batch(batch.id);

    if (ImGui::SmallButton("Add note")) dialogs_.add_note(batch.id, element.id);
    ImGui::SameLine();
    if (ImGui::SmallButton("Correct volume...")) dialogs_.correct_volume(element.id);
    ImGui::SameLine();
    if (ImGui::SmallButton("Dump...")) dialogs_.dump(element.id);
    if (batch.status == domain::BatchStatus::Fermenting || batch.status == domain::BatchStatus::Conditioning) {
        ImGui::SameLine();
        const bool fermenting = batch.status == domain::BatchStatus::Fermenting;
        if (ImGui::SmallButton(fermenting ? "Start conditioning" : "Back to fermenting")) {
            batches_.set_status(batch.id,
                                fermenting ? domain::BatchStatus::Conditioning : domain::BatchStatus::Fermenting,
                                domain::now());
        }
    }
}

void ElementWindows::draw_brewhouse_status(const domain::Element& element) {
    if (ImGui::Button("Log a brew...")) dialogs_.log_brew(element.id);
    ImGui::SameLine();
    ImGui::TextDisabled("Record a brew, with its knock-out or with the knock-out to follow");

    // Brews logged without their knock-out, oldest first.
    const std::vector<domain::Brew> waiting = batches_.pending_brews(element.id);
    if (waiting.empty()) return;
    ImGui::SeparatorText("Waiting for knock-out");
    if (!ImGui::BeginTable("##waiting", 3, kListFlags)) return;
    ImGui::TableSetupColumn("Batch", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Brewed");
    ImGui::TableSetupColumn("##actions");
    for (const domain::Brew& brew : waiting) {
        const domain::Batch* batch = batches_.find_active(brew.batch_id);
        if (!batch) continue;
        ImGui::PushID(static_cast<int>(brew.id));
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(domain::label(*batch).c_str());
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(time_text(brew.started_at).c_str());
        ImGui::TableNextColumn();
        if (ImGui::SmallButton("Log knock-out...")) dialogs_.knock_out(brew.id);
        ImGui::SameLine();
        if (ImGui::SmallButton("Delete...")) dialogs_.delete_brew(brew.id);
        ImGui::PopID();
    }
    ImGui::EndTable();
}

// --- Brewhouse history ------------------------------------------------------------

void ElementWindows::draw_history_tab(const Window& window, const calc::UnitPreferences& units) {
    if (window.history.empty()) {
        wrapped_note("Brews made on this brewhouse will be listed here.");
        return;
    }
    const calc::Unit volume = units.unit_for(Measure::Volume);
    const calc::Unit gravity = units.unit_for(Measure::Gravity);
    if (!ImGui::BeginTable("##history", 5, kListFlags | ImGuiTableFlags_ScrollY)) return;
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Started");
    ImGui::TableSetupColumn("Batch", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Volume");
    ImGui::TableSetupColumn("OG");
    ImGui::TableSetupColumn("Into");
    ImGui::TableHeadersRow();
    for (const data::BrewSummary& entry : window.history) {
        ImGui::PushID(static_cast<int>(entry.brew.id));
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(time_text(entry.brew.started_at).c_str());
        ImGui::TableNextColumn();
        if (ImGui::Selectable(domain::batch_label(entry.code, entry.name).c_str(), false,
                              ImGuiSelectableFlags_SpanAllColumns) &&
            on_open_batch) {
            on_open_batch(entry.brew.batch_id);
        }
        ImGui::TableNextColumn();
        if (!entry.brew.in_progress()) ImGui::TextUnformatted(calc::format(entry.brew.volume_l, volume).c_str());
        ImGui::TableNextColumn();
        if (entry.brew.og) ImGui::TextUnformatted(calc::format(*entry.brew.og, gravity).c_str());
        ImGui::TableNextColumn();
        if (entry.brew.in_progress()) {
            ImGui::TextColored(kWarningColor, "Knock-out to log");
        } else if (const domain::Element* vessel = entry.brew.vessel_id ? floor_.find(*entry.brew.vessel_id) : nullptr) {
            ImGui::TextUnformatted(vessel->name.c_str());
        }
        ImGui::PopID();
    }
    ImGui::EndTable();
}

// --- Units ----------------------------------------------------------------------

void ElementWindows::draw_units_tab(const domain::Element& element) {
    wrapped_note("Units for this equipment only. \"Brewery default\" follows the Units menu.");
    ImGui::Spacing();
    if (!ImGui::BeginTable("##units", 2, ImGuiTableFlags_SizingStretchProp)) return;
    ImGui::TableSetupColumn("##measure", ImGuiTableColumnFlags_WidthFixed, 120.0f * ui_scale_);
    ImGui::TableSetupColumn("##unit", ImGuiTableColumnFlags_WidthStretch);

    for (calc::Measure measure : calc::kAllMeasures) {
        label_cell(calc::name(measure).data());

        const std::optional<calc::Unit> chosen = element.unit_overrides[calc::index_of(measure)];
        const std::string default_label =
            "Brewery default (" + std::string(calc::symbol(units_.unit_for(measure))) + ")";
        const std::string preview = chosen ? unit_label(*chosen) : default_label;

        ImGui::PushID(static_cast<int>(calc::index_of(measure)));
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::BeginCombo("##unit", preview.c_str())) {
            if (ImGui::Selectable(default_label.c_str(), !chosen)) {
                floor_.set_unit_override(element.id, measure, std::nullopt);
            }
            for (calc::Unit unit : calc::units_of(measure)) {
                if (ImGui::Selectable(unit_label(unit).c_str(), chosen == unit)) {
                    floor_.set_unit_override(element.id, measure, unit);
                }
            }
            ImGui::EndCombo();
        }
        ImGui::PopID();
    }
    ImGui::EndTable();
}

// --- Window management ------------------------------------------------------------

void ElementWindows::refresh(Window& window) {
    window.log = batches_.element_activity(window.element_id);
    window.history.clear();
    const domain::Element* element = floor_.find(window.element_id);
    if (element && element->type == domain::ElementType::Brewhouse) {
        window.history = batches_.brewhouse_history(window.element_id);
    }
    window.batch_revision = batches_.revision();
    window.task_revision = tasks_.revision();
    window.loaded = true;
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
