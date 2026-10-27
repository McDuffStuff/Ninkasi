#pragma once
// The audit log: who changed what, and when.

#include "data/database.h"

#include <cstdint>
#include <string_view>

namespace ninkasi::data {

// Records one change. `action` is a short dotted name such as
// "element.create"; `details` is free text for people reading the log.
void record_audit(Database& db, std::int64_t user_id, std::string_view action, std::string_view entity,
                  std::int64_t entity_id, std::string_view details);

} // namespace ninkasi::data
