#pragma once
// The brewery floor: the equipment placed on it and every change made to it.
//
// The UI reads elements() and changes them only through these methods. Each
// change is saved in one transaction, and the meaningful ones (adding,
// renaming, deleting...) are written to the audit log under the session's
// user. Moving or resizing is layout, not history, so it isn't audited.

#include "app/session.h"
#include "calc/units.h"
#include "data/database.h"
#include "data/element_repository.h"
#include "domain/element.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ninkasi::app {

class FloorService {
public:
    // Loads the floor from the database.
    FloorService(data::Database& db, Session session);

    const std::vector<domain::Element>& elements() const { return elements_; }
    // nullptr if there is no such element. The pointer is invalidated by add() and remove().
    const domain::Element* find(std::int64_t id) const;

    // Places a new element of `type`, at its default size, centered on the
    // given floor position and named after its type ("Fermenter 3").
    // Returns the new element's id.
    std::int64_t add(domain::ElementType type, double center_x, double center_y);

    // Moves or resizes an element; sizes below kMinElementSize are raised to it.
    void set_rect(std::int64_t id, const domain::Rect& rect);

    // Throws std::invalid_argument if `name` is blank.
    void rename(std::int64_t id, std::string_view name);

    // Throws std::invalid_argument if the capacity is negative.
    void set_capacity(std::int64_t id, std::optional<double> capacity_l);

    // Chooses the unit this element shows for one measure; nullopt follows
    // the brewery-wide preference.
    void set_unit_override(std::int64_t id, calc::Measure measure, std::optional<calc::Unit> unit);

    void remove(std::int64_t id);

private:
    // Throws std::invalid_argument if there is no such element.
    domain::Element& get(std::int64_t id);
    std::string default_name(domain::ElementType type) const;

    data::Database& db_;
    Session session_;
    data::ElementRepository repository_;
    std::vector<domain::Element> elements_;
};

} // namespace ninkasi::app
