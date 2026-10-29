#pragma once
// Selling beer: customers, the price list and the brewery's currency (and,
// from the next steps on, the sales themselves).
//
// Anyone can add and edit customers, since whoever records a sale may need
// a new one. The price list and the currency are the brewery's money, so
// only admins change them (others get NotAllowed); everyone can read prices,
// which fill in sales.

#include "app/session.h"
#include "data/database.h"
#include "data/sales_repository.h"
#include "data/settings_repository.h"
#include "domain/money.h"
#include "domain/sales.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace ninkasi::app {

class SalesService {
public:
    SalesService(data::Database& db, Session session, data::SettingsRepository& settings);

    // Whether the signed-in person can change prices and the currency (admins can).
    bool can_set_prices() const { return session_.is_admin(); }
    // Grows with every change, so windows know when to read again.
    std::uint64_t revision() const { return revision_; }

    // --- The currency -------------------------------------------------------------
    const domain::Currency& currency() const { return currency_; }
    void set_currency(const domain::Currency& currency);

    // --- Customers ----------------------------------------------------------------
    // Active customers first, then by name.
    const std::vector<domain::Customer>& customers() const { return customers_; }
    const domain::Customer* find_customer(std::int64_t id) const;
    // Each throws std::invalid_argument if the name is blank (or the id unknown).
    std::int64_t add_customer(const domain::Customer& customer);
    void update_customer(const domain::Customer& customer);

    // --- The price list -------------------------------------------------------------
    // Every entry, earlier prices included.
    const std::vector<domain::PriceEntry>& prices() const { return prices_; }
    // The price for `query` (see domain::find_price), if there is one.
    std::optional<domain::PriceEntry> price_for(const domain::PriceQuery& query) const;
    // Each throws std::invalid_argument for a negative price, a pack of fewer
    // than one, a pour without a size or a yield outside 0–100 %.
    std::int64_t add_price(const domain::PriceEntry& price);
    void update_price(const domain::PriceEntry& price);
    void remove_price(std::int64_t id);

private:
    void reload();
    static void check(const domain::PriceEntry& price);

    data::Database& db_;
    Session session_;
    data::SettingsRepository& settings_;
    data::SalesRepository repository_;
    domain::Currency currency_;
    std::vector<domain::Customer> customers_;
    std::vector<domain::PriceEntry> prices_;
    std::uint64_t revision_ = 0;
};

} // namespace ninkasi::app
