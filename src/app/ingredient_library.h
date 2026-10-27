#pragma once
// The brewery's ingredient library: fermentables, hops and yeasts that
// recipes pick from.
//
// On first use it is filled with a starter set of generic ingredients with
// typical values; edit them to match your suppliers' analyses. Changes are
// audited under the session's user.

#include "app/session.h"
#include "data/database.h"
#include "data/ingredient_repository.h"
#include "domain/recipe.h"

#include <cstdint>
#include <vector>

namespace ninkasi::app {

class IngredientLibrary {
public:
    IngredientLibrary(data::Database& db, Session session);

    const std::vector<domain::LibraryItem<domain::Fermentable>>& fermentables() const { return fermentables_; }
    const std::vector<domain::LibraryItem<domain::Hop>>& hops() const { return hops_; }
    const std::vector<domain::LibraryItem<domain::Yeast>>& yeasts() const { return yeasts_; }

    // Each throws std::invalid_argument if the name is blank, and returns the new id.
    std::int64_t add(const domain::Fermentable& fermentable);
    std::int64_t add(const domain::Hop& hop);
    std::int64_t add(const domain::Yeast& yeast);

    // Each throws std::invalid_argument if the name is blank or the id unknown.
    void update(std::int64_t id, const domain::Fermentable& fermentable);
    void update(std::int64_t id, const domain::Hop& hop);
    void update(std::int64_t id, const domain::Yeast& yeast);

    void remove(std::int64_t id);

private:
    template <typename T>
    std::vector<domain::LibraryItem<T>>& items();
    template <typename T>
    std::int64_t add_item(const T& item);
    template <typename T>
    void update_item(std::int64_t id, const T& item);

    void seed_once();
    void reload();

    data::Database& db_;
    Session session_;
    data::IngredientRepository repository_;
    std::vector<domain::LibraryItem<domain::Fermentable>> fermentables_;
    std::vector<domain::LibraryItem<domain::Hop>> hops_;
    std::vector<domain::LibraryItem<domain::Yeast>> yeasts_;
};

} // namespace ninkasi::app
