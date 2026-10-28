#pragma once
// Reads and writes floor elements. (The element_unit_overrides table, from
// when equipment could have its own units, is no longer read or written.)

#include "calc/units.h"
#include "data/database.h"
#include "domain/element.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace ninkasi::data {

class ElementRepository {
public:
    explicit ElementRepository(Database& db) : db_(db) {}

    // Every element, oldest first. Rows with a type this version doesn't
    // know are skipped.
    std::vector<domain::Element> load_all();

    // Inserts the element (its id is ignored); returns the new id.
    std::int64_t insert(const domain::Element& element);

    // Saves name, position, size and capacity.
    void update(const domain::Element& element);

    // Deletes the element.
    void remove(std::int64_t id);

    // The floor's background picture, if it has one (its pixel size isn't read).
    std::optional<domain::FloorBackground> load_background();
    // Replaces the picture, and where it is.
    void save_background(const domain::FloorBackground& background);
    // Moves, resizes or fades the picture without writing it again.
    void place_background(double x, double y, double width, double opacity);
    void remove_background();

private:
    Database& db_;
};

} // namespace ninkasi::data
