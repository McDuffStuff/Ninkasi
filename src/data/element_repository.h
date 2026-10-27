#pragma once
// Reads and writes floor elements and their unit overrides.

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

    // Every element, oldest first, with its unit overrides. Rows with a type
    // or unit this version doesn't know are skipped.
    std::vector<domain::Element> load_all();

    // Inserts the element (its id and overrides are ignored); returns the new id.
    std::int64_t insert(const domain::Element& element);

    // Saves name, position, size and capacity.
    void update(const domain::Element& element);

    // Deletes the element; its unit overrides go with it.
    void remove(std::int64_t id);

    // Sets the unit an element uses for one dimension, or clears it.
    void set_unit_override(std::int64_t element_id, calc::Dimension dimension, std::optional<calc::Unit> unit);

private:
    Database& db_;
};

} // namespace ninkasi::data
