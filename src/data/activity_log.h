#pragma once
// The activity log table: production events for people to read.

#include "data/database.h"
#include "domain/activity.h"

#include <cstdint>
#include <vector>

namespace ninkasi::data {

// Records one entry (its id and user are ignored; `user_id` says who).
void record_activity(Database& db, std::int64_t user_id, const domain::Activity& entry);

// Entries about a batch, or involving an element, newest first.
std::vector<domain::Activity> activity_for_batch(Database& db, std::int64_t batch_id, int limit);
std::vector<domain::Activity> activity_for_element(Database& db, std::int64_t element_id, int limit);

} // namespace ninkasi::data
