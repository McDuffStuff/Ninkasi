#pragma once
// Reads and writes recipes.

#include "data/database.h"
#include "domain/recipe.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ninkasi::data {

// What a recipe list shows, without loading every recipe.
struct RecipeSummary {
    std::int64_t id = 0;
    std::string name;
    std::string style;
    std::string updated_at;  // ISO-8601 UTC
};

class RecipeRepository {
public:
    explicit RecipeRepository(Database& db) : db_(db) {}

    // Every recipe, by name.
    std::vector<RecipeSummary> list();
    std::optional<domain::Recipe> load(std::int64_t id);

    // Inserts the recipe (its id is ignored) and returns the new id.
    std::int64_t insert(const domain::Recipe& recipe);
    void update(const domain::Recipe& recipe);
    void remove(std::int64_t id);

private:
    Database& db_;
};

} // namespace ninkasi::data
