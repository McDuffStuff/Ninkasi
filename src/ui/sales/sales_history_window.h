#pragma once
// Sales > Sales history: every sale, newest first. Clicking one shows its
// lines below.
//
// Admins also see what each sale came to, flagged where a line has no
// price yet, and can price those from the price list, change a line's price
// or delete a sale entered by mistake (which puts its beer back).

#include "app/sales_service.h"
#include "calc/units.h"
#include "domain/sales.h"
#include "ui/sales/sale_dialog.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ninkasi::ui {

class SalesHistoryWindow {
public:
    SalesHistoryWindow(app::SalesService& sales, SaleDialog& sale_dialog, const calc::UnitPreferences& units,
                       float ui_scale);

    void show();
    void draw();

private:
    void reload();
    void draw_toolbar(bool admin);
    void draw_list(bool admin);
    void draw_details(domain::Sale& sale, bool admin);
    // Asks before deleting the selected sale.
    void draw_delete_confirmation();
    bool shown(const domain::Sale& sale) const;

    app::SalesService& sales_;
    SaleDialog& sale_dialog_;
    const calc::UnitPreferences& units_;
    float ui_scale_;

    bool visible_ = false;
    bool focus_ = false;
    std::string filter_;
    bool unpriced_only_ = false;
    std::string message_;  // what pricing from the price list did
    std::string error_;
    std::vector<domain::Sale> sales_list_;  // working copies: prices are edited in them
    std::optional<std::int64_t> selected_;
    bool confirm_delete_ = false;
    std::uint64_t revision_ = ~0ull;
};

} // namespace ninkasi::ui
