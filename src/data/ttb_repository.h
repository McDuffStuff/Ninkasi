#pragma once
// Reads the two ledgers TTB reports are built from: the volume ledger (beer
// in tanks) and the packaged goods ledger (kegs and cans).

#include "data/database.h"
#include "domain/batch.h"
#include "domain/packaging.h"
#include "domain/timestamp.h"

#include <vector>

namespace ninkasi::data {

class TtbRepository {
public:
    explicit TtbRepository(Database& db) : db_(db) {}

    // Every row from before `end`, oldest first.
    std::vector<domain::VolumeEvent> volume_events(domain::Timestamp end);
    std::vector<domain::PackagedMovement> packaged_movements(domain::Timestamp end);

private:
    Database& db_;
};

} // namespace ninkasi::data
