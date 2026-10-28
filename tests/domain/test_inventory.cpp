#include "domain/inventory.h"
#include "test_helpers.h"

#include <vector>

using namespace ninkasi::domain;

TEST_CASE("stock is drawn from the oldest lot that has some left") {
    const std::vector<LotBalance> lots = {{1, 0.0}, {2, 40.0}, {3, 100.0}};
    CHECK(plan_draw(lots, 30.0) == std::vector<Draw>{{2, 30.0}});
    CHECK(plan_draw(lots, 70.0) == std::vector<Draw>{{2, 40.0}, {3, 30.0}});
    CHECK(plan_draw(lots, 140.0) == std::vector<Draw>{{2, 40.0}, {3, 100.0}});
}

TEST_CASE("a draw bigger than the lots takes the rest from unlotted stock") {
    const std::vector<LotBalance> lots = {{1, 25.0}, {2, -5.0}};
    CHECK(plan_draw(lots, 40.0) == std::vector<Draw>{{1, 25.0}, {std::nullopt, 15.0}});
    CHECK(plan_draw({}, 10.0) == std::vector<Draw>{{std::nullopt, 10.0}});
    CHECK(plan_draw(lots, 0.0).empty());
    CHECK(plan_draw(lots, -3.0).empty());
}

TEST_CASE("brew ingredients are matched to the stock they're drawn from") {
    std::vector<InventoryItem> items(4);
    items[0] = {.id = 1, .name = "Pale Malt", .unit = StockUnit::Mass};
    items[1] = {.id = 2, .name = "2-row (Briess)", .unit = StockUnit::Mass, .library_id = 7};
    items[2] = {.id = 3, .name = "London Ale", .category = ItemCategory::Yeast, .unit = StockUnit::Count};
    items[3] = {.id = 4, .name = "Old malt", .unit = StockUnit::Mass, .library_id = 9, .archived = true};

    IngredientUse use{.name = "pale malt", .amount_kind = AmountKind::Mass};
    REQUIRE(find_stock_for(use, items));
    CHECK(find_stock_for(use, items)->id == 1);  // by name, ignoring case

    use.library_id = 7;
    CHECK(find_stock_for(use, items)->id == 2);  // the library link wins

    use.library_id = 9;
    CHECK(find_stock_for(use, items)->id == 1);  // archived items aren't drawn from

    const IngredientUse yeast_by_mass{.name = "London Ale", .amount_kind = AmountKind::Mass};
    CHECK_FALSE(find_stock_for(yeast_by_mass, items));  // stocked in packs, not by weight
    const IngredientUse text{.name = "Pale Malt", .amount_kind = AmountKind::Text};
    CHECK_FALSE(find_stock_for(text, items));
}

TEST_CASE("inventory keys round-trip") {
    for (ItemCategory category : kItemCategories) CHECK(item_category_from_key(key(category)) == category);
    for (StockUnit unit : kStockUnits) CHECK(stock_unit_from_key(key(unit)) == unit);
    for (StockReason reason : {StockReason::Received, StockReason::Brewed, StockReason::Counted, StockReason::Used,
                               StockReason::Discarded, StockReason::Settled}) {
        CHECK(stock_reason_from_key(key(reason)) == reason);
    }
}
