#pragma once
// The brewery floor: the equipment placed on it and every change made to it.
//
// The UI reads elements() and changes them only through these methods. Each
// change is saved in one transaction, and the meaningful ones (adding,
// renaming, deleting...) are written to the audit log under the session's
// user. Moving or resizing is layout, not history, so it isn't audited.
//
// Setting up equipment (adding, moving, resizing, renaming, changing its
// capacity, removing) is for admins; the others throw NotAllowed for anyone else.

#include "app/session.h"
#include "calc/units.h"
#include "data/database.h"
#include "data/element_repository.h"
#include "domain/element.h"

#include <cstddef>
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

    // Whether the signed-in person can set up equipment (admins can).
    bool can_set_up() const { return session_.is_admin(); }

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

    // Why an element can't be removed (it holds beer, or has a brew waiting
    // for its knock-out), or nullopt if it can.
    std::optional<std::string> removal_blocker(std::int64_t id);
    // Throws std::invalid_argument if the element is in use (see removal_blocker).
    void remove(std::int64_t id);

    // --- The background picture -----------------------------------------------------------
    // The largest picture file accepted.
    static constexpr std::size_t kMaxBackgroundBytes = 25 * 1024 * 1024;

    const std::optional<domain::FloorBackground>& background() const { return background_; }
    // Goes up whenever the picture itself changes, so the UI knows to load it again.
    int background_revision() const { return background_revision_; }
    // Admins only, like the rest of the layout (NotAllowed otherwise).
    // Puts the picture in `file` under the floor. A first picture is sized to
    // cover the equipment; a replacement keeps the old one's place. Throws
    // std::invalid_argument if it isn't a PNG, JPEG, BMP or GIF, or is too big.
    void set_background(std::vector<unsigned char> file, std::string_view file_name);
    // Moves and resizes it (`width` in floor units) and sets how solid it is (0.05 to 1).
    void place_background(double x, double y, double width, double opacity);
    // Sizes it to cover the equipment, with a margin, centered on it.
    void fit_background();
    void remove_background();

private:
    // Throws std::invalid_argument if there is no such element.
    domain::Element& get(std::int64_t id);
    // Places `background` over the equipment (or at the origin if there's none).
    void cover_equipment(domain::FloorBackground& background) const;
    std::string default_name(domain::ElementType type) const;

    data::Database& db_;
    Session session_;
    data::ElementRepository repository_;
    std::vector<domain::Element> elements_;
    std::optional<domain::FloorBackground> background_;
    int background_revision_ = 0;
};

} // namespace ninkasi::app
