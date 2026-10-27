#include "ui/recipes/ingredient_library_window.h"

#include "calc/water.h"
#include "ui/widgets.h"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <exception>
#include <optional>

namespace ninkasi::ui {

namespace {

constexpr std::array kFermentableTypes = {
    domain::FermentableType::Grain,      domain::FermentableType::Adjunct, domain::FermentableType::Sugar,
    domain::FermentableType::Extract,    domain::FermentableType::DryExtract, domain::FermentableType::Fruit,
    domain::FermentableType::Other,
};
constexpr std::array kMaltCategories = {
    calc::MaltCategory::Base, calc::MaltCategory::Crystal, calc::MaltCategory::Roasted, calc::MaltCategory::Acidulated,
};
constexpr std::array kHopForms = {
    calc::HopForm::Pellet, calc::HopForm::Leaf, calc::HopForm::Plug, calc::HopForm::Powder, calc::HopForm::Extract,
};
constexpr std::array kYeastTypes = {domain::YeastType::Ale, domain::YeastType::Lager, domain::YeastType::Other};
constexpr std::array kYeastForms = {domain::YeastForm::Liquid, domain::YeastForm::Dry, domain::YeastForm::Slurry};

constexpr ImGuiTableFlags kTableFlags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                                        ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit;

bool matches(const std::string& name, const std::string& filter) {
    if (filter.empty()) return true;
    auto lower = [](std::string text) {
        std::transform(text.begin(), text.end(), text.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return text;
    };
    return lower(name).find(lower(filter)) != std::string::npos;
}

// True when the field just drawn was finished after an edit.
bool finished() {
    return ImGui::IsItemDeactivatedAfterEdit();
}

} // namespace

IngredientLibraryWindow::IngredientLibraryWindow(app::IngredientLibrary& library, const calc::UnitPreferences& units,
                                                 float ui_scale)
    : library_(library), units_(units), ui_scale_(ui_scale) {}

void IngredientLibraryWindow::show() {
    visible_ = true;
    focus_ = true;
    reload();
}

void IngredientLibraryWindow::reload() {
    fermentables_ = library_.fermentables();
    hops_ = library_.hops();
    yeasts_ = library_.yeasts();
}

void IngredientLibraryWindow::draw() {
    if (!visible_) return;
    if (needs_reload_) {
        reload();
        needs_reload_ = false;
    }
    ImGui::SetNextWindowSize(ImVec2(900.0f, 560.0f) * ui_scale_, ImGuiCond_FirstUseEver);
    if (focus_) {
        ImGui::SetNextWindowFocus();
        focus_ = false;
    }
    if (ImGui::Begin("Ingredient library", &visible_)) {
        ImGui::SetNextItemWidth(260.0f * ui_scale_);
        ImGui::InputTextWithHint("##filter", "Search", &filter_);
        ImGui::SameLine();
        ImGui::TextDisabled("Recipes copy an ingredient when it's added, so edits here don't change existing recipes.");
        if (!error_.empty()) ImGui::TextColored(ImVec4(0.95f, 0.65f, 0.20f, 1.0f), "%s", error_.c_str());

        if (ImGui::BeginTabBar("##kinds")) {
            if (ImGui::BeginTabItem("Fermentables")) {
                draw_fermentables();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Hops")) {
                draw_hops();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Yeasts")) {
                draw_yeasts();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }
    ImGui::End();
}

template <typename T>
void IngredientLibraryWindow::save_row(const domain::LibraryItem<T>& entry) {
    try {
        library_.update(entry.id, entry.item);
        error_.clear();
    } catch (const std::exception& e) {
        // Rejected (e.g. a blank name): show why and put the saved values back.
        error_ = e.what();
        needs_reload_ = true;
    }
}

void IngredientLibraryWindow::draw_fermentables() {
    const calc::Unit color = units_.unit_for(calc::Measure::MaltColor);
    if (ImGui::Button("Add fermentable")) {
        domain::Fermentable f;
        f.name = "New fermentable";
        library_.add(f);
        reload();
    }
    std::optional<std::int64_t> remove;
    if (ImGui::BeginTable("##fermentables", 8, kTableFlags, ImVec2(0.0f, -FLT_MIN))) {
        const float s = ui_scale_;
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Supplier", ImGuiTableColumnFlags_WidthFixed, 110 * s);
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 120 * s);
        ImGui::TableSetupColumn("Malt", ImGuiTableColumnFlags_WidthFixed, 110 * s);
        ImGui::TableSetupColumn(("Color (" + std::string(calc::symbol(color)) + ")").c_str(),
                                ImGuiTableColumnFlags_WidthFixed, 80 * s);
        ImGui::TableSetupColumn("Yield", ImGuiTableColumnFlags_WidthFixed, 70 * s);
        ImGui::TableSetupColumn("DI pH", ImGuiTableColumnFlags_WidthFixed, 65 * s);
        ImGui::TableSetupColumn("##remove", ImGuiTableColumnFlags_WidthFixed, 22 * s);
        ImGui::TableHeadersRow();

        for (auto& entry : fermentables_) {
            domain::Fermentable& f = entry.item;
            if (!matches(f.name, filter_)) continue;
            bool save = false;
            ImGui::PushID(static_cast<int>(entry.id));
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::InputText("##name", &f.name);
            save |= finished();
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::InputText("##supplier", &f.supplier);
            save |= finished();
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            save |= enum_combo("##type", f.type, kFermentableTypes);
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            save |= enum_combo("##category", f.category, kMaltCategories);
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            edit_quantity("##color", f.color_lovibond, color, false);
            save |= finished();
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            edit_percent("##yield", f.yield, 0);
            save |= finished();
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            double di_ph = f.di_ph.value_or(0.0);
            if (ImGui::InputDouble("##di_ph", &di_ph, 0.0, 0.0, di_ph > 0.0 ? "%.2f" : "auto")) {
                f.di_ph = di_ph > 0.0 ? std::optional<double>(di_ph) : std::nullopt;
            }
            save |= finished();
            ImGui::SetItemTooltip("Distilled-water mash pH, if you've measured it (%.2f is typical for this "
                                  "type). 0 = use the typical value.",
                                  calc::default_di_ph(f.category, f.color_lovibond));
            ImGui::TableNextColumn();
            if (ImGui::SmallButton("×")) remove = entry.id;
            ImGui::PopID();
            if (save) save_row(entry);
        }
        ImGui::EndTable();
    }
    if (remove) {
        library_.remove(*remove);
        reload();
    }
}

void IngredientLibraryWindow::draw_hops() {
    if (ImGui::Button("Add hop")) {
        domain::Hop h;
        h.name = "New hop";
        library_.add(h);
        reload();
    }
    std::optional<std::int64_t> remove;
    if (ImGui::BeginTable("##hops", 5, kTableFlags, ImVec2(0.0f, -FLT_MIN))) {
        const float s = ui_scale_;
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Origin", ImGuiTableColumnFlags_WidthFixed, 90 * s);
        ImGui::TableSetupColumn("Form", ImGuiTableColumnFlags_WidthFixed, 100 * s);
        ImGui::TableSetupColumn("Alpha acid", ImGuiTableColumnFlags_WidthFixed, 90 * s);
        ImGui::TableSetupColumn("##remove", ImGuiTableColumnFlags_WidthFixed, 22 * s);
        ImGui::TableHeadersRow();

        for (auto& entry : hops_) {
            domain::Hop& h = entry.item;
            if (!matches(h.name, filter_)) continue;
            bool save = false;
            ImGui::PushID(static_cast<int>(entry.id));
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::InputText("##name", &h.name);
            save |= finished();
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::InputText("##origin", &h.origin);
            save |= finished();
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            save |= enum_combo("##form", h.form, kHopForms);
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            edit_percent("##alpha", h.alpha_acid);
            save |= finished();
            ImGui::TableNextColumn();
            if (ImGui::SmallButton("×")) remove = entry.id;
            ImGui::PopID();
            if (save) save_row(entry);
        }
        ImGui::EndTable();
    }
    if (remove) {
        library_.remove(*remove);
        reload();
    }
}

void IngredientLibraryWindow::draw_yeasts() {
    if (ImGui::Button("Add yeast")) {
        domain::Yeast y;
        y.name = "New yeast";
        library_.add(y);
        reload();
    }
    std::optional<std::int64_t> remove;
    if (ImGui::BeginTable("##yeasts", 9, kTableFlags, ImVec2(0.0f, -FLT_MIN))) {
        const float s = ui_scale_;
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Laboratory", ImGuiTableColumnFlags_WidthFixed, 100 * s);
        ImGui::TableSetupColumn("Product", ImGuiTableColumnFlags_WidthFixed, 80 * s);
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 80 * s);
        ImGui::TableSetupColumn("Form", ImGuiTableColumnFlags_WidthFixed, 85 * s);
        ImGui::TableSetupColumn("Attenuation", ImGuiTableColumnFlags_WidthFixed, 85 * s);
        ImGui::TableSetupColumn("B cells/pack", ImGuiTableColumnFlags_WidthFixed, 85 * s);
        ImGui::TableSetupColumn("B cells/g", ImGuiTableColumnFlags_WidthFixed, 75 * s);
        ImGui::TableSetupColumn("##remove", ImGuiTableColumnFlags_WidthFixed, 22 * s);
        ImGui::TableHeadersRow();

        for (auto& entry : yeasts_) {
            domain::Yeast& y = entry.item;
            if (!matches(y.name, filter_)) continue;
            bool save = false;
            ImGui::PushID(static_cast<int>(entry.id));
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::InputText("##name", &y.name);
            save |= finished();
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::InputText("##laboratory", &y.laboratory);
            save |= finished();
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::InputText("##product", &y.product_id);
            save |= finished();
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            save |= enum_combo("##type", y.type, kYeastTypes);
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            save |= enum_combo("##form", y.form, kYeastForms);
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            edit_percent("##attenuation", y.attenuation, 0);
            save |= finished();
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            edit_number("##per_pack", y.cells_per_pack_billion, 0);
            save |= finished();
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            edit_number("##per_gram", y.cells_per_gram_billion, 1);
            save |= finished();
            ImGui::TableNextColumn();
            if (ImGui::SmallButton("×")) remove = entry.id;
            ImGui::PopID();
            if (save) save_row(entry);
        }
        ImGui::EndTable();
    }
    if (remove) {
        library_.remove(*remove);
        reload();
    }
}


} // namespace ninkasi::ui
