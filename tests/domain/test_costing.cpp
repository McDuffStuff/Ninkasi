#include "domain/costing.h"
#include "test_helpers.h"

#include <chrono>
#include <vector>

using namespace ninkasi;
using domain::CostBasis;
using domain::PackageType;
using domain::StockReason;

namespace {

domain::Timestamp day(int n) {
    return *domain::parse_iso8601("2026-09-01T09:00:00Z") + std::chrono::hours(24 * n);
}

domain::StockMovement movement(std::int64_t id, int on, std::int64_t item, std::optional<std::int64_t> lot,
                               double quantity, StockReason reason, std::optional<std::int64_t> batch = {}) {
    domain::StockMovement m;
    m.id = id;
    m.at = day(on);
    m.item_id = item;
    m.lot_id = lot;
    m.quantity = quantity;
    m.reason = reason;
    m.batch_id = batch;
    return m;
}

const domain::CostedDraw* draw_of(const std::vector<domain::CostedDraw>& draws, std::int64_t movement_id) {
    for (const domain::CostedDraw& draw : draws) {
        if (draw.movement_id == movement_id) return &draw;
    }
    return nullptr;
}

// Malt (item 1): lot 10 of 100 kg for $100 on day 1, lot 11 of 100 kg for
// $150 on day 10. Batch 1 uses 60 kg on day 2; batch 2 uses 60 on day 5,
// 40 from lot 10 and 20 before lot 11 was logged, which settles it; batch 3
// uses 30 kg from lot 11 on day 12.
// Hops (item 2): lot 20, entered without a cost; lot 21 at $20 a kg on day 8.
// Yeast (item 3): no cost ever entered.
const std::vector<domain::CostedLot> kLots{
    {10, 1, day(1), 100.0, 100.0},
    {11, 1, day(10), 100.0, 150.0},
    {20, 2, day(1), 5.0, std::nullopt},
    {21, 2, day(8), 5.0, 100.0},
    {30, 3, day(1), 10.0, std::nullopt},
};
const std::vector<domain::StockMovement> kMovements{
    movement(1, 1, 1, 10, 100.0, StockReason::Received),
    movement(2, 2, 1, 10, -60.0, StockReason::Brewed, 1),
    movement(3, 5, 1, 10, -40.0, StockReason::Brewed, 2),
    movement(4, 5, 1, std::nullopt, -20.0, StockReason::Brewed, 2),
    movement(5, 10, 1, 11, 100.0, StockReason::Received),
    movement(6, 10, 1, 11, -20.0, StockReason::Settled),
    movement(7, 10, 1, std::nullopt, 20.0, StockReason::Settled),
    movement(8, 12, 1, 11, -30.0, StockReason::Brewed, 3),
    movement(9, 1, 2, 20, 5.0, StockReason::Received),
    movement(10, 2, 2, 20, -1.0, StockReason::Brewed, 1),
    movement(11, 8, 2, 21, 5.0, StockReason::Received),
    movement(12, 1, 3, 30, 10.0, StockReason::Received),
    movement(13, 5, 3, 30, -1.0, StockReason::Brewed, 2),
    // More malt used than is left, never covered: costed at its latest, lot 11's.
    movement(14, 20, 1, 11, -50.0, StockReason::Brewed, 3),
    movement(15, 20, 1, std::nullopt, -10.0, StockReason::Brewed, 3),
};

} // namespace

TEST_CASE("each draw is costed from its lot, the delivery that covered it, or the latest cost") {
    const std::vector<domain::CostedDraw> draws = domain::cost_draws(kMovements, kLots);
    CHECK(draws.size() == 8);  // the settling pair draws nothing
    CHECK_FALSE(draw_of(draws, 6));
    CHECK_FALSE(draw_of(draws, 7));

    CHECK(draw_of(draws, 2)->cost == doctest::Approx(60.0));
    CHECK(draw_of(draws, 2)->basis == CostBasis::Lot);
    CHECK(draw_of(draws, 4)->cost == doctest::Approx(30.0));  // 20 kg at lot 11's $1.50
    CHECK(draw_of(draws, 4)->basis == CostBasis::Settled);
    CHECK(draw_of(draws, 10)->cost == doctest::Approx(20.0));  // lot 20 had no cost: lot 21's
    CHECK(draw_of(draws, 10)->basis == CostBasis::Estimated);
    CHECK(draw_of(draws, 13)->cost == 0.0);
    CHECK(draw_of(draws, 13)->basis == CostBasis::Unknown);
    CHECK(draw_of(draws, 15)->cost == doctest::Approx(15.0));
    CHECK(draw_of(draws, 15)->basis == CostBasis::Estimated);

    CHECK(domain::latest_unit_cost(kLots, 1, day(5)) == doctest::Approx(1.0));
    CHECK(domain::latest_unit_cost(kLots, 1, day(11)) == doctest::Approx(1.5));
    CHECK(domain::latest_unit_cost(kLots, 2, day(2)) == doctest::Approx(20.0));  // the first known after
    CHECK_FALSE(domain::latest_unit_cost(kLots, 3, day(2)));
}

TEST_CASE("a batch's beer costs its ingredients over the liters packaged, and a can adds the can") {
    const std::vector<domain::CostedDraw> draws = domain::cost_draws(kMovements, kLots);
    std::vector<domain::CostedDraw> with_cans = draws;
    domain::CostedDraw cans;
    cans.item_id = 4;
    cans.batch_id = 3;
    cans.reason = StockReason::Packaged;
    cans.quantity = 100.0;
    cans.cost = 12.0;
    with_cans.push_back(cans);

    const std::vector<domain::BatchFacts> batches{
        {1, "1", "Red Ale", domain::BatchStatus::Packaged, 600.0, day(20)},
        {2, "2", "Red Ale", domain::BatchStatus::Dumped, 600.0, day(15)},
        {3, "3", "Pils", domain::BatchStatus::Conditioning, 400.0, std::nullopt},
        {4, "4", "Stout", domain::BatchStatus::Packaged, 100.0, day(30)},
    };
    std::vector<domain::PackagedMovement> packaged(3);
    packaged[0].batch_id = 1;
    packaged[0].type = PackageType::HalfBarrel;
    packaged[0].unit_volume_l = 50.0;
    packaged[0].quantity = 10;  // 500 L of the 600 brewed
    packaged[1].batch_id = 3;
    packaged[1].type = PackageType::Can;
    packaged[1].unit_volume_l = 0.5;
    packaged[1].quantity = 100;
    packaged[2].batch_id = 4;
    packaged[2].type = PackageType::Sixtel;
    packaged[2].unit_volume_l = 20.0;
    packaged[2].quantity = 5;
    for (domain::PackagedMovement& movement : packaged) movement.reason = domain::PackagedReason::Packaged;

    const std::map<std::int64_t, domain::BatchCost> costs = domain::batch_costs(with_cans, packaged, batches);
    // Batch 1: $60 of malt and $20 of hops over the 500 L packaged.
    const domain::BatchCost& first = costs.at(1);
    CHECK(first.ingredients == doctest::Approx(80.0));
    CHECK(first.basis == CostBasis::Estimated);
    CHECK(*first.liquid_per_l() == doctest::Approx(80.0 / 500.0));
    CHECK(*first.package_cost(PackageType::HalfBarrel, 50.0) == doctest::Approx(8.0));

    // Batch 3, still conditioning: its malt over the 400 L brewed; a can
    // adds its share of the cans' $12.
    const domain::BatchCost& third = costs.at(3);
    CHECK(third.ingredients == doctest::Approx(45.0 + 50.0 * 1.5 + 15.0));
    CHECK(third.packaging == doctest::Approx(12.0));
    CHECK(*third.package_cost(PackageType::Can, 0.5) == doctest::Approx(135.0 / 400.0 * 0.5 + 0.12));

    // Batch 4 drew nothing from stock: it has no cost.
    CHECK_FALSE(costs.at(4).package_cost(PackageType::Sixtel, 20.0));

    // Two kegs of batch 1 sold, one returned.
    domain::PackagedMovement sold = packaged[0];
    sold.reason = domain::PackagedReason::Sold;
    sold.quantity = -2;
    CHECK(*domain::movement_cost(sold, costs) == doctest::Approx(16.0));
    CHECK(domain::counts_as_sold(domain::PackagedReason::Taproom));
    CHECK(domain::counts_as_lost(domain::PackagedReason::Destroyed));
    CHECK_FALSE(domain::counts_as_sold(domain::PackagedReason::Destroyed));

    // On hand: hops in lots 20 and 21, yeast with no cost, kegs and sixtels.
    const std::vector<domain::LotStock> remaining{{20, 4.0}, {21, 5.0}, {30, 9.0}};
    std::vector<domain::PackagedStock> on_hand(2);
    on_hand[0].batch_id = 1;
    on_hand[0].type = PackageType::HalfBarrel;
    on_hand[0].unit_volume_l = 50.0;
    on_hand[0].count = 8;
    on_hand[1].batch_id = 4;
    on_hand[1].type = PackageType::Sixtel;
    on_hand[1].unit_volume_l = 20.0;
    on_hand[1].count = 5;
    const domain::InventoryValue value = domain::inventory_value(kLots, remaining, on_hand, costs);
    CHECK(value.stock == doctest::Approx(4.0 * 20.0 + 5.0 * 20.0));  // lot 20 at lot 21's cost
    CHECK(value.estimated == 1);
    CHECK(value.uncosted == 2);  // the yeast, and batch 4's sixtels
    CHECK(value.packaged_beer == doctest::Approx(64.0));
    CHECK(value.in_progress == doctest::Approx(135.0 - 50.0 * 135.0 / 400.0));  // batch 3's beer not yet in cans
}
