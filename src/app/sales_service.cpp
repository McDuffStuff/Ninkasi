#include "app/sales_service.h"

#include "data/audit_log.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>

namespace ninkasi::app {

namespace {

constexpr const char* kSymbolKey = "money.symbol";
constexpr const char* kSymbolAfterKey = "money.symbol_after";
constexpr const char* kDecimalCommaKey = "money.decimal_comma";
constexpr const char* kDecimalsKey = "money.decimals";
constexpr const char* kHalfBarrelDepositKey = "sales.deposit.half_barrel";
constexpr const char* kSixtelDepositKey = "sales.deposit.sixtel";
constexpr const char* kTaproomRevenueKey = "sales.taproom_revenue";
constexpr const char* kPosTakeCansKey = "pos.take_cans";
constexpr const char* kPosColumnsKey = "pos.columns.";  // and the header's signature

// A setting holding an amount of money, 0 if it's missing or not a number.
double money_setting(data::SettingsRepository& settings, const char* key) {
    try {
        return std::stod(settings.get_or(key, "0"));
    } catch (const std::exception&) {
        return 0.0;
    }
}

[[noreturn]] void fail(const std::string& message) {
    throw std::invalid_argument(message);
}

// Beer sold in the taproom is removed for the taproom; anywhere else, sold.
domain::PackagedReason removal_reason(domain::Channel channel) {
    return channel == domain::Channel::Taproom ? domain::PackagedReason::Taproom : domain::PackagedReason::Sold;
}

std::string_view trim(std::string_view text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) return {};
    const auto last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

} // namespace

SalesService::SalesService(data::Database& db, Session session, data::SettingsRepository& settings,
                           PackagedGoodsService& goods)
    : db_(db), session_(std::move(session)), settings_(settings), goods_(goods), repository_(db) {
    currency_.symbol = settings_.get_or(kSymbolKey, "$");
    currency_.symbol_after = settings_.get_or(kSymbolAfterKey, "0") == "1";
    currency_.decimal_comma = settings_.get_or(kDecimalCommaKey, "0") == "1";
    try {
        currency_.decimals = std::clamp(std::stoi(settings_.get_or(kDecimalsKey, "2")), 0, 4);
    } catch (const std::exception&) {
        currency_.decimals = 2;
    }
    deposits_.half_barrel = money_setting(settings_, kHalfBarrelDepositKey);
    deposits_.sixtel = money_setting(settings_, kSixtelDepositKey);
    taproom_revenue_ = domain::taproom_revenue_from_key(settings_.get_or(kTaproomRevenueKey, ""))
                           .value_or(domain::TaproomRevenue::KegsUsedUp);
    reload();
}

void SalesService::reload() {
    customers_ = repository_.customers();
    prices_ = repository_.prices();
    keg_accounts_ = repository_.keg_accounts();
    pos_items_ = repository_.pos_items();
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

void SalesService::set_keg_deposits(const domain::KegDeposits& deposits) {
    require_admin(session_, "change keg deposits");
    if (!(deposits.half_barrel >= 0.0) || !(deposits.sixtel >= 0.0)) fail("A deposit can't be negative.");
    deposits_ = deposits;
    settings_.set(kHalfBarrelDepositKey, std::to_string(deposits_.half_barrel));
    settings_.set(kSixtelDepositKey, std::to_string(deposits_.sixtel));
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

// --- Sales ---------------------------------------------------------------------------

void SalesService::check(const domain::Sale& sale) {
    using domain::LineKind;
    if (sale.lines.empty()) fail("Add what was sold.");
    for (const domain::SaleLine& line : sale.lines) {
        if (line.unit_price && !(*line.unit_price >= 0.0)) fail("A price can't be negative.");
        switch (line.kind) {
        case LineKind::Package:
            if (trim(line.beer).empty()) fail("Choose the beer.");
            if (line.pack_size < 1) fail("A pack holds at least one package.");
            if (!(line.unit_volume_l > 0.0)) fail("Say what size the packages of " + line.beer + " are.");
            if (line.packages() < 1 || line.quantity != std::round(line.quantity)) {
                fail("Enter how many of " + line.beer + " were sold.");
            }
            break;
        case LineKind::Pour:
            if (trim(line.beer).empty()) fail("Choose the beer.");
            if (!(line.pour_l > 0.0)) fail("A pour needs a size.");
            if (!(line.quantity > 0.0)) fail("Enter how many pours of " + line.beer + " were sold.");
            break;
        case LineKind::Deposit:
            if (!domain::is_keg(line.package)) fail("Deposits are on kegs.");
            if (line.quantity == 0.0) fail("Enter how many kegs the deposit is on.");
            break;
        case LineKind::Other:
            if (trim(line.description).empty()) fail("Say what else was sold.");
            if (!(line.quantity > 0.0)) fail("Enter how many of " + line.description + " were sold.");
            break;
        }
    }
}

std::int64_t SalesService::write_sale(const domain::Sale& sale, bool take_packages) {
    // The note on the beer taken out says who it went to.
    std::string note = sale.customer_id && find_customer(*sale.customer_id)
                           ? find_customer(*sale.customer_id)->name
                           : std::string(domain::name(sale.channel));
    if (!sale.number.empty()) note += " · " + sale.number;
    if (!sale.note.empty()) note += " · " + sale.note;

    const std::int64_t id = repository_.insert(sale, session_.user_id);
    for (const domain::SaleLine& line : sale.lines) {
        const std::int64_t line_id = repository_.insert_line(id, line);
        if (line.kind != domain::LineKind::Package || !take_packages) continue;
        goods_.write_removal({.beer = line.beer,
                              .type = line.package,
                              .unit_volume_l = line.unit_volume_l,
                              .count = line.packages(),
                              .reason = removal_reason(sale.channel),
                              .at = sale.at,
                              .note = note,
                              .sale_line_id = line_id});
    }
    return id;
}

std::int64_t SalesService::record_sale(domain::Sale sale, bool charge_deposits) {
    for (domain::SaleLine& line : sale.lines) {
        line.beer = std::string(trim(line.beer));
        line.description = std::string(trim(line.description));
    }
    check(sale);
    if (sale.customer_id && !find_customer(*sale.customer_id)) fail("There's no such customer.");
    for (domain::SaleLine& line : sale.lines) {
        if (!line.unit_price) line.unit_price = domain::list_price(prices_, sale, line);
    }
    if (charge_deposits && sale.customer_id) {
        for (domain::SaleLine& deposit : domain::deposit_lines(sale.lines, deposits_)) {
            sale.lines.push_back(std::move(deposit));
        }
    }

    std::int64_t id = 0;
    try {
        data::Transaction tx(db_);
        id = write_sale(sale);
        data::record_audit(db_, session_.user_id, "sale.create", "sale", id,
                           std::string(domain::name(sale.channel)) + ": " + std::to_string(sale.revenue()));
        tx.commit();
    } catch (...) {
        goods_.refresh();  // what's on hand is as it was
        throw;
    }
    goods_.refresh();
    reload();
    return id;
}

std::vector<domain::Sale> SalesService::sales(int limit) {
    return repository_.sales(limit);
}

void SalesService::set_line_price(std::int64_t line_id, std::optional<double> price) {
    require_admin(session_, "change what a sale was priced at");
    if (price && !(*price >= 0.0)) fail("A price can't be negative.");
    data::Transaction tx(db_);
    repository_.set_unit_price(line_id, price);
    data::record_audit(db_, session_.user_id, "sale.price", "sale_line", line_id,
                       price ? std::to_string(*price) : std::string("no price"));
    tx.commit();
    reload();
}

SalesService::Pricing SalesService::price_from_list() {
    require_admin(session_, "price sales");
    Pricing pricing;
    data::Transaction tx(db_);
    for (const domain::Sale& sale : repository_.sales_needing_prices()) {
        for (const domain::SaleLine& line : sale.lines) {
            if (line.unit_price) continue;
            const std::optional<double> price = domain::list_price(prices_, sale, line);
            if (!price) {
                ++pricing.unpriced;
                continue;
            }
            repository_.set_unit_price(line.id, *price);
            ++pricing.priced;
        }
    }
    if (pricing.priced > 0) {
        data::record_audit(db_, session_.user_id, "sale.price_from_list", "sale_line", 0,
                           std::to_string(pricing.priced) + " lines");
    }
    tx.commit();
    reload();
    return pricing;
}

void SalesService::delete_sale(std::int64_t sale_id) {
    require_admin(session_, "delete sales");
    const std::optional<domain::Sale> sale = repository_.sale(sale_id);
    if (!sale) fail("There's no such sale.");
    if (!domain::can_delete(sale->source)) {
        fail("A sale from " + std::string(domain::name(sale->source)) + " can't be deleted here.");
    }
    data::Transaction tx(db_);
    repository_.delete_sale(sale_id);
    data::record_audit(db_, session_.user_id, "sale.delete", "sale", sale_id,
                       std::string(domain::name(sale->channel)) + ": " + std::to_string(sale->revenue()));
    tx.commit();
    goods_.refresh();
    reload();
}

// --- Taproom revenue --------------------------------------------------------------------

void SalesService::set_taproom_revenue(domain::TaproomRevenue revenue) {
    require_admin(session_, "choose where the taproom's revenue comes from");
    taproom_revenue_ = revenue;
    settings_.set(kTaproomRevenueKey, std::string(domain::key(revenue)));
    ++revision_;
}

void SalesService::take_to_taproom(const PackagedRemoval& removal) {
    PackagedRemoval taken = removal;
    taken.reason = domain::PackagedReason::Taproom;
    if (taproom_revenue_ != domain::TaproomRevenue::KegsUsedUp) {
        goods_.remove(taken);
        return;
    }

    domain::Sale sale;
    sale.at = removal.at;
    sale.channel = domain::Channel::Taproom;
    sale.source = domain::SaleSource::TaproomKeg;
    sale.note = removal.note;
    domain::SaleLine line;
    line.beer = removal.beer;
    line.package = removal.type;
    line.unit_volume_l = removal.unit_volume_l;
    line.quantity = removal.count;
    line.unit_price = domain::list_price(prices_, sale, line);
    sale.lines.push_back(std::move(line));
    try {
        data::Transaction tx(db_);
        const std::int64_t id = write_sale(sale);
        data::record_audit(db_, session_.user_id, "sale.taproom_keg", "sale", id,
                           removal.beer + ": " + std::to_string(sale.revenue()));
        tx.commit();
    } catch (...) {
        goods_.refresh();
        throw;
    }
    goods_.refresh();
    reload();
}

// --- Point-of-sale imports ----------------------------------------------------------------

void SalesService::save_pos_item(const domain::PosItem& item) {
    domain::PosItem saved = item;
    saved.item = std::string(trim(item.item));
    saved.beer = std::string(trim(item.beer));
    if (saved.item.empty()) fail("An item needs its name.");
    repository_.save(saved);
    reload();
}

std::optional<PosColumns> SalesService::pos_columns(std::string_view signature) {
    const std::string text = settings_.get_or(kPosColumnsKey + std::string(signature), "");
    if (text.empty()) return std::nullopt;
    return pos_columns_from_text(text);
}

void SalesService::remember_pos_columns(std::string_view signature, const PosColumns& columns) {
    settings_.set(kPosColumnsKey + std::string(signature), to_text(columns));
}

bool SalesService::pos_takes_cans() {
    return settings_.get_or(kPosTakeCansKey, "1") == "1";
}

int SalesService::pos_days_imported(std::span<const domain::PosRow> rows) {
    std::set<domain::Date> days;
    for (const domain::PosRow& row : rows) days.insert(row.day);
    std::set<domain::Date> imported;
    for (const domain::Sale& sale : repository_.sales_from(domain::SaleSource::Pos)) {
        const domain::Date day = domain::local_date(sale.at);
        if (days.count(day)) imported.insert(day);
    }
    return static_cast<int>(imported.size());
}

SalesService::PosImport SalesService::import_pos(std::string_view file_name, std::span<const domain::PosRow> rows,
                                                 bool take_cans, bool replace) {
    domain::PosSales made = domain::pos_sales(rows, pos_items_);
    if (!made.unmapped.empty()) {
        std::string names;
        for (const std::string& name : made.unmapped) names += (names.empty() ? "" : ", ") + name;
        fail("Say what these items are first: " + names + ".");
    }
    if (made.sales.empty()) fail("There are no sales in it to import.");
    for (const domain::Sale& sale : made.sales) check(sale);  // every beer and size said

    PosImport result;
    std::set<domain::Date> days;
    for (const domain::PosRow& row : rows) days.insert(row.day);
    result.days = static_cast<int>(days.size());
    result.netted_out = made.netted_out;

    // The days in the file already imported, to replace.
    std::vector<std::int64_t> replaced;
    std::set<domain::Date> replaced_days;
    for (const domain::Sale& sale : repository_.sales_from(domain::SaleSource::Pos)) {
        const domain::Date day = domain::local_date(sale.at);
        if (!days.count(day)) continue;
        replaced.push_back(sale.id);
        replaced_days.insert(day);
    }
    result.replaced_days = static_cast<int>(replaced_days.size());
    if (!replaced.empty() && !replace) {
        fail(std::to_string(replaced_days.size()) + (replaced_days.size() == 1 ? " day was" : " days were") +
             " imported before.");
    }

    try {
        data::Transaction tx(db_);
        for (std::int64_t id : replaced) repository_.delete_sale(id);
        const std::int64_t import_id = repository_.insert_pos_import(
            file_name, *days.begin(), *days.rbegin(), static_cast<int>(rows.size()), session_.user_id);
        for (domain::Sale& sale : made.sales) {
            sale.import_id = import_id;
            write_sale(sale, take_cans);
            result.revenue += sale.revenue();
            ++result.sales;
        }
        data::record_audit(db_, session_.user_id, "sale.pos_import", "pos_import", import_id,
                           std::string(file_name) + ": " + std::to_string(result.days) + " days");
        tx.commit();
    } catch (...) {
        goods_.refresh();
        throw;
    }
    settings_.set(kPosTakeCansKey, take_cans ? "1" : "0");
    goods_.refresh();
    reload();
    return result;
}

// --- Kegs at customers ----------------------------------------------------------------

domain::KegAccount SalesService::keg_account(std::int64_t customer_id, domain::PackageType type) const {
    const auto found = keg_accounts_.find(customer_id);
    if (found == keg_accounts_.end()) return {};
    return found->second[type == domain::PackageType::Sixtel ? 1 : 0];  // by position in kKegTypes
}

double SalesService::return_kegs(std::int64_t customer_id, std::span<const domain::KegCount> kegs,
                                 domain::Timestamp at, std::string_view note) {
    const domain::Customer* customer = find_customer(customer_id);
    if (!customer) fail("There's no such customer.");
    domain::Sale refund;
    refund.at = at;
    refund.channel = customer->channel;
    refund.customer_id = customer_id;
    refund.source = domain::SaleSource::KegReturn;
    for (const domain::KegCount& entry : kegs) {
        if (entry.count <= 0) continue;
        if (std::optional<domain::SaleLine> line = keg_account(customer_id, entry.type).refund(entry.type, entry.count)) {
            refund.lines.push_back(std::move(*line));
        }
    }

    const std::string text = note.empty() ? "Back from " + customer->name : std::string(note);
    data::Transaction tx(db_);
    goods_.write_kegs_added(kegs, at, text, customer_id);
    if (!refund.lines.empty()) {
        const std::int64_t id = write_sale(refund);
        data::record_audit(db_, session_.user_id, "sale.deposit_refund", "sale", id,
                           customer->name + ": " + std::to_string(refund.deposits()));
    }
    tx.commit();
    goods_.refresh();
    reload();
    return -refund.deposits();
}

} // namespace ninkasi::app
