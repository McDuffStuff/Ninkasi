#pragma once
// The ingredient library: fermentables, hops and yeasts that recipes pick
// from. Edits are saved when the brewer finishes with a field.

#include "app/ingredient_library.h"
#include "calc/units.h"
#include "domain/recipe.h"

#include <string>
#include <vector>

namespace ninkasi::ui {

class IngredientLibraryWindow {
public:
    IngredientLibraryWindow(app::IngredientLibrary& library, const calc::UnitPreferences& units, float ui_scale);

    // Opens the window, or brings it to the front.
    void show();
    void draw();

private:
    void reload();
    void draw_fermentables();
    void draw_hops();
    void draw_yeasts();
    void draw_waters();
    // Saves an edited row; a rejected edit is undone on the next frame.
    template <typename T>
    void save_row(const domain::LibraryItem<T>& entry);

    app::IngredientLibrary& library_;
    const calc::UnitPreferences& units_;
    float ui_scale_;
    bool visible_ = false;
    bool focus_ = false;
    std::string filter_;
    std::string error_;
    bool needs_reload_ = false;

    // Working copies, so typing isn't interrupted; each row is saved to the
    // library when one of its fields is finished.
    std::vector<domain::LibraryItem<domain::Fermentable>> fermentables_;
    std::vector<domain::LibraryItem<domain::Hop>> hops_;
    std::vector<domain::LibraryItem<domain::Yeast>> yeasts_;
    std::vector<domain::LibraryItem<domain::WaterSource>> waters_;
};

} // namespace ninkasi::ui
