#include "ui/cellar/batch_views.h"

#include "calc/common.h"
#include "ui/display.h"
#include "ui/widgets.h"

#include <imgui.h>
#include <implot.h>

#include <chrono>
#include <cmath>
#include <unordered_map>

namespace ninkasi::ui {

namespace {

using calc::Measure;

constexpr ImGuiTableFlags kListFlags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                                       ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_PadOuterX;

double unix_seconds(domain::Timestamp time) {
    return static_cast<double>(time.time_since_epoch().count());
}

// "Gravity (SG)"
std::string axis_label(const char* name, calc::Unit unit) {
    return std::string(name) + " (" + std::string(calc::symbol(unit)) + ")";
}

} // namespace

std::string batch_age_text(const domain::Batch& batch) {
    const std::optional<domain::Timestamp> brewed = batch.brewed_at();
    if (!brewed) {
        if (batch.brews.empty()) return "";
        return "brewed " + date_text(domain::local_date(batch.brews.front().started_at)) + ", knock-out to log";
    }
    const int days = domain::days_between(domain::local_date(*brewed), domain::today());
    if (days <= 0) return "brewed today";
    if (days == 1) return "brewed yesterday";
    return "brewed " + std::to_string(days) + " days ago";
}

std::optional<calc::Unit> amount_unit(const domain::IngredientUse& use, const calc::UnitPreferences& units) {
    switch (use.amount_kind) {
    case domain::AmountKind::Mass:
        switch (use.kind) {
        case domain::IngredientKind::Fermentable: return units.unit_for(Measure::Mass);
        case domain::IngredientKind::Hop:         return units.unit_for(Measure::HopMass);
        default:                                  return units.unit_for(Measure::AdditionMass);
        }
    case domain::AmountKind::Volume: return small_volume_unit(units.unit_for(Measure::Volume));
    case domain::AmountKind::Count:
    case domain::AmountKind::Text:   return std::nullopt;
    }
    return std::nullopt;
}

std::string amount_text(const domain::IngredientUse& use, double amount, const calc::UnitPreferences& units) {
    if (use.amount_kind == domain::AmountKind::Text) return use.text;
    if (use.amount_kind == domain::AmountKind::Count) return number_text(amount, 1, amount == 1.0 ? " pack" : " packs");
    return calc::format(amount, *amount_unit(use, units));
}

void draw_batch_figures(const domain::Batch& batch, const domain::RecipeStats& predicted,
                        const calc::UnitPreferences& units, calc::AbvFormula formula) {
    const calc::Unit gravity = units.unit_for(Measure::Gravity);
    const calc::Unit temperature = units.unit_for(Measure::Temperature);
    const std::optional<double> og = batch.og();
    const domain::Reading* latest = batch.latest_gravity();
    const domain::Reading* warmth = batch.latest_temperature();
    const domain::Timestamp now = domain::now();

    if (!ImGui::BeginTable("##figures", 6, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_BordersInnerV)) return;
    for (const char* label : {"OG", "Gravity", "ABV", "Attenuation", "Temperature", "Target FG"}) {
        ImGui::TableNextColumn();
        ImGui::TextDisabled("%s", label);
    }
    ImGui::TableNextRow();

    ImGui::TableNextColumn();
    ImGui::TextUnformatted(og ? calc::format(*og, gravity).c_str() : "--");
    ImGui::SetItemTooltip("Measured at knock-out. The recipe predicted %s.", calc::format(predicted.og, gravity).c_str());

    ImGui::TableNextColumn();
    if (latest) {
        ImGui::TextUnformatted(calc::format(*latest->gravity_sg, gravity).c_str());
        ImGui::SetItemTooltip("%s ago, %s", ago_text(latest->at, now).c_str(), time_text(latest->at).c_str());
    } else {
        ImGui::TextUnformatted("--");
    }

    // Alcohol and attenuation so far, from the OG and the latest gravity.
    const bool both = og && latest;
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(both ? number_text(calc::abv(*og, *latest->gravity_sg, formula) * 100.0, 1, " %").c_str()
                                : "--");
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(
        both ? number_text(calc::apparent_attenuation(*og, *latest->gravity_sg) * 100.0, 0, " %").c_str() : "--");

    ImGui::TableNextColumn();
    if (warmth) {
        ImGui::TextUnformatted(calc::format(*warmth->temperature_c, temperature).c_str());
        ImGui::SetItemTooltip("%s ago", ago_text(warmth->at, now).c_str());
    } else {
        ImGui::TextUnformatted("--");
    }

    ImGui::TableNextColumn();
    ImGui::TextUnformatted(calc::format(predicted.fg, gravity).c_str());
    ImGui::SetItemTooltip("Predicted by the recipe from the yeast's attenuation");
    ImGui::EndTable();
}

void draw_fermentation_chart(const domain::Batch& batch, double predicted_fg, const calc::UnitPreferences& units,
                             float height) {
    const calc::Unit gravity = units.unit_for(Measure::Gravity);
    const calc::Unit temperature = units.unit_for(Measure::Temperature);

    // The OG at knock-out starts the gravity curve.
    std::vector<double> gravity_x, gravity_y, temperature_x, temperature_y;
    if (const std::optional<double> og = batch.og(); og && batch.brewed_at()) {
        gravity_x.push_back(unix_seconds(*batch.brewed_at()));
        gravity_y.push_back(calc::from_canonical(*og, gravity));
    }
    for (const domain::Reading& reading : batch.readings) {
        if (reading.gravity_sg) {
            gravity_x.push_back(unix_seconds(reading.at));
            gravity_y.push_back(calc::from_canonical(*reading.gravity_sg, gravity));
        }
        if (reading.temperature_c) {
            temperature_x.push_back(unix_seconds(reading.at));
            temperature_y.push_back(calc::from_canonical(*reading.temperature_c, temperature));
        }
    }
    if (gravity_x.size() + temperature_x.size() < 2) {
        wrapped_note("The fermentation chart appears once there are readings to plot.");
        return;
    }

    // Refit when readings are added or removed; otherwise keep the brewer's zoom.
    static std::unordered_map<ImGuiID, std::size_t> plotted_counts;
    const ImGuiID plot_id = ImGui::GetID("##fermentation");
    const std::size_t count = gravity_x.size() + temperature_x.size();
    if (plotted_counts[plot_id] != count) {
        plotted_counts[plot_id] = count;
        ImPlot::SetNextAxesToFit();
    }

    if (!ImPlot::BeginPlot("##fermentation", ImVec2(-1.0f, height), ImPlotFlags_NoMenus)) return;
    ImPlot::SetupAxis(ImAxis_X1, nullptr, ImPlotAxisFlags_None);
    ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Time);
    ImPlot::SetupAxis(ImAxis_Y1, axis_label("Gravity", gravity).c_str());
    ImPlot::SetupAxisFormat(ImAxis_Y1, calc::display_decimals(gravity) >= 3 ? "%.3f" : "%.1f");
    ImPlot::SetupAxis(ImAxis_Y2, axis_label("Temperature", temperature).c_str(), ImPlotAxisFlags_AuxDefault);

    ImPlot::SetAxes(ImAxis_X1, ImAxis_Y1);
    if (!gravity_x.empty()) {
        ImPlot::SetNextMarkerStyle(ImPlotMarker_Circle, 4.0f);
        ImPlot::PlotLine("Gravity", gravity_x.data(), gravity_y.data(), static_cast<int>(gravity_x.size()));
    }
    if (calc::is_valid(predicted_fg)) {
        const double fg = calc::from_canonical(predicted_fg, gravity);
        ImPlot::SetNextLineStyle(ImVec4(0.6f, 0.6f, 0.6f, 0.8f), 1.0f);
        ImPlot::PlotInfLines("Target FG", &fg, 1, ImPlotInfLinesFlags_Horizontal);
    }
    ImPlot::SetAxes(ImAxis_X1, ImAxis_Y2);
    if (!temperature_x.empty()) {
        ImPlot::SetNextMarkerStyle(ImPlotMarker_Square, 3.0f);
        ImPlot::PlotLine("Temperature", temperature_x.data(), temperature_y.data(),
                         static_cast<int>(temperature_x.size()));
    }
    ImPlot::EndPlot();
}

std::optional<std::int64_t> draw_readings_table(const domain::Batch& batch, const calc::UnitPreferences& units) {
    if (batch.readings.empty()) {
        wrapped_note("No readings yet.");
        return std::nullopt;
    }
    const calc::Unit gravity = units.unit_for(Measure::Gravity);
    const calc::Unit temperature = units.unit_for(Measure::Temperature);
    std::optional<std::int64_t> remove;

    if (!ImGui::BeginTable("##readings", 7, kListFlags | ImGuiTableFlags_ScrollY, ImVec2(0.0f, 0.0f))) return remove;
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Taken");
    ImGui::TableSetupColumn("Gravity");
    ImGui::TableSetupColumn("Temperature");
    ImGui::TableSetupColumn("pH");
    ImGui::TableSetupColumn("Method");
    ImGui::TableSetupColumn("Note", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("##delete");
    ImGui::TableHeadersRow();

    for (auto it = batch.readings.rbegin(); it != batch.readings.rend(); ++it) {
        const domain::Reading& reading = *it;
        ImGui::PushID(static_cast<int>(reading.id));
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(time_text(reading.at).c_str());
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(reading.gravity_sg ? calc::format(*reading.gravity_sg, gravity).c_str() : "");
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(reading.temperature_c ? calc::format(*reading.temperature_c, temperature).c_str() : "");
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(reading.ph ? number_text(*reading.ph, 2).c_str() : "");
        ImGui::TableNextColumn();
        if (reading.method == domain::ReadingMethod::Refractometer && reading.raw_value) {
            ImGui::Text("Refractometer, %s °Bx", number_text(*reading.raw_value, 1).c_str());
        } else if (reading.gravity_sg) {
            ImGui::TextUnformatted(domain::name(reading.method).data());
        }
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(reading.note.c_str());
        ImGui::TableNextColumn();
        if (ImGui::SmallButton("×")) remove = reading.id;
        ImGui::SetItemTooltip("Delete this reading");
        ImGui::PopID();
    }
    ImGui::EndTable();
    return remove;
}

void draw_ingredients_used(const std::vector<domain::IngredientUse>& uses, const calc::UnitPreferences& units) {
    if (uses.empty()) {
        wrapped_note("No ingredients were recorded for this brew.");
        return;
    }
    if (!ImGui::BeginTable("##used", 4, kListFlags)) return;
    ImGui::TableSetupColumn("Ingredient", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Addition");
    ImGui::TableSetupColumn("Recipe");
    ImGui::TableSetupColumn("Used");
    ImGui::TableHeadersRow();
    for (const domain::IngredientUse& use : uses) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(use.name.c_str());
        ImGui::TableNextColumn();
        ImGui::TextDisabled("%s", use.detail.c_str());
        ImGui::TableNextColumn();
        if (use.amount_kind != domain::AmountKind::Text) {
            ImGui::TextUnformatted(amount_text(use, use.planned, units).c_str());
        }
        ImGui::TableNextColumn();
        // Highlight amounts more than 1 % off the recipe.
        const bool differs = use.amount_kind != domain::AmountKind::Text &&
                             std::abs(use.actual - use.planned) > 0.01 * std::abs(use.planned);
        if (differs) {
            ImGui::TextColored(kWarningColor, "%s", amount_text(use, use.actual, units).c_str());
        } else {
            ImGui::TextUnformatted(amount_text(use, use.actual, units).c_str());
        }
    }
    ImGui::EndTable();
}

void draw_activity(const std::vector<domain::Activity>& entries, const calc::UnitPreferences& units) {
    if (entries.empty()) {
        wrapped_note("Nothing has happened here yet.");
        return;
    }
    if (!ImGui::BeginTable("##activity", 3, kListFlags | ImGuiTableFlags_ScrollY)) return;
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("When");
    ImGui::TableSetupColumn("What", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Who");
    ImGui::TableHeadersRow();
    for (const domain::Activity& entry : entries) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextDisabled("%s", time_text(entry.at).c_str());
        ImGui::TableNextColumn();
        ImGui::TextWrapped("%s", rendered(entry.text, units).c_str());
        ImGui::TableNextColumn();
        ImGui::TextDisabled("%s", entry.user.c_str());
    }
    ImGui::EndTable();
}

} // namespace ninkasi::ui
