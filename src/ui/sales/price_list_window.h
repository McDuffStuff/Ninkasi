#pragma once
// Sales > Price list: what each beer sells for through each channel, by the
// keg, by the can or pack, or by the pour; and how the brewery writes money.
//
// A price applies from its date, so a price change is a new price from a
// date ("New price"), which leaves earlier sales at the old one. Admins edit
// it; everyone else sees it.

#include "app/batch_service.h"
#include "app/inventory_service.h"
#include "app/recipe_service.h"
#include "app/sales_service.h"
#include "calc/units.h"
#include "domain/money.h"
#include "domain/sales.h"

#include <cstdint>
#include <string>
#include <vector>

namespace ninkasi::ui {

class PriceListWindow {
public:
    PriceListWindow(app::SalesService& sales, app::BatchService& batches, app::RecipeService& recipes,
                    const app::InventoryService& inventory, const calc::UnitPreferences& units, float ui_scale);

    void show();
    void draw();

private:
    void reload();
    void draw_currency();
    void draw_deposits();
    void draw_channel(domain::Channel channel);
    // Each returns true when the price changed.
    bool beer_combo(domain::PriceEntry& price);
    bool sold_as_combo(domain::PriceEntry& price);
    bool size_field(domain::PriceEntry& price);
    void save(const domain::PriceEntry& price);
    void add(const domain::PriceEntry& price);
    // A sensible first price for a channel: pours in the taproom, packs of
    // cans to go, and kegs otherwise.
    domain::PriceEntry starting_price(domain::Channel channel) const;

    app::SalesService& sales_;
    app::BatchService& batches_;
    app::RecipeService& recipes_;
    const app::InventoryService& inventory_;
    const calc::UnitPreferences& units_;
    float ui_scale_;

    bool visible_ = false;
    bool focus_ = false;
    bool show_earlier_ = false;
    std::string error_;
    std::vector<domain::PriceEntry> prices_;  // working copies, saved field by field
    domain::Currency currency_;               // being edited
    domain::KegDeposits deposits_;            // being edited
    std::vector<std::string> beers_;          // from recipes and batches, for choosing
    std::vector<double> can_sizes_;           // of the cans in inventory, liters
    std::uint64_t revision_ = ~0ull;
};

} // namespace ninkasi::ui
