#pragma once
// Recipe editor windows, one per open recipe.
//
// Edits change a working copy, so the numbers update while the brewer types.
// The copy is saved a moment after they stop, when the window closes, and
// when Ninkasi exits. Open recipes are reopened next time.

#include "app/ingredient_library.h"
#include "app/recipe_service.h"
#include "calc/units.h"
#include "data/settings_repository.h"
#include "domain/recipe.h"
#include "domain/recipe_math.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace ninkasi::ui {

class RecipeEditors {
public:
    RecipeEditors(app::RecipeService& recipes, app::IngredientLibrary& library, data::SettingsRepository& settings,
                  const calc::UnitPreferences& units, const domain::CalcSettings& calc_settings, float ui_scale);
    ~RecipeEditors();  // saves any unsaved changes

    RecipeEditors(const RecipeEditors&) = delete;
    RecipeEditors& operator=(const RecipeEditors&) = delete;

    // Opens the recipe's window, or brings it to the front.
    void open(std::int64_t recipe_id);
    // Closes a recipe's window without saving (e.g. after deleting it).
    void discard(std::int64_t recipe_id);
    void draw();

    // Called after a recipe is saved, e.g. to refresh the recipe list.
    std::function<void()> on_saved;

private:
    struct Editor {
        domain::Recipe recipe;
        bool dirty = false;
        double last_change = 0.0;
        bool focus = false;
        bool restore_tab = true;  // open on the last-used tab
        std::string error;
    };

    // Draws a section tab; remembers it as the last-used tab when picked.
    bool begin_tab(Editor& editor, const char* name);

    // Returns false once the brewer closes the window.
    bool draw_editor(Editor& editor);
    // Saves the working copy; on failure leaves it dirty and explains why.
    bool save(Editor& editor);
    void save_open_list();

    app::RecipeService& recipes_;
    app::IngredientLibrary& library_;
    data::SettingsRepository& settings_;
    const calc::UnitPreferences& units_;
    const domain::CalcSettings& calc_settings_;
    float ui_scale_;
    std::string last_tab_;
    std::vector<Editor> editors_;
};

} // namespace ninkasi::ui
