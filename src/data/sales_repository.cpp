#include "data/sales_repository.h"

namespace ninkasi::data {

namespace {

domain::PriceEntry read_price(const Statement& stmt) {
    domain::PriceEntry price;
    price.id = stmt.column_int64(0);
    price.beer = stmt.column_text(1);
    price.channel = domain::channel_from_key(stmt.column_text(2)).value_or(domain::Channel::Other);
    price.priced_as = domain::priced_as_from_key(stmt.column_text(3)).value_or(domain::PricedAs::Package);
    price.package = domain::package_type_from_key(stmt.column_text(4)).value_or(domain::PackageType::HalfBarrel);
    price.unit_volume_l = stmt.column_double(5);
    price.pack_size = static_cast<int>(stmt.column_int64(6));
    price.pour_l = stmt.column_double(7);
    price.yield = stmt.column_double(8);
    price.price = stmt.column_double(9);
    price.effective_from = domain::parse_date(stmt.column_text(10)).value_or(domain::Date{});
    return price;
}

// Binds a price's fields to parameters 1 to 10.
void bind_price(Statement& stmt, const domain::PriceEntry& price) {
    stmt.bind(1, price.beer)
        .bind(2, domain::key(price.channel))
        .bind(3, domain::key(price.priced_as))
        // A pour has no package: an empty key, as a default-constructed view would bind NULL.
        .bind(4, price.priced_as == domain::PricedAs::Package ? domain::key(price.package) : std::string_view(""))
        .bind(5, price.unit_volume_l)
        .bind(6, price.pack_size)
        .bind(7, price.pour_l)
        .bind(8, price.yield)
        .bind(9, price.price)
        .bind(10, domain::to_iso8601(price.effective_from));
}

} // namespace

std::vector<domain::Customer> SalesRepository::customers() {
    std::vector<domain::Customer> customers;
    auto stmt = db_.prepare(
        "SELECT id, name, channel, contact, notes, active FROM customers "
        "ORDER BY active DESC, name COLLATE NOCASE, id;");
    while (stmt.step()) {
        domain::Customer customer;
        customer.id = stmt.column_int64(0);
        customer.name = stmt.column_text(1);
        customer.channel = domain::channel_from_key(stmt.column_text(2)).value_or(domain::Channel::Other);
        customer.contact = stmt.column_text(3);
        customer.notes = stmt.column_text(4);
        customer.active = stmt.column_int64(5) != 0;
        customers.push_back(std::move(customer));
    }
    return customers;
}

std::int64_t SalesRepository::insert(const domain::Customer& customer) {
    auto stmt = db_.prepare("INSERT INTO customers (name, channel, contact, notes, active) VALUES (?, ?, ?, ?, ?);");
    stmt.bind(1, customer.name)
        .bind(2, domain::key(customer.channel))
        .bind(3, customer.contact)
        .bind(4, customer.notes)
        .bind(5, customer.active ? 1 : 0);
    stmt.execute();
    return db_.last_insert_id();
}

void SalesRepository::update(const domain::Customer& customer) {
    auto stmt = db_.prepare("UPDATE customers SET name = ?, channel = ?, contact = ?, notes = ?, active = ? WHERE id = ?;");
    stmt.bind(1, customer.name)
        .bind(2, domain::key(customer.channel))
        .bind(3, customer.contact)
        .bind(4, customer.notes)
        .bind(5, customer.active ? 1 : 0)
        .bind(6, customer.id);
    stmt.execute();
}

std::vector<domain::PriceEntry> SalesRepository::prices() {
    std::vector<domain::PriceEntry> prices;
    auto stmt = db_.prepare(
        "SELECT id, beer, channel, priced_as, package, unit_volume_l, pack_size, pour_l, yield, price, "
        "effective_from FROM prices ORDER BY beer COLLATE NOCASE, channel, priced_as, effective_from, id;");
    while (stmt.step()) prices.push_back(read_price(stmt));
    return prices;
}

std::int64_t SalesRepository::insert(const domain::PriceEntry& price, std::int64_t user_id) {
    auto stmt = db_.prepare(
        "INSERT INTO prices (beer, channel, priced_as, package, unit_volume_l, pack_size, pour_l, yield, price, "
        "effective_from, user_id) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);");
    bind_price(stmt, price);
    stmt.bind(11, user_id);
    stmt.execute();
    return db_.last_insert_id();
}

void SalesRepository::update(const domain::PriceEntry& price) {
    auto stmt = db_.prepare(
        "UPDATE prices SET beer = ?, channel = ?, priced_as = ?, package = ?, unit_volume_l = ?, pack_size = ?, "
        "pour_l = ?, yield = ?, price = ?, effective_from = ? WHERE id = ?;");
    bind_price(stmt, price);
    stmt.bind(11, price.id);
    stmt.execute();
}

void SalesRepository::remove_price(std::int64_t id) {
    auto stmt = db_.prepare("DELETE FROM prices WHERE id = ?;");
    stmt.bind(1, id);
    stmt.execute();
}

} // namespace ninkasi::data
