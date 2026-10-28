#pragma once
// The recipe library: every recipe, with new, open, duplicate, delete and
// BeerJSON import.

#include "app/ingredient_library.h"
#include "app/recipe_service.h"
#include "data/recipe_repository.h"
#include "ui/recipes/recipe_editors.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace ninkasi::ui {

class RecipeLibraryWindow {
public:
    RecipeLibraryWindow(app::RecipeService& recipes, app::IngredientLibrary& library, RecipeEditors& editors,
                        float ui_scale);

    // Opens the window, or brings it to the front.
    void show();
    // Reloads the recipe list (e.g. after a recipe was saved).
    void refresh();
    void draw();

    // Imports every recipe in the BeerJSON files, and adds their water
    // profiles to the ingredient library; reports what happened.
    void import_files(const std::vector<std::filesystem::path>& files);

private:
    void draw_list();
    void draw_new_recipe_popup();
    void draw_delete_confirmation();
    void draw_import_report();

    app::RecipeService& recipes_;
    app::IngredientLibrary& library_;
    RecipeEditors& editors_;
    float ui_scale_;

    bool visible_ = false;
    bool focus_ = false;
    std::vector<data::RecipeSummary> list_;
    std::string filter_;
    std::optional<std::int64_t> selected_;

    bool open_new_popup_ = false;
    std::string new_name_;
    bool open_delete_popup_ = false;
    std::int64_t delete_id_ = 0;
    bool open_import_report_ = false;
    std::vector<std::string> import_report_;
};

} // namespace ninkasi::ui
