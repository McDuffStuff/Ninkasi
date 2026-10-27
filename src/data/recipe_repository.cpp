#include "data/recipe_repository.h"

#include "data/recipe_json.h"

namespace ninkasi::data {

std::vector<RecipeSummary> RecipeRepository::list() {
    std::vector<RecipeSummary> recipes;
    auto stmt = db_.prepare("SELECT id, name, style, updated_at FROM recipes ORDER BY name COLLATE NOCASE, id;");
    while (stmt.step()) {
        recipes.push_back({stmt.column_int64(0), stmt.column_text(1), stmt.column_text(2), stmt.column_text(3)});
    }
    return recipes;
}

std::optional<domain::Recipe> RecipeRepository::load(std::int64_t id) {
    auto stmt = db_.prepare("SELECT body FROM recipes WHERE id = ?;");
    stmt.bind(1, id);
    if (!stmt.step()) return std::nullopt;
    domain::Recipe recipe = recipe_from_json(stmt.column_text(0));
    recipe.id = id;
    return recipe;
}

std::int64_t RecipeRepository::insert(const domain::Recipe& recipe) {
    auto stmt = db_.prepare("INSERT INTO recipes (name, style, body) VALUES (?, ?, ?);");
    stmt.bind(1, recipe.name).bind(2, recipe.style).bind(3, to_json(recipe));
    stmt.execute();
    return db_.last_insert_id();
}

void RecipeRepository::update(const domain::Recipe& recipe) {
    auto stmt = db_.prepare(
        "UPDATE recipes SET name = ?, style = ?, body = ?, "
        "updated_at = strftime('%Y-%m-%dT%H:%M:%SZ', 'now') WHERE id = ?;");
    stmt.bind(1, recipe.name).bind(2, recipe.style).bind(3, to_json(recipe)).bind(4, recipe.id);
    stmt.execute();
}

void RecipeRepository::remove(std::int64_t id) {
    auto stmt = db_.prepare("DELETE FROM recipes WHERE id = ?;");
    stmt.bind(1, id);
    stmt.execute();
}

} // namespace ninkasi::data
