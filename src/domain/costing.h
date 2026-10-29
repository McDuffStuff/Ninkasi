#pragma once
// What the beer cost to make, traced from the invoices its ingredients came
// on, and what the brewery's stock is worth.
//
// A lot of stock cost what its invoice line cost, so a unit of it costs that
// over the quantity received. A batch costs what was drawn for it: its
// ingredients when it was brewed and its cans when it was packaged. Stock
// used before its delivery was logged is drawn from no lot; the delivery
// that later covers it (see InventoryService::receive) says what it cost.
// Stock never covered, or drawn from a lot entered without a cost, is costed
// at the item's latest known cost and marked as estimated.
//
// Beer costs its ingredients over the liters packaged, so beer lost on the
// way (in transfers, at packaging, dumped) makes the rest cost more. A
// package costs its beer and, for a can, the can. Beer leaving packaged
// goods takes that cost with it: sold, it's the cost of goods sold; given
// away, lost or destroyed, it's beer lost.

#include "domain/batch.h"
#include "domain/inventory.h"
#include "domain/packaging.h"
#include "domain/timestamp.h"

#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace ninkasi::domain {

// A lot as costing needs it.
struct CostedLot {
    std::int64_t id = 0;
    std::int64_t item_id = 0;
    Timestamp received_at{};
    double quantity = 0.0;       // as received
    std::optional<double> cost;  // of all of it
    std::string supplier;
};

// What one unit of a lot cost: its cost over the quantity received. nullopt
// if it has no cost (or no quantity).
std::optional<double> unit_cost(const CostedLot& lot);

// How sure a cost is, from surest to least.
enum class CostBasis {
    Lot,        // the lot it was drawn from
    Settled,    // drawn before its delivery was logged, at the lot that covered it
    Estimated,  // at the item's latest known cost
    Unknown,    // no cost known for the item at all
};
// The less sure of two.
CostBasis weaker(CostBasis a, CostBasis b);

// A draw on stock, and what it cost.
struct CostedDraw {
    std::int64_t movement_id = 0;
    std::int64_t item_id = 0;
    std::optional<std::int64_t> batch_id;
    StockReason reason = StockReason::Brewed;
    Timestamp at{};
    double quantity = 0.0;  // taken
    double cost = 0.0;      // of the part with a known or estimated cost
    CostBasis basis = CostBasis::Lot;
};

// Costs every draw on stock: each movement out but the settling of a
// shortfall, which only moves stock already drawn onto its lot. `movements`
// and `lots` are every item's, in any order.
std::vector<CostedDraw> cost_draws(std::span<const StockMovement> movements, std::span<const CostedLot> lots);

// The latest known unit cost of an item on `at`: of its lot with a cost
// received last by then, else of the first received after. nullopt if none
// of its lots has a cost.
std::optional<double> latest_unit_cost(std::span<const CostedLot> lots, std::int64_t item_id, Timestamp at);

// --- Batches ------------------------------------------------------------------------

// What costing needs to know of a batch.
struct BatchFacts {
    std::int64_t id = 0;
    std::string code;
    std::string beer;
    BatchStatus status = BatchStatus::Brewing;
    double brewed_l = 0.0;  // knocked out, over all its brews
    std::optional<Timestamp> finished_at;
};

struct BatchCost {
    std::int64_t batch_id = 0;
    double ingredients = 0.0;      // drawn for it when it was brewed
    double packaging = 0.0;        // drawn when it was packaged: cans
    int draws = 0;                 // none: brewed without stock being drawn, so no cost
    CostBasis basis = CostBasis::Lot;  // the least sure of its draws
    double brewed_l = 0.0;
    double packaged_l = 0.0;
    int cans_packaged = 0;
    double can_volume_l = 0.0;  // what its cans hold, if it was canned
    bool finished = false;      // all of it packaged or dumped

    // Its beer's cost per liter: over the liters packaged once it's
    // finished; before, over the liters brewed. nullopt with nothing drawn,
    // or nothing to divide by.
    std::optional<double> liquid_per_l() const;
    // One package of it: its beer, and for a can, its share of the cans
    // and packaging. nullopt where liquid_per_l() is.
    std::optional<double> package_cost(PackageType type, double unit_volume_l) const;
};

// The cost of every batch in `batches`, from the draws for them and their
// packaged goods.
std::map<std::int64_t, BatchCost> batch_costs(std::span<const CostedDraw> draws,
                                              std::span<const PackagedMovement> packaged,
                                              std::span<const BatchFacts> batches);

// --- Beer leaving packaged goods ---------------------------------------------------------

// Whether beer taken out for `reason` was sold (its cost is the cost of
// goods sold): sold, poured in the taproom, exported, or supplies for ships
// and aircraft. Beer returned is the reverse of a sale.
bool counts_as_sold(PackagedReason reason);
// Whether it left without being sold: drunk at the brewery, samples, unfit,
// destroyed, lost, short at a count (an overage the reverse), or sent to
// another brewery of the same owners.
bool counts_as_lost(PackagedReason reason);

// What a packaged goods movement carried at cost: positive for beer going
// out, negative for beer coming in. nullopt if its batch has no cost.
std::optional<double> movement_cost(const PackagedMovement& movement, const std::map<std::int64_t, BatchCost>& costs);

// --- What the stock is worth ----------------------------------------------------------

// What's left of a lot.
struct LotStock {
    std::int64_t lot_id = 0;
    double remaining = 0.0;
};

struct InventoryValue {
    double stock = 0.0;          // ingredients and packaging on hand
    double packaged_beer = 0.0;  // at cost
    double in_progress = 0.0;    // beer not yet packaged: its ingredients' cost
    int estimated = 0;           // lots valued at the item's latest cost, theirs unknown
    int uncosted = 0;            // lots and packaged beer with no cost known, left out

    double total() const { return stock + packaged_beer + in_progress; }
};

InventoryValue inventory_value(std::span<const CostedLot> lots, std::span<const LotStock> remaining,
                               std::span<const PackagedStock> packaged,
                               const std::map<std::int64_t, BatchCost>& costs);

} // namespace ninkasi::domain
