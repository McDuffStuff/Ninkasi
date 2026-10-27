#pragma once
// The activity log: what happened in the brewery, when and by whom, as shown
// on each vessel's and batch's Log tab.
//
// Entries are text written when the event happens. Quantities in the text
// are stored as tokens in canonical units ("{volume:1173.5}") and formatted
// when shown, so they always appear in the reader's current units.

#include "calc/units.h"
#include "domain/timestamp.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace ninkasi::domain {

struct Activity {
    std::int64_t id = 0;
    Timestamp at{};
    std::string user;  // who did it
    std::string kind;  // what happened: "brew", "transfer", "reading", "note"...
    std::optional<std::int64_t> batch_id;
    std::optional<std::int64_t> element_id;
    std::optional<std::int64_t> other_element_id;  // the other end of a transfer or knock-out
    std::string text;
};

// Tokens for quantities in activity text.
std::string volume_token(double volume_l);
std::string gravity_token(double sg);
std::string temperature_token(double temp_c);

// `text` with each quantity token formatted in `units`. Anything that isn't
// a well-formed token is left as it is.
std::string render_quantities(std::string_view text, const calc::UnitPreferences& units);

} // namespace ninkasi::domain
