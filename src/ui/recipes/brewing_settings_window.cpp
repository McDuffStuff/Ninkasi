#include "ui/recipes/brewing_settings_window.h"

#include "app/calc_settings.h"
#include "ui/recipes/recipe_sections.h"
#include "ui/widgets.h"

#include <imgui.h>

#include <array>

namespace ninkasi::ui {

namespace {

constexpr double kSaveDelay = 0.5;  // seconds after the last change

constexpr std::array kIbuFormulas = {calc::IbuFormula::Tinseth, calc::IbuFormula::Rager, calc::IbuFormula::Garetz};
constexpr std::array kBoilGravities = {
    domain::BoilGravity::PreBoil, domain::BoilGravity::Average, domain::BoilGravity::PostBoil,
};
constexpr std::array kColorModels = {calc::ColorModel::Morey, calc::ColorModel::Daniels, calc::ColorModel::Mosher};
constexpr std::array kAbvFormulas = {calc::AbvFormula::Simple, calc::AbvFormula::Alternate, calc::AbvFormula::Balling};
constexpr std::array kHopForms = {
    calc::HopForm::Pellet, calc::HopForm::Leaf, calc::HopForm::Plug, calc::HopForm::Powder, calc::HopForm::Extract,
};

void row(const char* label, float width) {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::TableNextColumn();
    ImGui::SetNextItemWidth(width);
}

} // namespace

BrewingSettingsWindow::BrewingSettingsWindow(domain::CalcSettings& calc_settings, data::SettingsRepository& settings,
                                             app::RecipeService& recipes, app::IngredientLibrary& library,
                                             const calc::UnitPreferences& units, float ui_scale)
    : calc_settings_(calc_settings),
      settings_(settings),
      recipes_(recipes),
      library_(library),
      units_(units),
      ui_scale_(ui_scale) {}

BrewingSettingsWindow::~BrewingSettingsWindow() {
    try {
        save();
    } catch (...) {
        // Nothing useful to do during shutdown.
    }
}

void BrewingSettingsWindow::show() {
    if (!visible_) defaults_ = recipes_.defaults();
    visible_ = true;
    focus_ = true;
}

void BrewingSettingsWindow::save() {
    if (calc_dirty_) app::save_calc_settings(settings_, calc_settings_);
    if (defaults_dirty_) recipes_.set_defaults(defaults_);
    calc_dirty_ = defaults_dirty_ = false;
}

void BrewingSettingsWindow::draw() {
    if (!visible_) return;
    ImGui::SetNextWindowSize(ImVec2(720.0f, 620.0f) * ui_scale_, ImGuiCond_FirstUseEver);
    if (focus_) {
        ImGui::SetNextWindowFocus();
        focus_ = false;
    }
    if (ImGui::Begin("Brewing settings", &visible_)) {
        if (ImGui::BeginTabBar("##settings")) {
            if (ImGui::BeginTabItem("Calculations")) {
                if (draw_calculations()) {
                    calc_dirty_ = true;
                    last_change_ = ImGui::GetTime();
                }
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("New recipe defaults")) {
                wrapped_note("New recipes, and recipes imported from BeerJSON, start with these values. "
                             "Changing them doesn't change existing recipes.");
                const RecipeContext context{units_, library_, ui_scale_};
                bool changed = false;
                ImGui::SeparatorText("Batch");
                changed |= draw_batch_settings(defaults_, context);
                ImGui::SeparatorText("Equipment");
                changed |= draw_equipment(defaults_, context);
                ImGui::SeparatorText("Mash steps");
                changed |= draw_mash_steps(defaults_, context);
                ImGui::SeparatorText("Source water");
                changed |= draw_water_source(defaults_, context);
                if (changed) {
                    defaults_dirty_ = true;
                    last_change_ = ImGui::GetTime();
                }
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }
    ImGui::End();

    if ((calc_dirty_ || defaults_dirty_) && (!visible_ || ImGui::GetTime() - last_change_ > kSaveDelay)) save();
}

bool BrewingSettingsWindow::draw_calculations() {
    bool changed = false;
    calc::IbuSettings& ibu = calc_settings_.ibu;
    const float width = 260.0f * ui_scale_;
    const float narrow = 90.0f * ui_scale_;

    if (!ImGui::BeginTable("##calculations", 2, ImGuiTableFlags_SizingStretchProp)) return false;
    ImGui::TableSetupColumn("##label", ImGuiTableColumnFlags_WidthFixed, 220.0f * ui_scale_);
    ImGui::TableSetupColumn("##value", ImGuiTableColumnFlags_WidthStretch);

    row("IBU formula", width);
    changed |= enum_combo("##ibu_formula", ibu.formula, kIbuFormulas);
    row("Gravity used for IBUs", width);
    changed |= enum_combo("##boil_gravity", calc_settings_.boil_gravity, kBoilGravities);
    row("Utilization scale", narrow);
    changed |= edit_percent("##scale", ibu.utilization_scale, 0);
    ImGui::SameLine();
    ImGui::TextDisabled("calibrate to lab-measured IBUs");
    for (calc::HopForm form : kHopForms) {
        const std::string label = std::string(domain::name(form)) + " utilization";
        row(label.c_str(), narrow);
        ImGui::PushID(static_cast<int>(form));
        changed |= edit_percent("##form", ibu.form_factor[static_cast<std::size_t>(form)], 0);
        ImGui::PopID();
    }
    row("First-wort hops", narrow);
    changed |= edit_percent("##first_wort", ibu.first_wort_factor, 0);
    ImGui::SameLine();
    ImGui::TextDisabled("of a full-length boil addition");
    row("Mash hops", narrow);
    changed |= edit_percent("##mash_hops", ibu.mash_hop_factor, 0);
    ImGui::SameLine();
    ImGui::TextDisabled("of a full-length boil addition");
    if (ibu.formula == calc::IbuFormula::Garetz) {
        row("Brewery elevation", narrow);
        changed |= edit_number("##elevation", ibu.elevation_m, 0, "m");
        ImGui::SameLine();
        ImGui::TextDisabled("(%.0f ft)", ibu.elevation_m / 0.3048);
    }

    row("Color model", width);
    changed |= enum_combo("##color_model", calc_settings_.color_model, kColorModels);
    row("ABV formula", width);
    changed |= enum_combo("##abv_formula", calc_settings_.abv_formula, kAbvFormulas);
    ImGui::EndTable();

    ImGui::Spacing();
    wrapped_note("Tinseth is the most widely used IBU formula; Rager estimates higher and Garetz lower. Whirlpool "
                 "hops are scaled by steep temperature (Hosom). The defaults use each published formula unchanged; "
                 "the factors let you calibrate them to your brewhouse.");
    return changed;
}

} // namespace ninkasi::ui
