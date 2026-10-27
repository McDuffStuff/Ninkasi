#include "data/ingredient_repository.h"

#include "data/recipe_json.h"

namespace ninkasi::data {

namespace {

constexpr std::string_view kFermentable = "fermentable";
constexpr std::string_view kHop = "hop";
constexpr std::string_view kYeast = "yeast";

// Loads every ingredient of `kind`, parsing each body with `parse`.
template <typename T, typename Parse>
std::vector<domain::LibraryItem<T>> load(Database& db, std::string_view kind, Parse parse) {
    std::vector<domain::LibraryItem<T>> items;
    auto stmt = db.prepare("SELECT id, body FROM ingredients WHERE kind = ? ORDER BY name COLLATE NOCASE, id;");
    stmt.bind(1, kind);
    while (stmt.step()) items.push_back({stmt.column_int64(0), parse(stmt.column_text(1))});
    return items;
}

} // namespace

std::vector<domain::LibraryItem<domain::Fermentable>> IngredientRepository::fermentables() {
    return load<domain::Fermentable>(db_, kFermentable, fermentable_from_json);
}

std::vector<domain::LibraryItem<domain::Hop>> IngredientRepository::hops() {
    return load<domain::Hop>(db_, kHop, hop_from_json);
}

std::vector<domain::LibraryItem<domain::Yeast>> IngredientRepository::yeasts() {
    return load<domain::Yeast>(db_, kYeast, yeast_from_json);
}

std::int64_t IngredientRepository::insert(const domain::Fermentable& f) { return insert(kFermentable, f.name, to_json(f)); }
std::int64_t IngredientRepository::insert(const domain::Hop& h) { return insert(kHop, h.name, to_json(h)); }
std::int64_t IngredientRepository::insert(const domain::Yeast& y) { return insert(kYeast, y.name, to_json(y)); }

void IngredientRepository::update(std::int64_t id, const domain::Fermentable& f) { update(id, f.name, to_json(f)); }
void IngredientRepository::update(std::int64_t id, const domain::Hop& h) { update(id, h.name, to_json(h)); }
void IngredientRepository::update(std::int64_t id, const domain::Yeast& y) { update(id, y.name, to_json(y)); }

void IngredientRepository::remove(std::int64_t id) {
    auto stmt = db_.prepare("DELETE FROM ingredients WHERE id = ?;");
    stmt.bind(1, id);
    stmt.execute();
}

std::int64_t IngredientRepository::insert(std::string_view kind, std::string_view name, std::string_view body) {
    auto stmt = db_.prepare("INSERT INTO ingredients (kind, name, body) VALUES (?, ?, ?);");
    stmt.bind(1, kind).bind(2, name).bind(3, body);
    stmt.execute();
    return db_.last_insert_id();
}

void IngredientRepository::update(std::int64_t id, std::string_view name, std::string_view body) {
    auto stmt = db_.prepare("UPDATE ingredients SET name = ?, body = ? WHERE id = ?;");
    stmt.bind(1, name).bind(2, body).bind(3, id);
    stmt.execute();
}

} // namespace ninkasi::data
