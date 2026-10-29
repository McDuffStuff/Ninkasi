#pragma once
// Reads and writes customers, the price list and sales. SalesService
// decides what may be written; this only stores it.

#include "data/database.h"
#include "domain/sales.h"

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <string_view>
#include <vector>

namespace ninkasi::data {

class SalesRepository {
public:
    explicit SalesRepository(Database& db) : db_(db) {}

    // --- Customers --------------------------------------------------------------
    // Every customer: active ones first, then by name.
    std::vector<domain::Customer> customers();
    std::int64_t insert(const domain::Customer& customer);
    void update(const domain::Customer& customer);

    // --- The price list -----------------------------------------------------------
    // Every entry, earlier ones included: by beer (every beer first), channel and date.
    std::vector<domain::PriceEntry> prices();
    std::int64_t insert(const domain::PriceEntry& price, std::int64_t user_id);
    void update(const domain::PriceEntry& price);
    void remove_price(std::int64_t id);

    // --- Sales ----------------------------------------------------------------------
    // The sale alone, then each line: insert() doesn't write its lines.
    std::int64_t insert(const domain::Sale& sale, std::int64_t user_id);
    std::int64_t insert_line(std::int64_t sale_id, const domain::SaleLine& line);
    // Newest first, with their lines.
    std::vector<domain::Sale> sales(int limit);
    // Every sale with a line that has no price, oldest first.
    std::vector<domain::Sale> sales_needing_prices();
    std::optional<domain::Sale> sale(std::int64_t id);
    void set_unit_price(std::int64_t line_id, std::optional<double> price);
    // Deletes a sale, its lines, and the packaged goods taken out for it.
    void delete_sale(std::int64_t id);

    // Each customer's kegs, by size (in the order of domain::kKegTypes).
    std::map<std::int64_t, std::array<domain::KegAccount, domain::kKegTypes.size()>> keg_accounts();

private:
    // The sales whose ids `id_query` selects (with `parameter` bound to its
    // one parameter, if it has one), in the order given by `order`, with their lines.
    std::vector<domain::Sale> read_sales(std::string_view id_query, std::string_view order,
                                         std::optional<std::int64_t> parameter);

    Database& db_;
};

} // namespace ninkasi::data
