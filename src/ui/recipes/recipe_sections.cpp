#include "ui/recipes/recipe_sections.h"

#include "calc/common.h"
#include "calc/gravity.h"
#include "calc/water.h"
#include "ui/widgets.h"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <string>
#include <string_view>

namespace ninkasi::ui {

namespace {

using calc::Measure;
using domain::FermentableType;

constexpr std::array kFermentableTypes = {
    FermentableType::Grain,   FermentableType::Adjunct,    FermentableType::Sugar, FermentableType::Extract,
    FermentableType::DryExtract, FermentableType::Fruit, FermentableType::Other,
};
constexpr std::array kMaltCategories = {
    calc::MaltCategory::Base, calc::MaltCategory::Crystal, calc::MaltCategory::Roasted, calc::MaltCategory::Acidulated,
};
constexpr std::array kHopForms = {
    calc::HopForm::Pellet, calc::HopForm::Leaf, calc::HopForm::Plug, calc::HopForm::Powder, calc::HopForm::Extract,
};
constexpr std::array kHopUses = {
    calc::HopUse::Boil, calc::HopUse::FirstWort, calc::HopUse::Whirlpool, calc::HopUse::Mash, calc::HopUse::DryHop,
};
constexpr std::array kYeastForms = {domain::YeastForm::Liquid, domain::YeastForm::Dry, domain::YeastForm::Slurry};
constexpr std::array kMiscUses = {
    domain::MiscUse::Mash, domain::MiscUse::Boil, domain::MiscUse::Whirlpool, domain::MiscUse::Fermenter,
    domain::MiscUse::Packaging,
};
constexpr std::array kStarterMethods = {
    calc::StarterMethod::StirPlate, calc::StarterMethod::Shaken, calc::StarterMethod::NoAgitation,
};

constexpr ImGuiTableFlags kListFlags =
    ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_PadOuterX;

const ImVec4 kGood{0.40f, 0.75f, 0.40f, 1.0f};
const ImVec4 kWarning{0.95f, 0.65f, 0.20f, 1.0f};

float px(const RecipeContext& context, float points) {
    return points * context.ui_scale;
}

// "Amount (lb)"
std::string with_unit(const char* label, calc::Unit unit) {
    return std::string(label) + " (" + std::string(calc::symbol(unit)) + ")";
}

// A number with `decimals` places, or "--" if it can't be worked out.
std::string number(double value, int decimals, const char* suffix = "") {
    if (!calc::is_valid(value)) return "--";
    char text[64];
    std::snprintf(text, sizeof text, "%.*f%s", decimals, value, suffix);
    return text;
}

bool begin_form(const char* id, const RecipeContext& context) {
    if (!ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingStretchProp)) return false;
    ImGui::TableSetupColumn("##label", ImGuiTableColumnFlags_WidthFixed, px(context, 170.0f));
    ImGui::TableSetupColumn("##value", ImGuiTableColumnFlags_WidthStretch);
    return true;
}

void form_row(const char* label) {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::TableNextColumn();
}

bool contains_ignoring_case(std::string_view text, std::string_view filter) {
    if (filter.empty()) return true;
    auto lower = [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); };
    return std::search(text.begin(), text.end(), filter.begin(), filter.end(),
                       [&](char a, char b) { return lower(a) == lower(b); }) != text.end();
}

bool remove_button() {
    const bool clicked = ImGui::SmallButton("×");
    ImGui::SetItemTooltip("Remove");
    return clicked;
}

// A button that opens a searchable list of library ingredients. `add` is
// called with the chosen entry's id and a copy of it (id 0 for a custom one).
template <typename T, typename Describe, typename Add>
bool library_picker(const char* label, const std::vector<domain::LibraryItem<T>>& entries, Describe describe, Add add,
                    const RecipeContext& context) {
    bool added = false;
    if (ImGui::Button(label)) ImGui::OpenPopup(label);
    if (ImGui::BeginPopup(label)) {
        static std::string filter;
        if (ImGui::IsWindowAppearing()) {
            filter.clear();
            ImGui::SetKeyboardFocusHere();
        }
        ImGui::SetNextItemWidth(px(context, 380.0f));
        ImGui::InputTextWithHint("##filter", "Search the library", &filter);
        if (ImGui::BeginChild("##entries", ImVec2(px(context, 380.0f), px(context, 280.0f)))) {
            for (const domain::LibraryItem<T>& entry : entries) {
                if (!contains_ignoring_case(entry.item.name, filter)) continue;
                ImGui::PushID(static_cast<int>(entry.id));
                if (ImGui::Selectable(entry.item.name.c_str())) {
                    add(entry.id, entry.item);
                    added = true;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine(px(context, 220.0f));
                ImGui::TextDisabled("%s", describe(entry.item).c_str());
                ImGui::PopID();
            }
        }
        ImGui::EndChild();
        ImGui::Separator();
        if (ImGui::Selectable("Custom ingredient (not from the library)")) {
            T custom;
            custom.name = "New ingredient";
            add(0, custom);
            added = true;
        }
        ImGui::EndPopup();
    }
    return added;
}

bool salt_combo(const char* label, calc::Salt& salt) {
    bool changed = false;
    if (ImGui::BeginCombo(label, calc::info(salt).name.data())) {
        for (const calc::SaltInfo& option : calc::salts()) {
            const std::string text = std::string(option.name) + "  " + std::string(option.formula);
            if (ImGui::Selectable(text.c_str(), option.salt == salt) && option.salt != salt) {
                salt = option.salt;
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    return changed;
}

bool acid_combo(const char* label, calc::Acid& acid) {
    bool changed = false;
    if (ImGui::BeginCombo(label, calc::info(acid).name.data())) {
        for (const calc::AcidInfo& option : calc::acids()) {
            if (ImGui::Selectable(option.name.data(), option.acid == acid) && option.acid != acid) {
                acid = option.acid;
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    return changed;
}

} // namespace

// --- Header and stats -----------------------------------------------------------

bool draw_recipe_header(domain::Recipe& recipe, const RecipeContext& context) {
    bool changed = false;
    ImGui::SetNextItemWidth(px(context, 280.0f));
    changed |= ImGui::InputTextWithHint("##name", "Recipe name", &recipe.name);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(px(context, 240.0f));
    changed |= ImGui::InputTextWithHint("##style", "Style", &recipe.style);

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Batch");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(px(context, 80.0f));
    changed |= edit_quantity("##batch", recipe.batch_volume_l, context.units.unit_for(Measure::Volume));
    ImGui::SetItemTooltip("Volume into the fermenter");

    ImGui::SameLine(0.0f, px(context, 20.0f));
    ImGui::TextUnformatted("Boil");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(px(context, 55.0f));
    changed |= edit_number("##boil", recipe.boil_time_min, 0, "min");

    ImGui::SameLine(0.0f, px(context, 20.0f));
    ImGui::TextUnformatted("Efficiency");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(px(context, 75.0f));
    changed |= edit_percent("##efficiency", recipe.efficiency);
    ImGui::SetItemTooltip("Brewhouse efficiency: the share of the grain's extract that reaches the fermenter");
    return changed;
}

bool draw_batch_settings(domain::Recipe& recipe, const RecipeContext& context) {
    bool changed = false;
    if (!begin_form("##batch_settings", context)) return false;
    form_row("Batch size");
    ImGui::SetNextItemWidth(px(context, 100.0f));
    changed |= edit_quantity("##batch", recipe.batch_volume_l, context.units.unit_for(Measure::Volume));
    form_row("Boil time");
    ImGui::SetNextItemWidth(px(context, 100.0f));
    changed |= edit_number("##boil", recipe.boil_time_min, 0, "min");
    form_row("Brewhouse efficiency");
    ImGui::SetNextItemWidth(px(context, 100.0f));
    changed |= edit_percent("##efficiency", recipe.efficiency);
    form_row("Pitch rate");
    ImGui::SetNextItemWidth(px(context, 100.0f));
    changed |= edit_number("##pitch_rate", recipe.pitch_rate, 2, "million cells/mL/°P");
    ImGui::EndTable();
    return changed;
}

void draw_recipe_stats(const domain::RecipeStats& stats, const RecipeContext& context) {
    const calc::Unit gravity = context.units.unit_for(Measure::Gravity);
    const calc::Unit color = context.units.unit_for(Measure::Color);
    if (!ImGui::BeginTable("##stats", 8, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_BordersInnerV)) return;

    const char* labels[] = {"OG", "FG", "ABV", "IBU", "BU:GU", "Color", "Pre-boil", "Mash pH"};
    ImGui::TableNextRow();
    for (const char* label : labels) {
        ImGui::TableNextColumn();
        ImGui::TextDisabled("%s", label);
    }

    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(calc::format(stats.og, gravity).c_str());
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(calc::format(stats.fg, gravity).c_str());
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(number(stats.abv * 100.0, 1, " %").c_str());
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(number(stats.ibu, 0).c_str());
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(number(stats.bu_gu, 2).c_str());
    ImGui::TableNextColumn();
    if (calc::is_valid(stats.srm)) {
        const calc::Rgb rgb = calc::srm_to_rgb(stats.srm);
        const float size = ImGui::GetTextLineHeight();
        ImGui::ColorButton("##beer_color", ImVec4(rgb.r, rgb.g, rgb.b, 1.0f), ImGuiColorEditFlags_NoTooltip,
                           ImVec2(size, size));
        ImGui::SameLine();
    }
    ImGui::TextUnformatted(calc::format(stats.srm, color).c_str());
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(calc::format(stats.pre_boil_gravity, gravity).c_str());
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(number(stats.mash_ph, 2).c_str());
    ImGui::EndTable();
}

// --- Fermentables -----------------------------------------------------------------

bool draw_fermentables(domain::Recipe& recipe, const domain::RecipeStats& stats, const RecipeContext& context) {
    bool changed = false;
    const calc::Unit mass = context.units.unit_for(Measure::Mass);
    const calc::Unit malt_color = context.units.unit_for(Measure::MaltColor);
    double total_kg = 0.0;
    for (const domain::FermentableAddition& addition : recipe.fermentables) total_kg += addition.mass_kg;

    if (ImGui::BeginTable("##fermentables", 8, kListFlags)) {
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn(with_unit("Amount", mass).c_str(), ImGuiTableColumnFlags_WidthFixed, px(context, 90));
        ImGui::TableSetupColumn("Share", ImGuiTableColumnFlags_WidthFixed, px(context, 50));
        ImGui::TableSetupColumn(with_unit("Color", malt_color).c_str(), ImGuiTableColumnFlags_WidthFixed, px(context, 80));
        ImGui::TableSetupColumn("Yield", ImGuiTableColumnFlags_WidthFixed, px(context, 70));
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, px(context, 120));
        ImGui::TableSetupColumn("Malt", ImGuiTableColumnFlags_WidthFixed, px(context, 110));
        ImGui::TableSetupColumn("##remove", ImGuiTableColumnFlags_WidthFixed, px(context, 22));
        ImGui::TableHeadersRow();

        std::optional<std::size_t> remove;
        for (std::size_t i = 0; i < recipe.fermentables.size(); ++i) {
            domain::FermentableAddition& addition = recipe.fermentables[i];
            domain::Fermentable& f = addition.fermentable;
            ImGui::PushID(static_cast<int>(i));
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            changed |= ImGui::InputText("##name", &f.name);
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            changed |= edit_quantity("##amount", addition.mass_kg, mass, false);
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", number(total_kg > 0.0 ? 100.0 * addition.mass_kg / total_kg : 0.0, 1, " %").c_str());
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            changed |= edit_quantity("##color", f.color_lovibond, malt_color, false);
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            changed |= edit_percent("##yield", f.yield, 0);
            ImGui::SetItemTooltip("Extract yield, as-is basis");
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            changed |= enum_combo("##type", f.type, kFermentableTypes);
            ImGui::TableNextColumn();
            if (domain::is_mashed(f.type)) {
                ImGui::SetNextItemWidth(-FLT_MIN);
                changed |= enum_combo("##category", f.category, kMaltCategories);
                ImGui::SetItemTooltip("Used for the mash pH estimate");
            } else {
                ImGui::TextDisabled("kettle");
            }
            ImGui::TableNextColumn();
            if (remove_button()) remove = i;
            ImGui::PopID();
        }
        ImGui::EndTable();
        if (remove) {
            recipe.fermentables.erase(recipe.fermentables.begin() + static_cast<std::ptrdiff_t>(*remove));
            changed = true;
        }
    }

    changed |= library_picker(
        "Add fermentable", context.library.fermentables(),
        [&](const domain::Fermentable& f) {
            return calc::format(f.color_lovibond, malt_color) + ", " + number(f.yield * 100.0, 0, " % yield");
        },
        [&](std::int64_t id, const domain::Fermentable& f) { recipe.fermentables.push_back({id, f, 0.0}); }, context);

    ImGui::SameLine(0.0f, px(context, 20.0f));
    ImGui::TextDisabled("Total %s · grain %s · potential extract %s", calc::format(total_kg, mass).c_str(),
                        calc::format(stats.grain_mass_kg, mass).c_str(),
                        calc::format(stats.potential_extract_kg, mass).c_str());
    return changed;
}

// --- Hops -------------------------------------------------------------------------

bool draw_hops(domain::Recipe& recipe, const domain::RecipeStats& stats, const RecipeContext& context) {
    bool changed = false;
    const calc::Unit mass = context.units.unit_for(Measure::HopMass);
    const calc::Unit temperature = context.units.unit_for(Measure::Temperature);

    if (ImGui::BeginTable("##hops", 9, kListFlags)) {
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn(with_unit("Amount", mass).c_str(), ImGuiTableColumnFlags_WidthFixed, px(context, 85));
        ImGui::TableSetupColumn("Alpha", ImGuiTableColumnFlags_WidthFixed, px(context, 70));
        ImGui::TableSetupColumn("Form", ImGuiTableColumnFlags_WidthFixed, px(context, 85));
        ImGui::TableSetupColumn("Use", ImGuiTableColumnFlags_WidthFixed, px(context, 100));
        ImGui::TableSetupColumn("Time (min)", ImGuiTableColumnFlags_WidthFixed, px(context, 75));
        ImGui::TableSetupColumn(with_unit("Temp", temperature).c_str(), ImGuiTableColumnFlags_WidthFixed, px(context, 75));
        ImGui::TableSetupColumn("IBU", ImGuiTableColumnFlags_WidthFixed, px(context, 45));
        ImGui::TableSetupColumn("##remove", ImGuiTableColumnFlags_WidthFixed, px(context, 22));
        ImGui::TableHeadersRow();

        std::optional<std::size_t> remove;
        for (std::size_t i = 0; i < recipe.hops.size(); ++i) {
            domain::HopAddition& addition = recipe.hops[i];
            ImGui::PushID(static_cast<int>(i));
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            changed |= ImGui::InputText("##name", &addition.hop.name);
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            changed |= edit_quantity("##amount", addition.mass_kg, mass, false);
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            changed |= edit_percent("##alpha", addition.hop.alpha_acid);
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            changed |= enum_combo("##form", addition.hop.form, kHopForms);
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            changed |= enum_combo("##use", addition.use, kHopUses);

            ImGui::TableNextColumn();
            const bool timed = addition.use == calc::HopUse::Boil || addition.use == calc::HopUse::Whirlpool;
            if (timed) {
                ImGui::SetNextItemWidth(-FLT_MIN);
                changed |= edit_number("##time", addition.time_min, 0);
                ImGui::SetItemTooltip("%s", addition.use == calc::HopUse::Boil ? "Minutes before flameout"
                                                                                : "Minutes steeped");
            } else if (addition.use == calc::HopUse::DryHop) {
                ImGui::TextDisabled("--");
            } else {
                ImGui::TextDisabled("%s", number(recipe.boil_time_min, 0, " (boil)").c_str());
            }

            ImGui::TableNextColumn();
            if (addition.use == calc::HopUse::Whirlpool) {
                ImGui::SetNextItemWidth(-FLT_MIN);
                changed |= edit_quantity("##temperature", addition.temperature_c, temperature, false);
            } else {
                ImGui::TextDisabled("--");
            }
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(number(i < stats.hop_ibus.size() ? stats.hop_ibus[i] : calc::kInvalid, 1).c_str());
            ImGui::TableNextColumn();
            if (remove_button()) remove = i;
            ImGui::PopID();
        }
        ImGui::EndTable();
        if (remove) {
            recipe.hops.erase(recipe.hops.begin() + static_cast<std::ptrdiff_t>(*remove));
            changed = true;
        }
    }

    changed |= library_picker(
        "Add hop", context.library.hops(),
        [](const domain::Hop& h) { return number(h.alpha_acid * 100.0, 1, " % alpha") + ", " + h.origin; },
        [&](std::int64_t id, const domain::Hop& h) {
            domain::HopAddition addition;
            addition.library_id = id;
            addition.hop = h;
            addition.time_min = recipe.boil_time_min;
            recipe.hops.push_back(addition);
        },
        context);
    ImGui::SameLine(0.0f, px(context, 20.0f));
    ImGui::TextDisabled("Total %s IBU", number(stats.ibu, 1).c_str());
    return changed;
}

// --- Yeast --------------------------------------------------------------------------

bool draw_yeast(domain::Recipe& recipe, const domain::RecipeStats& stats, const RecipeContext& context) {
    bool changed = false;
    const calc::Unit small_mass = context.units.unit_for(Measure::AdditionMass);
    const calc::Unit small_volume = small_volume_unit(context.units.unit_for(Measure::Volume));

    if (ImGui::BeginTable("##yeasts", 7, kListFlags)) {
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Form", ImGuiTableColumnFlags_WidthFixed, px(context, 85));
        ImGui::TableSetupColumn("Attenuation", ImGuiTableColumnFlags_WidthFixed, px(context, 85));
        ImGui::TableSetupColumn("Amount", ImGuiTableColumnFlags_WidthFixed, px(context, 125));
        ImGui::TableSetupColumn("Cells", ImGuiTableColumnFlags_WidthFixed, px(context, 125));
        ImGui::TableSetupColumn("Viability", ImGuiTableColumnFlags_WidthFixed, px(context, 75));
        ImGui::TableSetupColumn("##remove", ImGuiTableColumnFlags_WidthFixed, px(context, 22));
        ImGui::TableHeadersRow();

        std::optional<std::size_t> remove;
        for (std::size_t i = 0; i < recipe.yeasts.size(); ++i) {
            domain::YeastAddition& addition = recipe.yeasts[i];
            domain::Yeast& yeast = addition.yeast;
            ImGui::PushID(static_cast<int>(i));
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            changed |= ImGui::InputText("##name", &yeast.name);
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            changed |= enum_combo("##form", yeast.form, kYeastForms);
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            changed |= edit_percent("##attenuation", yeast.attenuation, 0);

            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(px(context, 70));
            switch (yeast.form) {
            case domain::YeastForm::Liquid:
                changed |= edit_number("##amount", addition.packs, 1, "packs");
                break;
            case domain::YeastForm::Dry:
                changed |= edit_quantity("##amount", addition.mass_kg, small_mass);
                break;
            case domain::YeastForm::Slurry:
                changed |= edit_quantity("##amount", addition.slurry_l, small_volume);
                break;
            }

            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(px(context, 60));
            switch (yeast.form) {
            case domain::YeastForm::Liquid:
                changed |= edit_number("##cells", yeast.cells_per_pack_billion, 0, "B/pack");
                break;
            case domain::YeastForm::Dry:
                changed |= edit_number("##cells", yeast.cells_per_gram_billion, 1, "B/g");
                break;
            case domain::YeastForm::Slurry:
                changed |= edit_number("##cells", addition.slurry_cells_per_ml_billion, 2, "B/mL");
                break;
            }
            ImGui::SetItemTooltip("Billions of cells per unit");

            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            changed |= edit_percent("##viability", addition.viability, 0);
            ImGui::TableNextColumn();
            if (remove_button()) remove = i;
            ImGui::PopID();
        }
        ImGui::EndTable();
        if (remove) {
            recipe.yeasts.erase(recipe.yeasts.begin() + static_cast<std::ptrdiff_t>(*remove));
            changed = true;
        }
    }

    changed |= library_picker(
        "Add yeast", context.library.yeasts(),
        [](const domain::Yeast& y) {
            return std::string(domain::name(y.form)) + ", " + number(y.attenuation * 100.0, 0, " % attenuation");
        },
        [&](std::int64_t id, const domain::Yeast& y) {
            domain::YeastAddition addition;
            addition.library_id = id;
            addition.yeast = y;
            recipe.yeasts.push_back(addition);
            if (y.type == domain::YeastType::Lager) recipe.pitch_rate = calc::kLagerPitchRate;
        },
        context);
    if (recipe.yeasts.size() > 1) {
        ImGui::SameLine(0.0f, px(context, 20.0f));
        ImGui::TextDisabled("FG uses the first yeast's attenuation");
    }

    ImGui::Spacing();
    ImGui::SeparatorText("Pitching");
    if (begin_form("##pitching", context)) {
        form_row("Pitch rate");
        ImGui::SetNextItemWidth(px(context, 70));
        changed |= edit_number("##pitch_rate", recipe.pitch_rate, 2, "million cells/mL/°P");
        ImGui::SameLine();
        if (ImGui::SmallButton("Ale")) {
            recipe.pitch_rate = calc::kAlePitchRate;
            changed = true;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Lager")) {
            recipe.pitch_rate = calc::kLagerPitchRate;
            changed = true;
        }

        form_row("Cells needed");
        ImGui::Text("%s billion", number(stats.cells_needed_billion, 0).c_str());

        form_row("Starter");
        changed |= ImGui::Checkbox("Use a starter", &recipe.starter.enabled);
        if (recipe.starter.enabled) {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(px(context, 60));
            changed |= edit_quantity("##starter_volume", recipe.starter.volume_l, small_volume);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(px(context, 60));
            changed |= edit_quantity("##starter_gravity", recipe.starter.gravity_sg,
                                     context.units.unit_for(Measure::Gravity));
            ImGui::SameLine();
            ImGui::SetNextItemWidth(px(context, 120));
            changed |= enum_combo("##starter_method", recipe.starter.method, kStarterMethods);
        }

        form_row("Cells pitched");
        const double ratio = stats.cells_available_billion / stats.cells_needed_billion;
        const bool enough = calc::is_valid(ratio) && ratio >= 0.95;
        ImGui::TextColored(enough ? kGood : kWarning, "%s billion (%s of target)",
                           number(stats.cells_available_billion, 0).c_str(), number(ratio * 100.0, 0, " %").c_str());
        if (recipe.starter.enabled) {
            ImGui::SameLine();
            ImGui::TextDisabled("%s billion before the starter", number(stats.cells_pitched_billion, 0).c_str());
        }
        ImGui::EndTable();
    }
    return changed;
}

// --- Other additions ----------------------------------------------------------------

bool draw_other_additions(domain::Recipe& recipe, const RecipeContext& context) {
    bool changed = false;
    if (ImGui::BeginTable("##miscs", 5, kListFlags)) {
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Amount", ImGuiTableColumnFlags_WidthFixed, px(context, 120));
        ImGui::TableSetupColumn("Use", ImGuiTableColumnFlags_WidthFixed, px(context, 110));
        ImGui::TableSetupColumn("Time (min)", ImGuiTableColumnFlags_WidthFixed, px(context, 75));
        ImGui::TableSetupColumn("##remove", ImGuiTableColumnFlags_WidthFixed, px(context, 22));
        ImGui::TableHeadersRow();

        std::optional<std::size_t> remove;
        for (std::size_t i = 0; i < recipe.miscs.size(); ++i) {
            domain::MiscAddition& misc = recipe.miscs[i];
            ImGui::PushID(static_cast<int>(i));
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            changed |= ImGui::InputText("##name", &misc.name);
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            changed |= ImGui::InputTextWithHint("##amount", "e.g. 1 tablet", &misc.amount);
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            changed |= enum_combo("##use", misc.use, kMiscUses);
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            changed |= edit_number("##time", misc.time_min, 0);
            ImGui::TableNextColumn();
            if (remove_button()) remove = i;
            ImGui::PopID();
        }
        ImGui::EndTable();
        if (remove) {
            recipe.miscs.erase(recipe.miscs.begin() + static_cast<std::ptrdiff_t>(*remove));
            changed = true;
        }
    }
    if (ImGui::Button("Add addition")) {
        recipe.miscs.push_back({"New addition", "", domain::MiscUse::Boil, 0.0});
        changed = true;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("Spices, finings, nutrients, fruit...");
    return changed;
}

// --- Mash ---------------------------------------------------------------------------

bool draw_mash_steps(domain::Recipe& recipe, const RecipeContext& context) {
    bool changed = false;
    const calc::Unit temperature = context.units.unit_for(Measure::Temperature);
    if (ImGui::BeginTable("##mash_steps", 4, kListFlags)) {
        ImGui::TableSetupColumn("Step", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn(with_unit("Temperature", temperature).c_str(), ImGuiTableColumnFlags_WidthFixed,
                                px(context, 110));
        ImGui::TableSetupColumn("Time (min)", ImGuiTableColumnFlags_WidthFixed, px(context, 80));
        ImGui::TableSetupColumn("##remove", ImGuiTableColumnFlags_WidthFixed, px(context, 22));
        ImGui::TableHeadersRow();

        std::optional<std::size_t> remove;
        for (std::size_t i = 0; i < recipe.mash_steps.size(); ++i) {
            domain::MashStep& step = recipe.mash_steps[i];
            ImGui::PushID(static_cast<int>(i));
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            changed |= ImGui::InputText("##name", &step.name);
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            changed |= edit_quantity("##temperature", step.temperature_c, temperature, false);
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            changed |= edit_number("##time", step.time_min, 0);
            ImGui::TableNextColumn();
            if (remove_button()) remove = i;
            ImGui::PopID();
        }
        ImGui::EndTable();
        if (remove) {
            recipe.mash_steps.erase(recipe.mash_steps.begin() + static_cast<std::ptrdiff_t>(*remove));
            changed = true;
        }
    }
    if (ImGui::Button("Add step")) {
        const double last_temperature = recipe.mash_steps.empty() ? 67.0 : recipe.mash_steps.back().temperature_c;
        recipe.mash_steps.push_back({"Rest", last_temperature, 15.0});
        changed = true;
    }
    return changed;
}

bool draw_mash(domain::Recipe& recipe, const domain::RecipeStats& stats, const RecipeContext& context) {
    const bool changed = draw_mash_steps(recipe, context);
    ImGui::Spacing();
    ImGui::SeparatorText("Mash");
    const calc::Unit volume = context.units.unit_for(Measure::Volume);
    if (begin_form("##mash_summary", context)) {
        form_row("Grain");
        ImGui::TextUnformatted(calc::format(stats.grain_mass_kg, context.units.unit_for(Measure::Mass)).c_str());
        form_row("Strike water");
        ImGui::TextUnformatted(calc::format(stats.volumes.strike_water_l, volume).c_str());
        form_row("Sparge water");
        ImGui::TextUnformatted(calc::format(stats.volumes.sparge_water_l, volume).c_str());
        form_row("Mash volume");
        ImGui::TextUnformatted(calc::format(stats.volumes.mash_volume_l, volume).c_str());
        ImGui::SetItemTooltip("Strike water plus grain: check it fits your mash tun");
        form_row("Estimated mash pH");
        ImGui::TextUnformatted(number(stats.mash_ph, 2).c_str());
        ImGui::EndTable();
    }
    return changed;
}

// --- Water --------------------------------------------------------------------------

bool draw_water_source(domain::Recipe& recipe, const RecipeContext& context) {
    bool changed = false;
    calc::WaterProfile& source = recipe.water.source;
    struct Ion {
        const char* label;
        double* ppm;
    };
    const Ion ions[] = {
        {"Calcium", &source.calcium},   {"Magnesium", &source.magnesium}, {"Sodium", &source.sodium},
        {"Chloride", &source.chloride}, {"Sulfate", &source.sulfate},     {"Bicarbonate", &source.bicarbonate},
    };
    if (ImGui::BeginTable("##source", 6, ImGuiTableFlags_SizingStretchSame)) {
        for (const Ion& ion : ions) {
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", ion.label);
        }
        for (const Ion& ion : ions) {
            ImGui::TableNextColumn();
            ImGui::PushID(ion.label);
            ImGui::SetNextItemWidth(-FLT_MIN);
            changed |= edit_number("##ppm", *ion.ppm, 0);
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (ImGui::SmallButton("Distilled / RO water")) {
        source = {};
        changed = true;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("Ion concentrations in ppm (mg/L), from your water report");
    (void)context;
    return changed;
}

bool draw_water(domain::Recipe& recipe, const domain::RecipeStats& stats, const RecipeContext& context) {
    bool changed = false;
    domain::Water& water = recipe.water;

    ImGui::SeparatorText("Source water");
    changed |= draw_water_source(recipe, context);

    ImGui::SeparatorText("Salts (dissolved in all the brewing water)");
    const calc::Unit salt_mass = context.units.unit_for(Measure::AdditionMass);
    std::optional<std::size_t> remove_salt;
    for (std::size_t i = 0; i < water.salts.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        ImGui::SetNextItemWidth(px(context, 220));
        changed |= salt_combo("##salt", water.salts[i].salt);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(px(context, 80));
        changed |= edit_quantity("##mass", water.salts[i].mass_kg, salt_mass);
        ImGui::SameLine();
        if (remove_button()) remove_salt = i;
        ImGui::PopID();
    }
    if (remove_salt) {
        water.salts.erase(water.salts.begin() + static_cast<std::ptrdiff_t>(*remove_salt));
        changed = true;
    }
    if (ImGui::Button("Add salt")) {
        water.salts.push_back({calc::Salt::Gypsum, 0.0});
        changed = true;
    }

    ImGui::SeparatorText("Acids (added to the mash)");
    std::optional<std::size_t> remove_acid;
    for (std::size_t i = 0; i < water.mash_acids.size(); ++i) {
        ImGui::PushID(static_cast<int>(1000 + i));
        ImGui::SetNextItemWidth(px(context, 220));
        changed |= acid_combo("##acid", water.mash_acids[i].acid);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(px(context, 80));
        changed |= edit_quantity("##volume", water.mash_acids[i].volume_l, calc::Unit::Milliliter);
        ImGui::SameLine();
        if (remove_button()) remove_acid = i;
        ImGui::PopID();
    }
    if (remove_acid) {
        water.mash_acids.erase(water.mash_acids.begin() + static_cast<std::ptrdiff_t>(*remove_acid));
        changed = true;
    }
    if (ImGui::Button("Add acid")) {
        water.mash_acids.push_back({calc::Acid::Lactic88, 0.0});
        changed = true;
    }

    ImGui::SeparatorText("Result");
    const calc::WaterProfile& mash = stats.mash_water;
    if (ImGui::BeginTable("##mash_water", 6, ImGuiTableFlags_SizingStretchSame)) {
        const char* labels[] = {"Calcium", "Magnesium", "Sodium", "Chloride", "Sulfate", "Bicarbonate"};
        const double values[] = {mash.calcium, mash.magnesium, mash.sodium, mash.chloride, mash.sulfate, mash.bicarbonate};
        for (const char* label : labels) {
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", label);
        }
        for (double value : values) {
            ImGui::TableNextColumn();
            ImGui::Text("%s ppm", number(value, 0).c_str());
        }
        ImGui::EndTable();
    }

    if (begin_form("##water_result", context)) {
        form_row("Residual alkalinity");
        ImGui::Text("%s ppm as CaCO3", number(stats.residual_alkalinity, 0).c_str());
        form_row("Sulfate : chloride");
        ImGui::TextUnformatted(number(stats.sulfate_chloride_ratio, 2).c_str());
        form_row("Target mash pH");
        ImGui::SetNextItemWidth(px(context, 70));
        changed |= edit_number("##target_ph", water.target_mash_ph, 2);
        form_row("Estimated mash pH");
        ImGui::TextUnformatted(number(stats.mash_ph, 2).c_str());
        form_row("To reach the target");
        const double meq = stats.acid_to_target_meq;
        if (!calc::is_valid(meq)) {
            ImGui::TextDisabled("--");
        } else if (std::abs(meq) < 0.5) {
            ImGui::TextColored(kGood, "On target");
        } else if (meq > 0.0) {
            const double lactic_ml = calc::acid_volume_l(calc::Acid::Lactic88, meq, water.target_mash_ph) * 1000.0;
            const double phosphoric_ml =
                calc::acid_volume_l(calc::Acid::Phosphoric10, meq, water.target_mash_ph) * 1000.0;
            ImGui::Text("Add %s mL lactic acid 88 %% (or %s mL phosphoric acid 10 %%)", number(lactic_ml, 1).c_str(),
                        number(phosphoric_ml, 0).c_str());
        } else {
            ImGui::TextColored(kWarning, "Below target: use less acid, or add alkalinity (e.g. baking soda)");
        }
        ImGui::EndTable();
    }
    wrapped_note("The mash pH is an estimate from each malt's typical acidity and your water's residual alkalinity "
                 "(±0.1–0.2). Check it with a meter and adjust the malt values in the library to match.");
    return changed;
}

// --- Equipment ----------------------------------------------------------------------

bool draw_equipment(domain::Recipe& recipe, const RecipeContext& context) {
    bool changed = false;
    domain::Equipment& e = recipe.equipment;
    const calc::Unit volume = context.units.unit_for(Measure::Volume);
    const calc::Unit mass = context.units.unit_for(Measure::Mass);
    if (!begin_form("##equipment", context)) return false;

    form_row("Boil-off rate");
    ImGui::SetNextItemWidth(px(context, 90));
    changed |= edit_quantity("##boil_off", e.boil_off_l_per_hr, volume);
    ImGui::SameLine();
    ImGui::TextUnformatted("per hour");
    form_row("Kettle losses");
    ImGui::SetNextItemWidth(px(context, 90));
    changed |= edit_quantity("##kettle_loss", e.kettle_loss_l, volume);
    ImGui::SetItemTooltip("Trub, hops, chiller and dead space left behind at knockout");
    form_row("Mash tun dead space");
    ImGui::SetNextItemWidth(px(context, 90));
    changed |= edit_quantity("##mash_tun_loss", e.mash_tun_loss_l, volume);
    form_row("Cooling shrinkage");
    ImGui::SetNextItemWidth(px(context, 90));
    changed |= edit_percent("##shrinkage", e.cooling_shrinkage);
    form_row("Grain absorption");
    ImGui::SetNextItemWidth(px(context, 90));
    changed |= edit_volume_per_mass("##absorption", e.grain_absorption_l_per_kg, mass);
    form_row("Mash thickness");
    ImGui::SetNextItemWidth(px(context, 90));
    changed |= edit_volume_per_mass("##thickness", e.mash_thickness_l_per_kg, mass);
    form_row("Grain displacement");
    ImGui::SetNextItemWidth(px(context, 90));
    changed |= edit_volume_per_mass("##displacement", e.grain_displacement_l_per_kg, mass);
    ImGui::EndTable();
    return changed;
}

void draw_volumes(const domain::RecipeStats& stats, const RecipeContext& context) {
    const calc::Unit volume = context.units.unit_for(Measure::Volume);
    const calc::BrewVolumes& v = stats.volumes;
    if (!begin_form("##volumes", context)) return;
    const std::pair<const char*, double> rows[] = {
        {"Total water", v.total_water_l},   {"Strike water", v.strike_water_l},
        {"Sparge water", v.sparge_water_l}, {"Pre-boil (hot)", v.pre_boil_volume_l},
        {"Post-boil (hot)", v.post_boil_volume_l}, {"Knockout (cooled)", v.knockout_volume_l},
        {"Into the fermenter", v.fermenter_volume_l},
    };
    for (const auto& [label, liters] : rows) {
        form_row(label);
        ImGui::TextUnformatted(calc::format(liters, volume).c_str());
    }
    ImGui::EndTable();
}

// --- Notes --------------------------------------------------------------------------

bool draw_notes(domain::Recipe& recipe) {
    bool changed = false;
    ImGui::SetNextItemWidth(-FLT_MIN);
    changed |= ImGui::InputTextWithHint("##author", "Author", &recipe.author);
    changed |= ImGui::InputTextMultiline("##notes", &recipe.notes, ImVec2(-FLT_MIN, -FLT_MIN));
    return changed;
}

} // namespace ninkasi::ui
