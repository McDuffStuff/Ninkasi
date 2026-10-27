#pragma once
// Recipes: listing, creating, saving and deleting them, plus the brewery's
// defaults for new recipes (batch size, equipment, water...).
//
// Creating, saving and deleting are audited under the session's user.

#include "app/session.h"
#include "data/database.h"
#include "data/recipe_repository.h"
#include "data/settings_repository.h"
#include "domain/recipe.h"

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace ninkasi::app {

class RecipeService {
public:
    RecipeService(data::Database& db, Session session);

    std::vector<data::RecipeSummary> list();
    std::optional<domain::Recipe> load(std::int64_t id);

    // A new, unsaved recipe built from the brewery's defaults.
    domain::Recipe new_recipe(std::string_view name);

    // Saves a new recipe and returns its id. Throws std::invalid_argument if
    // the name is blank.
    std::int64_t create(const domain::Recipe& recipe);
    // Saves changes to an existing recipe. Throws std::invalid_argument if
    // the name is blank.
    void save(const domain::Recipe& recipe);
    // Copies a recipe as "<name> (copy)" and returns the copy's id.
    std::int64_t duplicate(std::int64_t id);
    void remove(std::int64_t id);

    // The template new recipes start from; its ingredient lists are ignored.
    domain::Recipe defaults();
    void set_defaults(const domain::Recipe& defaults);

private:
    data::Database& db_;
    Session session_;
    data::RecipeRepository repository_;
    data::SettingsRepository settings_;
};

// The defaults before the brewery sets its own: a 10 bbl brewhouse.
domain::Recipe built_in_recipe_defaults();

} // namespace ninkasi::app
