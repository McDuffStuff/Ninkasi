#pragma once
// Reads and writes customers and the price list.

#include "data/database.h"
#include "domain/sales.h"

#include <cstdint>
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

private:
    Database& db_;
};

} // namespace ninkasi::data
