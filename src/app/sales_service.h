#pragma once
// Selling beer: customers, the price list, the brewery's currency and keg
// deposits, the sales themselves, and the kegs customers have.
//
// Anyone can add and edit customers and record sales, since whoever
// delivers the beer may need to. Sales are priced from the price list. The
// price list, the currency, the deposits and changing what a sale was
// priced at are the brewery's money, so only admins do them (others get
// NotAllowed).
//
// A sale's packaged beer leaves packaged goods (see PackagedGoodsService)
// oldest batch first, each removal linked to the line it was sold on, so
// what it cost can be traced to its batches. Kegs sold stay out at the
// customer until they bring them back (return_kegs).

#include "app/packaged_goods_service.h"
#include "app/session.h"
#include "data/database.h"
#include "data/sales_repository.h"
#include "data/settings_repository.h"
#include "domain/money.h"
#include "domain/packaging.h"
#include "domain/sales.h"

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace ninkasi::app {

class SalesService {
public:
    SalesService(data::Database& db, Session session, data::SettingsRepository& settings,
                 PackagedGoodsService& goods);

    // Whether the signed-in person can change prices and the currency (admins can).
    bool can_set_prices() const { return session_.is_admin(); }
    // Grows with every change, so windows know when to read again.
    std::uint64_t revision() const { return revision_; }

    // --- The currency -------------------------------------------------------------
    const domain::Currency& currency() const { return currency_; }
    void set_currency(const domain::Currency& currency);

    // --- Keg deposits ---------------------------------------------------------------
    // Charged on each keg sold (when the sale asks for them), and refunded
    // when it comes back.
    const domain::KegDeposits& keg_deposits() const { return deposits_; }
    // Throws std::invalid_argument for a negative deposit.
    void set_keg_deposits(const domain::KegDeposits& deposits);

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

    // --- Sales ------------------------------------------------------------------------
    // Records a sale. Lines without a price are priced from the price list
    // where it has one; with `charge_deposits`, deposits are added for its
    // kegs (see keg_deposits) if it's to a customer, as deposits are
    // refunded to whoever brings the kegs back. Its packaged beer leaves packaged goods, as
    // sold (or, in the taproom, as sent to the taproom). Throws
    // std::invalid_argument, saving nothing, for a sale with no lines, a line
    // with nothing on it, or more beer than is on hand.
    std::int64_t record_sale(domain::Sale sale, bool charge_deposits);
    // Newest first.
    std::vector<domain::Sale> sales(int limit = 2000);
    // Prices a line, or clears its price (admins).
    void set_line_price(std::int64_t line_id, std::optional<double> price);
    // Prices every line still without a price from the price list in effect
    // on its sale's date (admins).
    struct Pricing {
        int priced = 0;    // lines given a price
        int unpriced = 0;  // lines the price list has no price for
    };
    Pricing price_from_list();
    // Deletes a sale recorded by mistake (admins): its beer goes back into
    // packaged goods. Throws std::invalid_argument for a sale that can't be
    // deleted (see domain::can_delete).
    void delete_sale(std::int64_t sale_id);

    // --- Kegs at customers ------------------------------------------------------------
    domain::KegAccount keg_account(std::int64_t customer_id, domain::PackageType type) const;
    // Kegs a customer brought back: they go to the keg cleaner dirty, and the
    // deposits paid on them are refunded. Returns what was refunded. Throws
    // std::invalid_argument, saving nothing, if no kegs are given.
    double return_kegs(std::int64_t customer_id, std::span<const domain::KegCount> kegs, domain::Timestamp at,
                       std::string_view note);

private:
    void reload();
    static void check(const domain::PriceEntry& price);
    static void check(const domain::Sale& sale);
    // Writes a sale and its lines, taking its packaged beer out. Call inside a transaction.
    std::int64_t write_sale(const domain::Sale& sale);

    data::Database& db_;
    Session session_;
    data::SettingsRepository& settings_;
    PackagedGoodsService& goods_;
    data::SalesRepository repository_;
    domain::Currency currency_;
    domain::KegDeposits deposits_;
    std::vector<domain::Customer> customers_;
    std::vector<domain::PriceEntry> prices_;
    std::map<std::int64_t, std::array<domain::KegAccount, domain::kKegTypes.size()>> keg_accounts_;
    std::uint64_t revision_ = 0;
};

} // namespace ninkasi::app
