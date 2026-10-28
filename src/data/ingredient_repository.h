#pragma once
// Reads and writes the ingredient library: fermentables, hops, yeasts and
// water profiles, each kept as JSON in the ingredients table.

#include "data/database.h"
#include "domain/recipe.h"

#include <cstdint>
#include <string_view>
#include <vector>

namespace ninkasi::data {

class IngredientRepository {
public:
    explicit IngredientRepository(Database& db) : db_(db) {}

    // Each kind of ingredient, by name.
    std::vector<domain::LibraryItem<domain::Fermentable>> fermentables();
    std::vector<domain::LibraryItem<domain::Hop>> hops();
    std::vector<domain::LibraryItem<domain::Yeast>> yeasts();
    std::vector<domain::LibraryItem<domain::WaterSource>> waters();

    // Each returns the new ingredient's id.
    std::int64_t insert(const domain::Fermentable& fermentable);
    std::int64_t insert(const domain::Hop& hop);
    std::int64_t insert(const domain::Yeast& yeast);
    std::int64_t insert(const domain::WaterSource& water);

    void update(std::int64_t id, const domain::Fermentable& fermentable);
    void update(std::int64_t id, const domain::Hop& hop);
    void update(std::int64_t id, const domain::Yeast& yeast);
    void update(std::int64_t id, const domain::WaterSource& water);

    void remove(std::int64_t id);

private:
    std::int64_t insert(std::string_view kind, std::string_view name, std::string_view body);
    void update(std::int64_t id, std::string_view name, std::string_view body);

    Database& db_;
};

} // namespace ninkasi::data
