#pragma once
// Reads what costing needs, all at once: every lot and what's left of it,
// the stock ledger, the packaged goods ledger and the batches.

#include "data/database.h"
#include "domain/costing.h"

#include <vector>

namespace ninkasi::data {

struct CostingData {
    std::vector<domain::CostedLot> lots;
    std::vector<domain::LotStock> remaining;  // of each lot with some left
    std::vector<domain::StockMovement> movements;
    std::vector<domain::PackagedMovement> packaged;
    std::vector<domain::BatchFacts> batches;
};

class CostingRepository {
public:
    explicit CostingRepository(Database& db) : db_(db) {}

    CostingData load();

private:
    Database& db_;
};

} // namespace ninkasi::data
