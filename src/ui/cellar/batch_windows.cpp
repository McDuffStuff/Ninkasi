#include "ui/cellar/batch_windows.h"

#include "calc/common.h"
#include "ui/cellar/batch_views.h"
#include "ui/display.h"
#include "ui/widgets.h"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace ninkasi::ui {

namespace {

using calc::Measure;

constexpr const char* kOpenWindowsKey = "batches.open_windows";

constexpr ImGuiTableFlags kListFlags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                                       ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_PadOuterX;

std::string lowercase(std::string text) {
    for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

ImVec4 status_color(domain::BatchStatus status) {
    switch (status) {
    case domain::BatchStatus::Brewing:      return {0.80f, 0.47f, 0.20f, 1.0f};  // copper, like the brewhouse
    case domain::BatchStatus::Fermenting:   return {0.30f, 0.55f, 0.82f, 1.0f};  // fermenter blue
    case domain::BatchStatus::Conditioning: return {0.20f, 0.66f, 0.62f, 1.0f};  // brite teal
    case domain::BatchStatus::Packaged:     return kGoodColor;
    case domain::BatchStatus::Dumped:       return ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
    }
    return ImGui::GetStyleColorVec4(ImGuiCol_Text);
}

} // namespace

BatchWindows::BatchWindows(app::BatchService& batches, const app::FloorService& floor, CellarDialogs& dialogs,
                           data::SettingsRepository& settings, const calc::UnitPreferences& units,
                           const domain::CalcSettings& calc_settings, float ui_scale)
    : batches_(batches),
      floor_(floor),
      dialogs_(dialogs),
      settings_(settings),
      units_(units),
      calc_settings_(calc_settings),
      ui_scale_(ui_scale) {
    // Saved as comma-separated batch ids, e.g. "3,7".
    const std::string saved = settings_.get_or(kOpenWindowsKey, "");
    const char* cursor = saved.c_str();
    while (*cursor != '\0') {
        char* end = nullptr;
        const long long id = std::strtoll(cursor, &end, 10);
        if (end == cursor) break;
        windows_.push_back(Window{.batch_id = id});
        cursor = (*end == ',') ? end + 1 : end;
    }
}

void BatchWindows::show_list() {
    list_visible_ = true;
    list_focus_ = true;
}

void BatchWindows::open(std::int64_t batch_id) {
    for (Window& window : windows_) {
        if (window.batch_id == batch_id) {
            window.focus = true;
            return;
        }
    }
    windows_.push_back(Window{.batch_id = batch_id, .focus = true});
    save_open_windows();
}

void BatchWindows::draw() {
    if (list_visible_) draw_list();
    bool closed_any = false;
    for (std::size_t i = 0; i < windows_.size();) {
        if (draw_window(windows_[i])) {
            ++i;
        } else {
            windows_.erase(windows_.begin() + static_cast<std::ptrdiff_t>(i));
            closed_any = true;
        }
    }
    if (closed_any) save_open_windows();
}

// --- The batch list -------------------------------------------------------------

void BatchWindows::draw_list() {
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.6f));
    ImGui::SetNextWindowSize(ImVec2(940.0f, 480.0f) * ui_scale_, ImGuiCond_FirstUseEver);
    const bool raise = list_focus_;
    if (list_focus_) {
        ImGui::SetNextWindowFocus();
        list_focus_ = false;
    }
    const bool expanded = ImGui::Begin("Batches", &list_visible_);
    if (raise) raise_os_window();
    if (!expanded) {
        ImGui::End();
        return;
    }
    if (!list_loaded_ || list_revision_ != batches_.revision()) {
        list_ = batches_.list();
        list_revision_ = batches_.revision();
        list_loaded_ = true;
    }

    ImGui::SetNextItemWidth(220.0f * ui_scale_);
    ImGui::InputTextWithHint("##filter", "Search code, beer or style", &filter_);
    ImGui::SameLine();
    ImGui::Checkbox("Show finished batches", &show_finished_);
    ImGui::Spacing();

    if (list_.empty()) {
        wrapped_note("No batches yet. Start one from a brewhouse on the Brewery Floor: click it, then Start brew.");
        ImGui::End();
        return;
    }

    const calc::Unit gravity = units_.unit_for(Measure::Gravity);
    const calc::Unit volume = units_.unit_for(Measure::Volume);
    const std::string needle = lowercase(filter_);
    if (ImGui::BeginTable("##batches", 9, kListFlags | ImGuiTableFlags_ScrollY)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Batch");
        ImGui::TableSetupColumn("Beer", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Status");
        ImGui::TableSetupColumn("Brewed");
        ImGui::TableSetupColumn("Where");
        ImGui::TableSetupColumn("Volume");
        ImGui::TableSetupColumn("OG");
        ImGui::TableSetupColumn("Gravity");
        ImGui::TableSetupColumn("ABV");
        ImGui::TableHeadersRow();

        for (const data::BatchSummary& batch : list_) {
            if (!show_finished_ && !domain::is_active(batch.status)) continue;
            if (!needle.empty() && lowercase(batch.code + " " + batch.name + " " + batch.style).find(needle) ==
                                       std::string::npos) {
                continue;
            }
            ImGui::PushID(static_cast<int>(batch.id));
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            if (ImGui::Selectable(domain::batch_label(batch.code, "").c_str(), false,
                                  ImGuiSelectableFlags_SpanAllColumns)) {
                open(batch.id);
            }
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(batch.name.c_str());
            if (!batch.style.empty()) {
                ImGui::SameLine();
                ImGui::TextDisabled("%s", batch.style.c_str());
            }
            ImGui::TableNextColumn();
            ImGui::TextColored(status_color(batch.status), "%s", domain::name(batch.status).data());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(batch.brewed_at ? date_text(domain::local_date(*batch.brewed_at)).c_str() : "");
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(where_text(batch.id, false).c_str());
            ImGui::TableNextColumn();
            if (domain::is_active(batch.status) && batch.status != domain::BatchStatus::Brewing) {
                ImGui::TextUnformatted(calc::format(batches_.volume(batch.id), volume).c_str());
            }
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(batch.og ? calc::format(*batch.og, gravity).c_str() : "");
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(batch.latest_gravity ? calc::format(*batch.latest_gravity, gravity).c_str() : "");
            ImGui::TableNextColumn();
            if (batch.og && batch.latest_gravity) {
                const double abv = calc::abv(*batch.og, *batch.latest_gravity, calc_settings_.abv_formula);
                ImGui::TextUnformatted(number_text(abv * 100.0, 1, " %").c_str());
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::End();
}

// --- Batch windows ----------------------------------------------------------------

void BatchWindows::refresh(Window& window) {
    window.finished.reset();
    if (!batches_.find_active(window.batch_id)) window.finished = batches_.load(window.batch_id);
    window.log = batches_.batch_activity(window.batch_id);
    window.ledger = batches_.volume_history(window.batch_id);
    window.used.clear();
    const domain::Batch* batch = window.finished ? &*window.finished : batches_.find_active(window.batch_id);
    if (batch) {
        for (const domain::Brew& brew : batch->brews) {
            if (!brew.in_progress()) window.used[brew.id] = batches_.ingredients(brew.id);
        }
    }
    window.revision = batches_.revision();
    window.loaded = true;
}

bool BatchWindows::draw_window(Window& window) {
    if (!window.loaded || window.revision != batches_.revision()) refresh(window);
    // Drawn from a copy: buttons in the window can change the batch service's
    // batches while the window is still being drawn.
    const domain::Batch* found = batches_.find_active(window.batch_id);
    if (!found && window.finished) found = &*window.finished;
    if (!found) return false;  // deleted (a cancelled first brew)
    const domain::Batch copy = *found;
    const domain::Batch* batch = &copy;

    const float stagger = static_cast<float>(window.batch_id % 6) * 28.0f * ui_scale_;
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter() + ImVec2(stagger, stagger), ImGuiCond_FirstUseEver,
                            ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(820.0f, 600.0f) * ui_scale_, ImGuiCond_FirstUseEver);
    const bool raise = window.focus;
    if (window.focus) {
        ImGui::SetNextWindowFocus();
        window.focus = false;
    }

    bool open = true;
    const std::string title = domain::label(*batch) + "###batch_" + std::to_string(batch->id);
    const bool expanded = ImGui::Begin(title.c_str(), &open);
    if (raise) raise_os_window();
    if (expanded) {
        const domain::RecipeStats predicted = domain::calculate(batch->recipe, calc_settings_);
        if (ImGui::BeginTabBar("##tabs")) {
            if (ImGui::BeginTabItem("Overview")) {
                draw_overview(*batch, predicted);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Fermentation")) {
                draw_fermentation_chart(*batch, predicted.fg, units_, 280.0f * ui_scale_);
                ImGui::Spacing();
                if (const std::optional<std::int64_t> remove = draw_readings_table(*batch, units_)) {
                    batches_.delete_reading(*remove);
                }
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Ingredients")) {
                for (std::size_t i = 0; i < batch->brews.size(); ++i) {
                    const domain::Brew& brew = batch->brews[i];
                    if (brew.in_progress()) continue;
                    if (batch->brews.size() > 1) {
                        ImGui::SeparatorText(("Brew " + std::to_string(i + 1)).c_str());
                    }
                    ImGui::PushID(static_cast<int>(brew.id));
                    const auto used = window.used.find(brew.id);
                    draw_ingredients_used(used != window.used.end() ? used->second
                                                                    : std::vector<domain::IngredientUse>{},
                                          units_);
                    ImGui::PopID();
                }
                if (!batch->brewed_at()) wrapped_note("The ingredients are confirmed at knock-out.");
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Volumes")) {
                draw_ledger(window);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Recipe")) {
                draw_recipe(*batch, predicted);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Log")) {
                draw_activity(window.log, units_);
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }
    ImGui::End();
    return open;
}

void BatchWindows::draw_overview(const domain::Batch& batch, const domain::RecipeStats& predicted) {
    ImGui::TextUnformatted(batch.name().c_str());
    if (!batch.recipe.style.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", batch.recipe.style.c_str());
    }
    ImGui::TextColored(status_color(batch.status), "%s", domain::name(batch.status).data());
    ImGui::SameLine();
    if (batch.finished_at) {
        ImGui::TextDisabled("· finished %s", date_text(domain::local_date(*batch.finished_at)).c_str());
    } else {
        ImGui::TextDisabled("· %s", batch_age_text(batch).c_str());
        const std::string where = where_text(batch.id, true);
        if (!where.empty()) {
            ImGui::SameLine();
            ImGui::TextDisabled("· in %s", where.c_str());
        }
    }
    ImGui::Spacing();
    draw_batch_figures(batch, predicted, units_, calc_settings_.abv_formula);

    if (domain::is_active(batch.status)) {
        ImGui::Spacing();
        if (ImGui::Button("Log reading")) dialogs_.log_reading(batch.id, std::nullopt);
        ImGui::SameLine();
        if (ImGui::Button("Add note")) dialogs_.add_note(batch.id, std::nullopt);
        if (batch.status == domain::BatchStatus::Fermenting || batch.status == domain::BatchStatus::Conditioning) {
            ImGui::SameLine();
            const bool fermenting = batch.status == domain::BatchStatus::Fermenting;
            if (ImGui::Button(fermenting ? "Start conditioning" : "Back to fermenting")) {
                batches_.set_status(batch.id,
                                    fermenting ? domain::BatchStatus::Conditioning : domain::BatchStatus::Fermenting,
                                    domain::now());
            }
            ImGui::SetItemTooltip("Moving the beer into a brite tank starts conditioning by itself.");
        }
    }

    ImGui::SeparatorText("Brews");
    draw_brews(batch);
}

void BatchWindows::draw_brews(const domain::Batch& batch) {
    const calc::Unit volume = units_.unit_for(Measure::Volume);
    const calc::Unit gravity = units_.unit_for(Measure::Gravity);
    if (!ImGui::BeginTable("##brews", 6, kListFlags)) return;
    ImGui::TableSetupColumn("Started");
    ImGui::TableSetupColumn("On");
    ImGui::TableSetupColumn("Knocked out");
    ImGui::TableSetupColumn("Into");
    ImGui::TableSetupColumn("Volume");
    ImGui::TableSetupColumn("OG");
    ImGui::TableHeadersRow();
    for (const domain::Brew& brew : batch.brews) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(time_text(brew.started_at).c_str());
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(element_name(brew.brewhouse_id).c_str());
        ImGui::TableNextColumn();
        if (brew.knocked_out_at) {
            ImGui::TextUnformatted(time_text(*brew.knocked_out_at).c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(element_name(brew.vessel_id).c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(calc::format(brew.volume_l, volume).c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(brew.og ? calc::format(*brew.og, gravity).c_str() : "--");
        } else {
            ImGui::TextColored(kWarningColor, "Knock-out to log");
            ImGui::TableNextColumn();
            ImGui::PushID(static_cast<int>(brew.id));
            if (ImGui::SmallButton("Log knock-out...")) dialogs_.knock_out(brew.id);
            ImGui::PopID();
        }
        if (!brew.notes.empty()) ImGui::SetItemTooltip("%s", brew.notes.c_str());
    }
    ImGui::EndTable();
}

void BatchWindows::draw_ledger(const Window& window) {
    if (window.ledger.empty()) {
        wrapped_note("Volumes are recorded from knock-out on.");
        return;
    }
    const calc::Unit volume = units_.unit_for(Measure::Volume);
    if (!ImGui::BeginTable("##ledger", 4, kListFlags | ImGuiTableFlags_ScrollY)) return;
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("When");
    ImGui::TableSetupColumn("Vessel");
    ImGui::TableSetupColumn("What", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Change");
    ImGui::TableHeadersRow();
    for (const domain::VolumeEvent& event : window.ledger) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextDisabled("%s", time_text(event.at).c_str());
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(element_name(event.vessel_id).c_str());
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(domain::name(event.reason).data());
        ImGui::TableNextColumn();
        const std::string change = (event.volume_l > 0.0 ? "+" : "") + calc::format(event.volume_l, volume);
        ImGui::TextUnformatted(change.c_str());
    }
    ImGui::EndTable();
}

void BatchWindows::draw_recipe(const domain::Batch& batch, const domain::RecipeStats& predicted) {
    const domain::Recipe& recipe = batch.recipe;
    const calc::Unit volume = units_.unit_for(Measure::Volume);
    const calc::Unit gravity = units_.unit_for(Measure::Gravity);
    const calc::Unit mass = units_.unit_for(Measure::Mass);
    const calc::Unit hop_mass = units_.unit_for(Measure::HopMass);
    wrapped_note("The recipe as it was when this batch was brewed. Later changes to the recipe don't affect it.");
    ImGui::Spacing();

    if (ImGui::BeginTable("##recipe", 2, ImGuiTableFlags_SizingFixedFit)) {
        table_row("Batch size");
        ImGui::TextUnformatted(calc::format(recipe.batch_volume_l, volume).c_str());
        table_row("Efficiency");
        ImGui::TextUnformatted(number_text(recipe.efficiency * 100.0, 0, " %").c_str());
        table_row("Predicted");
        ImGui::Text("OG %s · FG %s · %s ABV · %s IBU", calc::format(predicted.og, gravity).c_str(),
                    calc::format(predicted.fg, gravity).c_str(), number_text(predicted.abv * 100.0, 1, " %").c_str(),
                    number_text(predicted.ibu, 0).c_str());
        ImGui::EndTable();
    }

    if (!recipe.fermentables.empty() && ImGui::BeginTable("##fermentables", 2, kListFlags)) {
        ImGui::TableSetupColumn("Fermentable", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Amount");
        ImGui::TableHeadersRow();
        for (const domain::FermentableAddition& addition : recipe.fermentables) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(addition.fermentable.name.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(calc::format(addition.mass_kg, mass).c_str());
        }
        ImGui::EndTable();
    }
    if (!recipe.hops.empty() && ImGui::BeginTable("##hops", 3, kListFlags)) {
        ImGui::TableSetupColumn("Hop", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Use");
        ImGui::TableSetupColumn("Amount");
        ImGui::TableHeadersRow();
        for (const domain::HopAddition& addition : recipe.hops) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(addition.hop.name.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(domain::name(addition.use).data());
            if (addition.use == calc::HopUse::Boil || addition.use == calc::HopUse::Whirlpool) {
                ImGui::SameLine();
                ImGui::TextDisabled("%s min", number_text(addition.time_min, 0).c_str());
            }
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(calc::format(addition.mass_kg, hop_mass).c_str());
        }
        ImGui::EndTable();
    }
    for (const domain::YeastAddition& addition : recipe.yeasts) {
        ImGui::Text("Yeast: %s", addition.yeast.name.c_str());
    }
    if (!recipe.notes.empty()) {
        ImGui::SeparatorText("Notes");
        ImGui::TextWrapped("%s", recipe.notes.c_str());
    }
}

std::string BatchWindows::where_text(std::int64_t batch_id, bool with_volumes) const {
    const calc::Unit volume = units_.unit_for(Measure::Volume);
    std::string text;
    for (const auto& [vessel_id, volume_l] : batches_.locations(batch_id)) {
        if (!text.empty()) text += ", ";
        text += element_name(vessel_id);
        if (with_volumes) text += " (" + calc::format(volume_l, volume) + ")";
    }
    return text;
}

std::string BatchWindows::element_name(std::optional<std::int64_t> element_id) const {
    const domain::Element* element = element_id ? floor_.find(*element_id) : nullptr;
    return element ? element->name : std::string("--");
}

void BatchWindows::save_open_windows() {
    std::string ids;
    for (const Window& window : windows_) {
        if (!ids.empty()) ids += ',';
        ids += std::to_string(window.batch_id);
    }
    settings_.set(kOpenWindowsKey, ids);
}

} // namespace ninkasi::ui
