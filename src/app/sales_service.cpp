#include "app/sales_service.h"

#include "data/audit_log.h"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <utility>

namespace ninkasi::app {

namespace {

constexpr const char* kSymbolKey = "money.symbol";
constexpr const char* kSymbolAfterKey = "money.symbol_after";
constexpr const char* kDecimalCommaKey = "money.decimal_comma";
constexpr const char* kDecimalsKey = "money.decimals";

std::string_view trim(std::string_view text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) return {};
    const auto last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

} // namespace

SalesService::SalesService(data::Database& db, Session session, data::SettingsRepository& settings)
    : db_(db), session_(std::move(session)), settings_(settings), repository_(db) {
    currency_.symbol = settings_.get_or(kSymbolKey, "$");
    currency_.symbol_after = settings_.get_or(kSymbolAfterKey, "0") == "1";
    currency_.decimal_comma = settings_.get_or(kDecimalCommaKey, "0") == "1";
    try {
        currency_.decimals = std::clamp(std::stoi(settings_.get_or(kDecimalsKey, "2")), 0, 4);
    } catch (const std::exception&) {
        currency_.decimals = 2;
    }
    reload();
}

void SalesService::reload() {
    customers_ = repository_.customers();
    prices_ = repository_.prices();
    ++revision_;
}

void SalesService::set_currency(const domain::Currency& currency) {
    require_admin(session_, "change the currency");
    currency_ = currency;
    currency_.decimals = std::clamp(currency_.decimals, 0, 4);
    settings_.set(kSymbolKey, currency_.symbol);
    settings_.set(kSymbolAfterKey, currency_.symbol_after ? "1" : "0");
    settings_.set(kDecimalCommaKey, currency_.decimal_comma ? "1" : "0");
    settings_.set(kDecimalsKey, std::to_string(currency_.decimals));
    ++revision_;
}

// --- Customers ---------------------------------------------------------------------

const domain::Customer* SalesService::find_customer(std::int64_t id) const {
    const auto found = std::find_if(customers_.begin(), customers_.end(),
                                    [id](const domain::Customer& customer) { return customer.id == id; });
    return found == customers_.end() ? nullptr : &*found;
}

std::int64_t SalesService::add_customer(const domain::Customer& customer) {
    domain::Customer added = customer;
    added.name = std::string(trim(customer.name));
    if (added.name.empty()) throw std::invalid_argument("A customer needs a name.");
    data::Transaction tx(db_);
    const std::int64_t id = repository_.insert(added);
    data::record_audit(db_, session_.user_id, "customer.create", "customer", id, added.name);
    tx.commit();
    reload();
    return id;
}

void SalesService::update_customer(const domain::Customer& customer) {
    domain::Customer updated = customer;
    updated.name = std::string(trim(customer.name));
    if (updated.name.empty()) throw std::invalid_argument("A customer needs a name.");
    const domain::Customer* existing = find_customer(customer.id);
    if (!existing) throw std::invalid_argument("There's no such customer.");
    if (*existing == updated) return;
    data::Transaction tx(db_);
    repository_.update(updated);
    data::record_audit(db_, session_.user_id, "customer.update", "customer", updated.id, updated.name);
    tx.commit();
    reload();
}

// --- The price list --------------------------------------------------------------------

std::optional<domain::PriceEntry> SalesService::price_for(const domain::PriceQuery& query) const {
    if (const domain::PriceEntry* found = domain::find_price(prices_, query)) return *found;
    return std::nullopt;
}

void SalesService::check(const domain::PriceEntry& price) {
    if (!(price.price >= 0.0)) throw std::invalid_argument("A price can't be negative.");
    if (price.priced_as == domain::PricedAs::Package && price.pack_size < 1) {
        throw std::invalid_argument("A pack holds at least one package.");
    }
    if (price.priced_as == domain::PricedAs::Pour) {
        if (!(price.pour_l > 0.0)) throw std::invalid_argument("A pour needs a size.");
        if (!(price.yield > 0.0 && price.yield <= 1.0)) {
            throw std::invalid_argument("A keg's yield is between 0 and 100 %.");
        }
    }
}

std::int64_t SalesService::add_price(const domain::PriceEntry& price) {
    require_admin(session_, "change the price list");
    check(price);
    domain::PriceEntry added = price;
    added.beer = std::string(trim(price.beer));
    data::Transaction tx(db_);
    const std::int64_t id = repository_.insert(added, session_.user_id);
    data::record_audit(db_, session_.user_id, "price.create", "price", id,
                       (added.beer.empty() ? std::string("every beer") : added.beer) + ": " +
                           std::to_string(added.price));
    tx.commit();
    reload();
    return id;
}

void SalesService::update_price(const domain::PriceEntry& price) {
    require_admin(session_, "change the price list");
    check(price);
    domain::PriceEntry updated = price;
    updated.beer = std::string(trim(price.beer));
    data::Transaction tx(db_);
    repository_.update(updated);
    data::record_audit(db_, session_.user_id, "price.update", "price", updated.id,
                       (updated.beer.empty() ? std::string("every beer") : updated.beer) + ": " +
                           std::to_string(updated.price));
    tx.commit();
    reload();
}

void SalesService::remove_price(std::int64_t id) {
    require_admin(session_, "change the price list");
    data::Transaction tx(db_);
    repository_.remove_price(id);
    data::record_audit(db_, session_.user_id, "price.delete", "price", id, "");
    tx.commit();
    reload();
}

} // namespace ninkasi::app
