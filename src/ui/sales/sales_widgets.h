#pragma once
// Pieces the sales windows and dialogs share: how a sale's lines are put in
// words, and the customer picker.

#include "app/sales_service.h"
#include "calc/units.h"
#include "domain/sales.h"

#include <cstdint>
#include <optional>
#include <string>

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

} // namespace ninkasi::ui
