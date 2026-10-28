#include "ui/recipes/recipe_editors.h"

#include "ui/recipes/recipe_sections.h"
#include "ui/widgets.h"

#include <imgui.h>

#include <algorithm>
#include <cstdlib>
#include <exception>

namespace ninkasi::ui {

namespace {

constexpr const char* kOpenEditorsKey = "recipes.open_editors";
constexpr const char* kLastTabKey = "recipes.editor_tab";
constexpr double kSaveDelay = 1.0;  // seconds after the last change
const ImVec4 kWarning{0.95f, 0.65f, 0.20f, 1.0f};

bool is_blank(const std::string& text) {
    return text.find_first_not_of(" \t\r\n") == std::string::npos;
}

} // namespace

RecipeEditors::RecipeEditors(app::RecipeService& recipes, app::IngredientLibrary& library,
                             data::SettingsRepository& settings, const calc::UnitPreferences& units,
                             const domain::CalcSettings& calc_settings, float ui_scale)
    : recipes_(recipes),
      library_(library),
      settings_(settings),
      units_(units),
      calc_settings_(calc_settings),
      ui_scale_(ui_scale),
      last_tab_(settings.get_or(kLastTabKey, "")) {
    // Saved as comma-separated recipe ids, e.g. "3,7".
    const std::string saved = settings_.get_or(kOpenEditorsKey, "");
    const char* cursor = saved.c_str();
    while (*cursor != '\0') {
        char* end = nullptr;
        const long long id = std::strtoll(cursor, &end, 10);
        if (end == cursor) break;
        if (std::optional<domain::Recipe> recipe = recipes_.load(id)) editors_.push_back({std::move(*recipe)});
        cursor = (*end == ',') ? end + 1 : end;
    }
}

RecipeEditors::~RecipeEditors() {
    // Whatever on_saved refreshes may already be gone during shutdown.
    on_saved = nullptr;
    for (Editor& editor : editors_) {
        if (editor.dirty) save(editor);
    }
}

void RecipeEditors::open(std::int64_t recipe_id) {
    for (Editor& editor : editors_) {
        if (editor.recipe.id == recipe_id) {
            editor.focus = true;
            return;
        }
    }
    std::optional<domain::Recipe> recipe = recipes_.load(recipe_id);
    if (!recipe) return;
    editors_.push_back({std::move(*recipe)});
    editors_.back().focus = true;
    save_open_list();
}

void RecipeEditors::discard(std::int64_t recipe_id) {
    if (std::erase_if(editors_, [recipe_id](const Editor& e) { return e.recipe.id == recipe_id; }) > 0) {
        save_open_list();
    }
}

void RecipeEditors::draw() {
    bool closed_any = false;
    for (std::size_t i = 0; i < editors_.size();) {
        if (draw_editor(editors_[i])) {
            ++i;
        } else {
            editors_.erase(editors_.begin() + static_cast<std::ptrdiff_t>(i));
            closed_any = true;
        }
    }
    if (closed_any) save_open_list();
}

bool RecipeEditors::draw_editor(Editor& editor) {
    domain::Recipe& recipe = editor.recipe;
    const float stagger = static_cast<float>(recipe.id % 6) * 28.0f * ui_scale_;
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter() + ImVec2(stagger, stagger), ImGuiCond_FirstUseEver,
                            {0.5f, 0.5f});
    ImGui::SetNextWindowSize(ImVec2(960.0f, 660.0f) * ui_scale_, ImGuiCond_FirstUseEver);
    const bool raise = editor.focus;
    if (editor.focus) {
        ImGui::SetNextWindowFocus();
        editor.focus = false;
    }

    // "###" keeps the window's identity while the visible title changes.
    const std::string title = (is_blank(recipe.name) ? std::string("Untitled recipe") : recipe.name) +
                              (editor.dirty ? " *" : "") + "###recipe_" + std::to_string(recipe.id);
    bool open = true;
    const bool expanded = ImGui::Begin(title.c_str(), &open);
    if (raise) raise_os_window();
    if (expanded) {
        const RecipeContext context{units_, library_, ui_scale_};
        bool changed = draw_recipe_header(recipe, context);
        const domain::RecipeStats stats = domain::calculate(recipe, calc_settings_);

        ImGui::Spacing();
        draw_recipe_stats(stats, context);
        if (!editor.error.empty()) ImGui::TextColored(kWarning, "%s", editor.error.c_str());
        ImGui::Spacing();

        if (ImGui::BeginTabBar("##sections")) {
            if (begin_tab(editor, "Fermentables")) {
                changed |= draw_fermentables(recipe, stats, context);
                ImGui::EndTabItem();
            }
            if (begin_tab(editor, "Hops")) {
                changed |= draw_hops(recipe, stats, context);
                ImGui::EndTabItem();
            }
            if (begin_tab(editor, "Yeast")) {
                changed |= draw_yeast(recipe, stats, context);
                ImGui::EndTabItem();
            }
            if (begin_tab(editor, "Mash")) {
                changed |= draw_mash(recipe, stats, context);
                ImGui::EndTabItem();
            }
            if (begin_tab(editor, "Water")) {
                changed |= draw_water(recipe, stats, context);
                ImGui::EndTabItem();
            }
            if (begin_tab(editor, "Other additions")) {
                changed |= draw_other_additions(recipe, context);
                ImGui::EndTabItem();
            }
            if (begin_tab(editor, "Equipment")) {
                changed |= draw_equipment(recipe, context);
                ImGui::SeparatorText("Volumes");
                draw_volumes(stats, context);
                ImGui::EndTabItem();
            }
            if (begin_tab(editor, "Notes")) {
                changed |= draw_notes(recipe);
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        editor.restore_tab = false;

        if (changed) {
            editor.dirty = true;
            editor.last_change = ImGui::GetTime();
        }
    }
    ImGui::End();

    const bool quiet_long_enough = ImGui::GetTime() - editor.last_change > kSaveDelay;
    if (editor.dirty && (quiet_long_enough || !open) && !save(editor)) {
        editor.last_change = ImGui::GetTime();  // don't retry every frame
    }
    return open;
}

bool RecipeEditors::begin_tab(Editor& editor, const char* name) {
    const bool restoring = editor.restore_tab && last_tab_ == name;
    const bool open = ImGui::BeginTabItem(name, nullptr, restoring ? ImGuiTabItemFlags_SetSelected : 0);
    if (open && !editor.restore_tab && last_tab_ != name) {
        last_tab_ = name;
        settings_.set(kLastTabKey, last_tab_);
    }
    return open;
}

bool RecipeEditors::save(Editor& editor) {
    if (is_blank(editor.recipe.name)) {
        editor.error = "Give the recipe a name to save it.";
        return false;
    }
    try {
        recipes_.save(editor.recipe);
    } catch (const std::exception& e) {
        editor.error = std::string("Couldn't save: ") + e.what();
        return false;
    }
    editor.dirty = false;
    editor.error.clear();
    if (on_saved) on_saved();
    return true;
}

void RecipeEditors::save_open_list() {
    std::string ids;
    for (const Editor& editor : editors_) {
        if (!ids.empty()) ids += ',';
        ids += std::to_string(editor.recipe.id);
    }
    settings_.set(kOpenEditorsKey, ids);
}

} // namespace ninkasi::ui
