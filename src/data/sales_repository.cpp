#include "data/sales_repository.h"

#include <cmath>
#include <string>

namespace ninkasi::data {

namespace {

std::size_t keg_index(domain::PackageType type) {
    return type == domain::PackageType::Sixtel ? 1 : 0;  // by position in kKegTypes
}

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

// --- Sales ------------------------------------------------------------------------

std::int64_t SalesRepository::insert(const domain::Sale& sale, std::int64_t user_id) {
    auto stmt = db_.prepare(
        "INSERT INTO sales (at, channel, customer_id, number, source, import_id, note, user_id) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?);");
    stmt.bind(1, domain::to_iso8601(sale.at)).bind(2, domain::key(sale.channel));
    stmt.bind_optional(3, sale.customer_id);
    stmt.bind(4, sale.number).bind(5, domain::key(sale.source));
    stmt.bind_optional(6, sale.import_id);
    stmt.bind(7, sale.note).bind(8, user_id);
    stmt.execute();
    return db_.last_insert_id();
}

std::int64_t SalesRepository::insert_line(std::int64_t sale_id, const domain::SaleLine& line) {
    auto stmt = db_.prepare(
        "INSERT INTO sale_lines (sale_id, kind, beer, package, unit_volume_l, quantity, pack_size, pour_l, "
        "unit_price, description) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?);");
    const bool has_package = line.kind == domain::LineKind::Package || line.kind == domain::LineKind::Deposit;
    stmt.bind(1, sale_id)
        .bind(2, domain::key(line.kind))
        .bind(3, line.beer)
        .bind(4, has_package ? domain::key(line.package) : std::string_view(""))
        .bind(5, line.unit_volume_l)
        .bind(6, line.quantity)
        .bind(7, line.pack_size)
        .bind(8, line.pour_l);
    stmt.bind_optional(9, line.unit_price);
    stmt.bind(10, line.description);
    stmt.execute();
    return db_.last_insert_id();
}

std::vector<domain::Sale> SalesRepository::read_sales(std::string_view id_query, std::string_view order,
                                                      std::optional<std::int64_t> parameter) {
    std::vector<domain::Sale> sales;
    std::map<std::int64_t, std::size_t> index;  // sale id -> position in `sales`
    {
        auto stmt = db_.prepare(
            "SELECT s.id, s.at, s.channel, s.customer_id, s.number, s.source, s.import_id, s.note, "
            "COALESCE(c.name, ''), u.display_name FROM sales s JOIN users u ON u.id = s.user_id "
            "LEFT JOIN customers c ON c.id = s.customer_id WHERE s.id IN (" +
            std::string(id_query) + ") ORDER BY " + std::string(order) + ";");
        if (parameter) stmt.bind(1, *parameter);
        while (stmt.step()) {
            domain::Sale sale;
            sale.id = stmt.column_int64(0);
            sale.at = domain::parse_iso8601(stmt.column_text(1)).value_or(domain::Timestamp{});
            sale.channel = domain::channel_from_key(stmt.column_text(2)).value_or(domain::Channel::Other);
            sale.customer_id = stmt.column_optional_int64(3);
            sale.number = stmt.column_text(4);
            sale.source = domain::sale_source_from_key(stmt.column_text(5)).value_or(domain::SaleSource::Entered);
            sale.import_id = stmt.column_optional_int64(6);
            sale.note = stmt.column_text(7);
            sale.customer = stmt.column_text(8);
            sale.user = stmt.column_text(9);
            index[sale.id] = sales.size();
            sales.push_back(std::move(sale));
        }
    }
    auto stmt = db_.prepare(
        "SELECT sale_id, id, kind, beer, package, unit_volume_l, quantity, pack_size, pour_l, unit_price, "
        "description FROM sale_lines WHERE sale_id IN (" +
        std::string(id_query) + ") ORDER BY sale_id, id;");
    if (parameter) stmt.bind(1, *parameter);
    while (stmt.step()) {
        const auto found = index.find(stmt.column_int64(0));
        if (found == index.end()) continue;
        domain::SaleLine line;
        line.id = stmt.column_int64(1);
        line.kind = domain::line_kind_from_key(stmt.column_text(2)).value_or(domain::LineKind::Other);
        line.beer = stmt.column_text(3);
        line.package = domain::package_type_from_key(stmt.column_text(4)).value_or(domain::PackageType::HalfBarrel);
        line.unit_volume_l = stmt.column_double(5);
        line.quantity = stmt.column_double(6);
        line.pack_size = static_cast<int>(stmt.column_int64(7));
        line.pour_l = stmt.column_double(8);
        line.unit_price = stmt.column_optional_double(9);
        line.description = stmt.column_text(10);
        sales[found->second].lines.push_back(std::move(line));
    }
    return sales;
}

std::vector<domain::Sale> SalesRepository::sales(int limit) {
    return read_sales("SELECT id FROM sales ORDER BY at DESC, id DESC LIMIT ?", "s.at DESC, s.id DESC",
                      static_cast<std::int64_t>(limit));
}

std::vector<domain::Sale> SalesRepository::sales_needing_prices() {
    return read_sales("SELECT sale_id FROM sale_lines WHERE unit_price IS NULL", "s.at, s.id", std::nullopt);
}

std::vector<domain::Sale> SalesRepository::sales_from(domain::SaleSource source) {
    return read_sales("SELECT id FROM sales WHERE source = '" + std::string(domain::key(source)) + "'", "s.at, s.id",
                      std::nullopt);
}

std::optional<domain::Sale> SalesRepository::sale(std::int64_t id) {
    std::vector<domain::Sale> found = read_sales("SELECT ?", "s.id", id);
    if (found.empty()) return std::nullopt;
    return std::move(found.front());
}

void SalesRepository::set_unit_price(std::int64_t line_id, std::optional<double> price) {
    auto stmt = db_.prepare("UPDATE sale_lines SET unit_price = ? WHERE id = ?;");
    stmt.bind_optional(1, price);
    stmt.bind(2, line_id);
    stmt.execute();
}

void SalesRepository::delete_sale(std::int64_t id) {
    // The packaged goods taken out for it first, as they refer to its lines.
    auto movements = db_.prepare(
        "DELETE FROM packaged_movements WHERE sale_line_id IN (SELECT id FROM sale_lines WHERE sale_id = ?);");
    movements.bind(1, id);
    movements.execute();
    auto lines = db_.prepare("DELETE FROM sale_lines WHERE sale_id = ?;");
    lines.bind(1, id);
    lines.execute();
    auto sale = db_.prepare("DELETE FROM sales WHERE id = ?;");
    sale.bind(1, id);
    sale.execute();
}

std::map<std::int64_t, std::array<domain::KegAccount, domain::kKegTypes.size()>> SalesRepository::keg_accounts() {
    std::map<std::int64_t, std::array<domain::KegAccount, domain::kKegTypes.size()>> accounts;
    const auto account = [&](const Statement& stmt) -> domain::KegAccount& {
        const domain::PackageType type =
            domain::package_type_from_key(stmt.column_text(1)).value_or(domain::PackageType::HalfBarrel);
        return accounts[stmt.column_int64(0)][keg_index(type)];
    };

    // Kegs sold to each customer...
    auto sold = db_.prepare(
        "SELECT s.customer_id, l.package, SUM(CAST(ROUND(l.quantity) AS INTEGER) * l.pack_size) "
        "FROM sale_lines l JOIN sales s ON s.id = l.sale_id "
        "WHERE l.kind = 'package' AND l.package IN ('half_barrel', 'sixtel') AND s.customer_id IS NOT NULL "
        "GROUP BY s.customer_id, l.package;");
    while (sold.step()) account(sold).sold = static_cast<int>(sold.column_int64(2));

    // ...the deposits they hold...
    auto deposits = db_.prepare(
        "SELECT s.customer_id, l.package, SUM(l.quantity), SUM(l.quantity * COALESCE(l.unit_price, 0)) "
        "FROM sale_lines l JOIN sales s ON s.id = l.sale_id "
        "WHERE l.kind = 'deposit' AND s.customer_id IS NOT NULL GROUP BY s.customer_id, l.package;");
    while (deposits.step()) {
        domain::KegAccount& entry = account(deposits);
        entry.deposit_kegs = static_cast<int>(std::lround(deposits.column_double(2)));
        entry.deposits = deposits.column_double(3);
    }

    // ...and the kegs they brought back.
    auto returned = db_.prepare(
        "SELECT customer_id, keg_type, SUM(quantity) FROM keg_movements "
        "WHERE customer_id IS NOT NULL AND reason = ? GROUP BY customer_id, keg_type;");
    returned.bind(1, domain::key(domain::KegReason::Added));
    while (returned.step()) account(returned).returned = static_cast<int>(returned.column_int64(2));
    return accounts;
}

// --- Point-of-sale imports ---------------------------------------------------------

std::vector<domain::PosItem> SalesRepository::pos_items() {
    std::vector<domain::PosItem> items;
    auto stmt = db_.prepare(
        "SELECT item, kind, beer, pour_l, package, unit_volume_l, pack_size FROM pos_items "
        "ORDER BY item COLLATE NOCASE;");
    while (stmt.step()) {
        domain::PosItem item;
        item.item = stmt.column_text(0);
        item.kind = domain::pos_item_kind_from_key(stmt.column_text(1)).value_or(domain::PosItemKind::Other);
        item.beer = stmt.column_text(2);
        item.pour_l = stmt.column_double(3);
        item.package = domain::package_type_from_key(stmt.column_text(4)).value_or(domain::PackageType::Can);
        item.unit_volume_l = stmt.column_double(5);
        item.pack_size = static_cast<int>(stmt.column_int64(6));
        items.push_back(std::move(item));
    }
    return items;
}

void SalesRepository::save(const domain::PosItem& item) {
    auto stmt = db_.prepare(
        "INSERT OR REPLACE INTO pos_items (item, kind, beer, pour_l, package, unit_volume_l, pack_size) "
        "VALUES (?, ?, ?, ?, ?, ?, ?);");
    stmt.bind(1, item.item)
        .bind(2, domain::key(item.kind))
        .bind(3, item.beer)
        .bind(4, item.pour_l)
        .bind(5, domain::key(item.package))
        .bind(6, item.unit_volume_l)
        .bind(7, item.pack_size);
    stmt.execute();
}

std::int64_t SalesRepository::insert_pos_import(std::string_view file_name, domain::Date first_day,
                                                domain::Date last_day, int rows, std::int64_t user_id) {
    auto stmt = db_.prepare(
        "INSERT INTO pos_imports (file_name, first_day, last_day, rows, user_id) VALUES (?, ?, ?, ?, ?);");
    stmt.bind(1, file_name)
        .bind(2, domain::to_iso8601(first_day))
        .bind(3, domain::to_iso8601(last_day))
        .bind(4, rows)
        .bind(5, user_id);
    stmt.execute();
    return db_.last_insert_id();
}

} // namespace ninkasi::data
