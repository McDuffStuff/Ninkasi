#include "domain/costing.h"

#include <algorithm>
#include <cmath>
#include <deque>

namespace ninkasi::domain {

namespace {

constexpr double kNegligible = 1e-9;

// An item's lots, oldest first.
using LotsByItem = std::map<std::int64_t, std::vector<const CostedLot*>>;

LotsByItem lots_by_item(std::span<const CostedLot> lots) {
    LotsByItem by_item;
    for (const CostedLot& lot : lots) by_item[lot.item_id].push_back(&lot);
    for (auto& [item, list] : by_item) {
        std::stable_sort(list.begin(), list.end(),
                         [](const CostedLot* a, const CostedLot* b) { return a->received_at < b->received_at; });
    }
    return by_item;
}

// The latest known unit cost of an item's lots on `at` (see latest_unit_cost).
std::optional<double> latest_of(const std::vector<const CostedLot*>& lots, Timestamp at) {
    std::optional<double> before;
    std::optional<double> after;
    for (const CostedLot* lot : lots) {
        const std::optional<double> unit = unit_cost(*lot);
        if (!unit) continue;
        if (lot->received_at <= at) {
            before = unit;
        } else if (!after) {
            after = unit;
        }
    }
    return before ? before : after;
}

// How much of a draw was costed each way.
struct Parts {
    double settled = 0.0;
    double estimated = 0.0;
    double unknown = 0.0;

    CostBasis basis() const {
        if (unknown > kNegligible) return CostBasis::Unknown;
        if (estimated > kNegligible) return CostBasis::Estimated;
        if (settled > kNegligible) return CostBasis::Settled;
        return CostBasis::Lot;
    }
};

} // namespace

std::optional<double> unit_cost(const CostedLot& lot) {
    if (!lot.cost || !(lot.quantity > 0.0)) return std::nullopt;
    return *lot.cost / lot.quantity;
}

CostBasis weaker(CostBasis a, CostBasis b) {
    return static_cast<int>(a) > static_cast<int>(b) ? a : b;
}

std::optional<double> latest_unit_cost(std::span<const CostedLot> lots, std::int64_t item_id, Timestamp at) {
    const LotsByItem by_item = lots_by_item(lots);
    const auto found = by_item.find(item_id);
    if (found == by_item.end()) return std::nullopt;
    return latest_of(found->second, at);
}

std::vector<CostedDraw> cost_draws(std::span<const StockMovement> movements, std::span<const CostedLot> lots) {
    const LotsByItem by_item = lots_by_item(lots);
    std::map<std::int64_t, const CostedLot*> lot_by_id;
    for (const CostedLot& lot : lots) lot_by_id[lot.id] = &lot;
    static const std::vector<const CostedLot*> kNoLots;
    const auto item_lots = [&](std::int64_t item) -> const std::vector<const CostedLot*>& {
        const auto found = by_item.find(item);
        return found == by_item.end() ? kNoLots : found->second;
    };

    // Each item's movements in the order they happened.
    std::vector<const StockMovement*> ordered;
    for (const StockMovement& movement : movements) ordered.push_back(&movement);
    std::stable_sort(ordered.begin(), ordered.end(), [](const StockMovement* a, const StockMovement* b) {
        if (a->item_id != b->item_id) return a->item_id < b->item_id;
        if (a->at != b->at) return a->at < b->at;
        return a->id < b->id;
    });

    std::vector<CostedDraw> draws;
    std::vector<Parts> parts;  // by draw

    // Charges `quantity` of draw `index` at `unit`, or estimates it.
    const auto charge = [&](std::size_t index, double quantity, std::optional<double> unit, bool settled) {
        CostedDraw& draw = draws[index];
        if (unit) {
            draw.cost += quantity * *unit;
            if (settled) parts[index].settled += quantity;
            return;
        }
        if (const std::optional<double> estimate = latest_of(item_lots(draw.item_id), draw.at)) {
            draw.cost += quantity * *estimate;
            parts[index].estimated += quantity;
        } else {
            parts[index].unknown += quantity;
        }
    };

    // Unlotted draws not yet covered by a delivery, oldest first: the
    // deliveries that settle the shortfall cover them in that order.
    struct Uncovered {
        std::size_t draw = 0;
        double left = 0.0;
    };
    std::deque<Uncovered> uncovered;
    const auto estimate_uncovered = [&] {
        for (const Uncovered& entry : uncovered) charge(entry.draw, entry.left, std::nullopt, false);
        uncovered.clear();
    };

    for (std::size_t i = 0; i < ordered.size(); ++i) {
        const StockMovement& movement = *ordered[i];
        if (i > 0 && ordered[i - 1]->item_id != movement.item_id) estimate_uncovered();

        if (movement.reason == StockReason::Settled) {
            // A settling is a pair: out of the lot, into the unlotted balance.
            // The lot's side draws nothing; the unlotted side says which lot
            // covered the earliest draws still uncovered.
            if (movement.lot_id || !(movement.quantity > 0.0)) continue;
            std::optional<double> unit;
            for (const StockMovement* other : ordered) {
                if (other->item_id != movement.item_id || other->reason != StockReason::Settled || !other->lot_id ||
                    other->at != movement.at || std::abs(other->quantity + movement.quantity) > kNegligible) {
                    continue;
                }
                const auto lot = lot_by_id.find(*other->lot_id);
                if (lot != lot_by_id.end()) unit = unit_cost(*lot->second);
                break;
            }
            double left = movement.quantity;
            while (left > kNegligible && !uncovered.empty()) {
                Uncovered& entry = uncovered.front();
                const double taken = std::min(left, entry.left);
                charge(entry.draw, taken, unit, true);
                entry.left -= taken;
                left -= taken;
                if (entry.left <= kNegligible) uncovered.pop_front();
            }
            continue;
        }
        if (!(movement.quantity < 0.0)) continue;  // stock in

        const std::size_t index = draws.size();
        draws.push_back({movement.id, movement.item_id, movement.batch_id, movement.reason, movement.at,
                         -movement.quantity, 0.0, CostBasis::Lot});
        parts.push_back({});
        if (movement.lot_id) {
            const auto lot = lot_by_id.find(*movement.lot_id);
            charge(index, -movement.quantity, lot != lot_by_id.end() ? unit_cost(*lot->second) : std::nullopt, false);
        } else {
            uncovered.push_back({index, -movement.quantity});
        }
    }
    estimate_uncovered();

    for (std::size_t i = 0; i < draws.size(); ++i) draws[i].basis = parts[i].basis();
    return draws;
}

// --- Batches ------------------------------------------------------------------------

std::optional<double> BatchCost::liquid_per_l() const {
    if (draws == 0) return std::nullopt;
    const double liters = finished && packaged_l > 0.0 ? packaged_l : (brewed_l > 0.0 ? brewed_l : packaged_l);
    if (!(liters > 0.0)) return std::nullopt;
    return ingredients / liters;
}

std::optional<double> BatchCost::package_cost(PackageType type, double unit_volume_l) const {
    const std::optional<double> per_l = liquid_per_l();
    if (!per_l) return std::nullopt;
    double cost = *per_l * unit_volume_l;
    if (type == PackageType::Can && cans_packaged > 0) cost += packaging / cans_packaged;
    return cost;
}

std::map<std::int64_t, BatchCost> batch_costs(std::span<const CostedDraw> draws,
                                              std::span<const PackagedMovement> packaged,
                                              std::span<const BatchFacts> batches) {
    std::map<std::int64_t, BatchCost> costs;
    for (const BatchFacts& batch : batches) {
        BatchCost& cost = costs[batch.id];
        cost.batch_id = batch.id;
        cost.brewed_l = batch.brewed_l;
        cost.finished = !is_active(batch.status);
    }
    for (const CostedDraw& draw : draws) {
        if (!draw.batch_id) continue;
        const auto found = costs.find(*draw.batch_id);
        if (found == costs.end()) continue;
        BatchCost& cost = found->second;
        if (draw.reason == StockReason::Brewed) {
            cost.ingredients += draw.cost;
        } else if (draw.reason == StockReason::Packaged) {
            cost.packaging += draw.cost;
        } else {
            continue;
        }
        ++cost.draws;
        cost.basis = weaker(cost.basis, draw.basis);
    }
    for (const PackagedMovement& movement : packaged) {
        if (movement.reason != PackagedReason::Packaged) continue;
        const auto found = costs.find(movement.batch_id);
        if (found == costs.end()) continue;
        found->second.packaged_l += movement.quantity * movement.unit_volume_l;
        if (movement.type == PackageType::Can) {
            found->second.cans_packaged += movement.quantity;
            found->second.can_volume_l = movement.unit_volume_l;
        }
    }
    return costs;
}

// --- Beer leaving packaged goods ---------------------------------------------------------

bool counts_as_sold(PackagedReason reason) {
    switch (reason) {
    case PackagedReason::Sold:
    case PackagedReason::Taproom:
    case PackagedReason::Exported:
    case PackagedReason::Supplies:
    case PackagedReason::Returned: return true;
    default:                       return false;
    }
}

bool counts_as_lost(PackagedReason reason) {
    switch (reason) {
    case PackagedReason::ConsumedOnPremises:
    case PackagedReason::Research:
    case PackagedReason::LabSamples:
    case PackagedReason::Unfit:
    case PackagedReason::Destroyed:
    case PackagedReason::Lost:
    case PackagedReason::Shortage:
    case PackagedReason::Overage:
    case PackagedReason::SameOwnership: return true;
    default:                            return false;
    }
}

std::optional<double> movement_cost(const PackagedMovement& movement, const std::map<std::int64_t, BatchCost>& costs) {
    const auto found = costs.find(movement.batch_id);
    if (found == costs.end()) return std::nullopt;
    const std::optional<double> each = found->second.package_cost(movement.type, movement.unit_volume_l);
    if (!each) return std::nullopt;
    return -movement.quantity * *each;
}

// --- What the stock is worth ----------------------------------------------------------

InventoryValue inventory_value(std::span<const CostedLot> lots, std::span<const LotStock> remaining,
                               std::span<const PackagedStock> packaged,
                               const std::map<std::int64_t, BatchCost>& costs) {
    InventoryValue value;
    const LotsByItem by_item = lots_by_item(lots);
    std::map<std::int64_t, const CostedLot*> lot_by_id;
    for (const CostedLot& lot : lots) lot_by_id[lot.id] = &lot;

    for (const LotStock& stock : remaining) {
        if (!(stock.remaining > kNegligible)) continue;
        const auto found = lot_by_id.find(stock.lot_id);
        if (found == lot_by_id.end()) continue;
        const CostedLot& lot = *found->second;
        if (const std::optional<double> unit = unit_cost(lot)) {
            value.stock += stock.remaining * *unit;
            continue;
        }
        const auto item = by_item.find(lot.item_id);
        const std::optional<double> estimate =
            item == by_item.end() ? std::nullopt : latest_of(item->second, lot.received_at);
        if (estimate) {
            value.stock += stock.remaining * *estimate;
            ++value.estimated;
        } else {
            ++value.uncosted;
        }
    }

    for (const PackagedStock& stock : packaged) {
        const auto found = costs.find(stock.batch_id);
        const std::optional<double> each =
            found == costs.end() ? std::nullopt : found->second.package_cost(stock.type, stock.unit_volume_l);
        if (each) {
            value.packaged_beer += stock.count * *each;
        } else {
            ++value.uncosted;
        }
    }

    // Beer still in the tanks: what isn't packaged yet of its ingredients' cost.
    for (const auto& [id, cost] : costs) {
        if (cost.finished) continue;
        const std::optional<double> per_l = cost.liquid_per_l();
        if (!per_l) continue;
        value.in_progress += std::max(cost.ingredients - cost.packaged_l * *per_l, 0.0);
    }
    return value;
}

} // namespace ninkasi::domain
