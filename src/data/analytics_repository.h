#pragma once
// Reads everything the Insights window works from, all at once: the
// ledgers costing reads (see CostingRepository), sales, brews, volume
// events, vessels and inventory items.

#include "data/costing_repository.h"
#include "data/database.h"
#include "domain/batch.h"
#include "domain/inventory.h"
#include "domain/sales.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ninkasi::data {

struct BrewFact {
    std::int64_t batch_id = 0;
    domain::Timestamp started_at{};
    std::optional<domain::Timestamp> knocked_out_at;
    double volume_l = 0.0;
};

// A vessel: a tank beer stands in.
struct VesselFact {
    std::int64_t id = 0;
    std::string name;
};

struct AnalyticsData {
    CostingData ledgers;  // lots, stock and packaged movements, batches
    std::vector<domain::Sale> sales;
    std::vector<BrewFact> brews;
    std::vector<domain::VolumeEvent> volume_events;
    std::vector<VesselFact> vessels;
    std::vector<domain::InventoryItem> items;  // archived ones too
    int kegs_out = 0;                          // at customers
};

class AnalyticsRepository {
public:
    explicit AnalyticsRepository(Database& db) : db_(db) {}

    AnalyticsData load();

private:
    Database& db_;
};

} // namespace ninkasi::data
