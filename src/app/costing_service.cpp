#include "app/costing_service.h"

#include <algorithm>
#include <tuple>
#include <utility>

namespace ninkasi::app {

namespace {

constexpr const char* kAction = "see what the beer cost";

} // namespace

CostingService::CostingService(data::Database& db, Session session)
    : db_(db), session_(std::move(session)), repository_(db) {}

void CostingService::refresh(std::uint64_t stamp) {
    if (stamp_ == stamp) return;
    reload();
    stamp_ = stamp;
}

void CostingService::reload() {
    require_admin(session_, kAction);
    data_ = repository_.load();
    draws_ = domain::cost_draws(data_.movements, data_.lots);
    batches_ = domain::batch_costs(draws_, data_.packaged, data_.batches);
    stamp_.reset();
}

const domain::BatchFacts* CostingService::batch(std::int64_t batch_id) const {
    const auto found = std::find_if(data_.batches.begin(), data_.batches.end(),
                                    [batch_id](const domain::BatchFacts& batch) { return batch.id == batch_id; });
    return found == data_.batches.end() ? nullptr : &*found;
}

std::optional<double> CostingService::movement_cost(const domain::PackagedMovement& movement) const {
    require_admin(session_, kAction);
    return domain::movement_cost(movement, batches_);
}

CostingService::PeriodCosts CostingService::period_costs(domain::Timestamp from, domain::Timestamp to) const {
    require_admin(session_, kAction);
    PeriodCosts costs;
    for (const domain::PackagedMovement& movement : data_.packaged) {
        if (movement.at < from || movement.at >= to) continue;
        const bool sold = domain::counts_as_sold(movement.reason);
        if (!sold && !domain::counts_as_lost(movement.reason)) continue;
        const std::optional<double> cost = domain::movement_cost(movement, batches_);
        if (!cost) {
            ++costs.uncosted;
            continue;
        }
        (sold ? costs.sold : costs.lost) += *cost;
    }
    for (const domain::BatchFacts& batch : data_.batches) {
        if (batch.status != domain::BatchStatus::Dumped || !batch.finished_at) continue;
        if (*batch.finished_at < from || *batch.finished_at >= to) continue;
        const auto found = batches_.find(batch.id);
        if (found != batches_.end()) costs.dumped += found->second.ingredients;
    }
    return costs;
}

std::map<std::int64_t, double> CostingService::sale_line_costs() const {
    require_admin(session_, kAction);
    std::map<std::int64_t, double> costs;
    for (const domain::PackagedMovement& movement : data_.packaged) {
        if (!movement.sale_line_id) continue;
        if (const std::optional<double> cost = domain::movement_cost(movement, batches_)) {
            costs[*movement.sale_line_id] += *cost;
        }
    }
    return costs;
}

std::vector<domain::PackagedStock> CostingService::packaged_on_hand() const {
    require_admin(session_, kAction);
    std::map<std::tuple<std::int64_t, domain::PackageType, double>, int> counts;
    for (const domain::PackagedMovement& movement : data_.packaged) {
        counts[{movement.batch_id, movement.type, movement.unit_volume_l}] += movement.quantity;
    }
    std::vector<domain::PackagedStock> stock;
    for (const auto& [key, count] : counts) {
        if (count <= 0) continue;
        domain::PackagedStock entry;
        std::tie(entry.batch_id, entry.type, entry.unit_volume_l) = key;
        entry.count = count;
        if (const domain::BatchFacts* facts = batch(entry.batch_id)) {
            entry.batch_code = facts->code;
            entry.beer = facts->beer;
        }
        stock.push_back(std::move(entry));
    }
    return stock;
}

domain::InventoryValue CostingService::inventory_value() const {
    require_admin(session_, kAction);
    return domain::inventory_value(data_.lots, data_.remaining, packaged_on_hand(), batches_);
}

} // namespace ninkasi::app
