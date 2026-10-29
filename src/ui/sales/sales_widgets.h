#pragma once
// Pieces the sales windows and dialogs share: how a sale's lines are put in
// words, and the customer, beer and can pickers.

#include "app/batch_service.h"
#include "app/inventory_service.h"
#include "app/recipe_service.h"
#include "app/sales_service.h"
#include "calc/units.h"
#include "domain/sales.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ninkasi::ui {

// "6 half-barrels, Red Ale", "24 × 16.0 fl oz cans, Red Ale", "2 packs of
// 4 × 16.0 fl oz cans, Red Ale", "120 pours of 16.0 fl oz, Red Ale",
// "Deposit on 6 half-barrels", "3 × T-shirt".
std::string line_text(const domain::SaleLine& line, const calc::UnitPreferences& units);

// A sale's lines in one line of text: the first two, then how many more.
std::string sale_text(const domain::Sale& sale, const calc::UnitPreferences& units);

// A dropdown of the customers still buying from the brewery, with `none`
// ("No customer") first. While something is typed that isn't a customer's
// name, it ends with "Add "typed" as a customer", which adds one sold to
// through `channel` (and puts any error in `error`). Returns true when the
// choice changes.
bool customer_combo(const char* label, std::optional<std::int64_t>& customer_id, app::SalesService& sales,
                    const char* none, domain::Channel channel, std::string& error);

// The beers there are to sell: from recipes and batches, each name once, sorted.
std::vector<std::string> beer_names(app::RecipeService& recipes, app::BatchService& batches);
// The sizes of the cans in inventory, in liters.
std::vector<double> can_sizes(const app::InventoryService& inventory);
// A pint: 16 US fl oz where beer is measured in US units, half a liter elsewhere.
double default_pour_l(const calc::UnitPreferences& units);

// A dropdown of `beers`, starting with `every` ("Every beer", for no name)
// if given. While something is typed that isn't one of them, it offers
// "Use "typed"", for a beer not brewed yet. Returns true when the choice changes.
bool beer_combo(const char* label, std::string& beer, const std::vector<std::string>& beers, const char* every);
// A dropdown of can sizes shown in `unit`, starting with `any` ("Any can",
// for 0) if given. Returns true when the choice changes.
bool can_size_combo(const char* label, double& size_l, const std::vector<double>& sizes, calc::Unit unit,
                    const char* any);

} // namespace ninkasi::ui
