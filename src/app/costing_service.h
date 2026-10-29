#pragma once
// What the beer cost (see domain/costing.h), worked out from the ledgers and
// kept until they change. Costs are the brewery's money, so for anyone but
// an admin every call throws NotAllowed.

#include "app/session.h"
#include "data/costing_repository.h"
#include "data/database.h"
#include "domain/costing.h"
#include "domain/timestamp.h"

#include <cstdint>
#include <map>
#include <optional>
#include <vector>

namespace ninkasi::app {

class CostingService {
public:
    CostingService(data::Database& db, Session session);

    bool allowed() const { return session_.is_admin(); }

    // Works the costs out again if `stamp` (the revisions of whatever feeds
    // them, combined) has changed since they were.
    void refresh(std::uint64_t stamp);
    // Works them out again now.
    void reload();

    const data::CostingData& data() const { return data_; }
    const std::vector<domain::CostedDraw>& draws() const { return draws_; }
    const std::map<std::int64_t, domain::BatchCost>& batches() const { return batches_; }
    const domain::BatchFacts* batch(std::int64_t batch_id) const;

    // What a packaged goods movement carried at cost (see domain::movement_cost).
    std::optional<double> movement_cost(const domain::PackagedMovement& movement) const;

    // The beer that left, at cost, from `from` (included) to `to` (not).
    struct PeriodCosts {
        double sold = 0.0;    // the cost of goods sold
        double lost = 0.0;    // given away, lost or destroyed once packaged
        double dumped = 0.0;  // the ingredients of batches dumped, none of them packaged
        int uncosted = 0;     // removals whose batch has no cost, left out
    };
    PeriodCosts period_costs(domain::Timestamp from, domain::Timestamp to) const;

    // The cost of the beer sold on each sale line it was taken out for.
    std::map<std::int64_t, double> sale_line_costs() const;

    // Packaged beer on hand, by batch and package.
    std::vector<domain::PackagedStock> packaged_on_hand() const;
    domain::InventoryValue inventory_value() const;

private:
    data::Database& db_;
    Session session_;
    data::CostingRepository repository_;
    std::optional<std::uint64_t> stamp_;
    data::CostingData data_;
    std::vector<domain::CostedDraw> draws_;
    std::map<std::int64_t, domain::BatchCost> batches_;
};

} // namespace ninkasi::app
