#include "app/export_service.h"

#include "app/csv.h"
#include "domain/activity.h"
#include "domain/timestamp.h"

#include <cctype>
#include <cstdio>
#include <stdexcept>
#include <utility>

namespace ninkasi::app {

namespace {

namespace fs = std::filesystem;
using calc::Measure;

// How a column's values are written.
enum class Format {
    Text,
    Key,       // a stored key, made readable: "transfer_in" becomes "Transfer in"
    Time,      // an ISO-8601 UTC time, written in local time: "2026-10-28 14:05"
    Number,    // with `decimals` places
    Quantity,  // a canonical value in the brewery's unit for `measure`, named in the header
    Activity,  // activity log text, its quantities in the brewery's units
};

struct Column {
    std::string_view header;
    Format format = Format::Text;
    Measure measure = Measure::Volume;  // Quantity columns
    int decimals = 2;                   // Number columns
};

struct Spec {
    ExportTable table;
    std::string_view sql;
    std::vector<Column> columns;
};

constexpr Column text(std::string_view header) { return {header, Format::Text}; }
constexpr Column key(std::string_view header) { return {header, Format::Key}; }
constexpr Column time(std::string_view header) { return {header, Format::Time}; }
constexpr Column number(std::string_view header, int decimals) { return {header, Format::Number, Measure::Volume, decimals}; }
constexpr Column quantity(std::string_view header, Measure measure) { return {header, Format::Quantity, measure}; }

// Inventory amounts are in the item's stock unit.
#define NINKASI_STOCK_UNIT "CASE i.unit WHEN 'mass' THEN 'kg' WHEN 'volume' THEN 'L' ELSE 'each' END"

const std::vector<Spec>& specs() {
    static const std::vector<Spec> all = {
        {{"batches", "Batches", "Every batch, with its status, dates and gravities"},
         "SELECT b.code, b.name, b.style, b.status, "
         "(SELECT MIN(knocked_out_at) FROM brews WHERE batch_id = b.id), b.finished_at, "
         "(SELECT SUM(volume_l) FROM brews WHERE batch_id = b.id AND knocked_out_at IS NOT NULL), "
         "(SELECT SUM(og * volume_l) / SUM(volume_l) FROM brews WHERE batch_id = b.id AND og IS NOT NULL "
         " AND knocked_out_at IS NOT NULL AND volume_l > 0), "
         "(SELECT gravity_sg FROM readings WHERE batch_id = b.id AND gravity_sg IS NOT NULL "
         " ORDER BY at DESC, id DESC LIMIT 1) "
         "FROM batches b ORDER BY b.created_at, b.id;",
         {text("Batch"), text("Beer"), text("Style"), key("Status"), time("Brewed"), time("Finished"),
          quantity("Volume brewed", Measure::Volume), quantity("OG", Measure::Gravity),
          quantity("Latest gravity", Measure::Gravity)}},
        {{"brews", "Brews", "Each brewhouse turn and its knock-out"},
         "SELECT b.code, b.name, COALESCE(h.name, ''), r.started_at, r.knocked_out_at, COALESCE(v.name, ''), "
         "r.volume_l, r.og, r.notes FROM brews r JOIN batches b ON b.id = r.batch_id "
         "LEFT JOIN elements h ON h.id = r.brewhouse_id LEFT JOIN elements v ON v.id = r.vessel_id "
         "ORDER BY r.started_at, r.id;",
         {text("Batch"), text("Beer"), text("Brewhouse"), time("Started"), time("Knocked out"), text("Into"),
          quantity("Volume", Measure::Volume), quantity("OG", Measure::Gravity), text("Notes")}},
        {{"ingredients", "Ingredients used", "What each brew used, as confirmed at knock-out"},
         "SELECT b.code, b.name, r.knocked_out_at, i.kind, i.name, i.detail, "
         "CASE i.amount_kind WHEN 'text' THEN '' ELSE printf('%.3f', i.planned) END, "
         "CASE i.amount_kind WHEN 'text' THEN i.text ELSE printf('%.3f', i.actual) END, "
         "CASE i.amount_kind WHEN 'mass' THEN 'kg' WHEN 'volume' THEN 'L' WHEN 'count' THEN 'each' ELSE '' END "
         "FROM brew_ingredients i JOIN brews r ON r.id = i.brew_id JOIN batches b ON b.id = r.batch_id "
         "ORDER BY r.started_at, r.id, i.position;",
         {text("Batch"), text("Beer"), time("Knocked out"), key("Kind"), text("Ingredient"), text("Addition"),
          text("Planned"), text("Used"), text("Unit")}},
        {{"readings", "Readings", "Gravity, temperature and pH readings"},
         "SELECT b.code, b.name, r.at, COALESCE(v.name, ''), r.method, r.gravity_sg, r.temperature_c, r.ph, "
         "r.note, u.display_name FROM readings r JOIN batches b ON b.id = r.batch_id "
         "LEFT JOIN elements v ON v.id = r.vessel_id JOIN users u ON u.id = r.user_id ORDER BY r.at, r.id;",
         {text("Batch"), text("Beer"), time("When"), text("Tank"), key("Method"), quantity("Gravity", Measure::Gravity),
          quantity("Temperature", Measure::Temperature), number("pH", 2), text("Note"), text("Who")}},
        {{"volume_ledger", "Volume ledger", "Every change in the beer in each tank (what TTB reports come from)"},
         "SELECT e.at, b.code, b.name, COALESCE(v.name, ''), e.volume_l, e.reason, u.display_name "
         "FROM volume_events e JOIN batches b ON b.id = e.batch_id LEFT JOIN elements v ON v.id = e.vessel_id "
         "JOIN users u ON u.id = e.user_id ORDER BY e.at, e.id;",
         {time("When"), text("Batch"), text("Beer"), text("Tank"), quantity("Change", Measure::Volume), key("Reason"),
          text("Who")}},
        {{"inventory", "Inventory", "Everything stocked, with what's on hand"},
         "SELECT i.name, i.category, "
         "COALESCE((SELECT SUM(quantity) FROM stock_movements WHERE item_id = i.id), 0), " NINKASI_STOCK_UNIT ", "
         "i.reorder_level, i.notes FROM inventory_items i WHERE i.archived = 0 ORDER BY i.name COLLATE NOCASE;",
         {text("Item"), key("Category"), number("On hand", 3), text("Unit"), number("Reorder below", 3),
          text("Notes")}},
        {{"stock_movements", "Stock in and out", "Every delivery, use, count and brew draw"},
         "SELECT m.at, i.name, m.quantity, " NINKASI_STOCK_UNIT ", m.reason, COALESCE(b.code, ''), "
         "COALESCE(l.lot_code, ''), COALESCE(l.supplier, ''), COALESCE(v.number, ''), m.note, u.display_name "
         "FROM stock_movements m JOIN inventory_items i ON i.id = m.item_id JOIN users u ON u.id = m.user_id "
         "LEFT JOIN inventory_lots l ON l.id = m.lot_id LEFT JOIN invoices v ON v.id = l.invoice_id "
         "LEFT JOIN batches b ON b.id = m.batch_id ORDER BY m.at, m.id;",
         {time("When"), text("Item"), number("Change", 3), text("Unit"), key("Reason"), text("Batch"), text("Lot"),
          text("Supplier"), text("Invoice"), text("Note"), text("Who")}},
        {{"invoices", "Invoices", "Each line of each invoice received"},
         "SELECT v.received_at, v.supplier, v.number, i.name, l.quantity, " NINKASI_STOCK_UNIT ", l.cost, l.lot_code, "
         "COALESCE(l.best_before, ''), v.note, u.display_name FROM inventory_lots l "
         "JOIN invoices v ON v.id = l.invoice_id JOIN inventory_items i ON i.id = l.item_id "
         "JOIN users u ON u.id = v.user_id ORDER BY v.received_at, v.id, l.id;",
         {time("Received"), text("Supplier"), text("Invoice"), text("Item"), number("Quantity", 3), text("Unit"),
          number("Cost", 2), text("Lot"), text("Best before"), text("Note"), text("Logged by")}},
        {{"packaged_goods", "Packaged goods", "Packaged beer on hand, by beer, batch and package"},
         "SELECT b.name, b.code, p.package, p.unit_volume_l, SUM(p.quantity), SUM(p.quantity) * p.unit_volume_l "
         "FROM packaged_movements p JOIN batches b ON b.id = p.batch_id "
         "GROUP BY p.batch_id, p.package, p.unit_volume_l HAVING SUM(p.quantity) <> 0 "
         "ORDER BY b.name COLLATE NOCASE, b.code, p.package;",
         {text("Beer"), text("Batch"), key("Package"), number("Each holds (L)", 3), number("Count", 0),
          quantity("Volume", Measure::Volume)}},
        {{"packaging_log", "Packaging log", "Beer packaged, sold, sent to the taproom, returned..."},
         "SELECT p.at, b.code, b.name, p.package, p.quantity, p.reason, COALESCE(e.name, ''), p.note, u.display_name "
         "FROM packaged_movements p JOIN batches b ON b.id = p.batch_id LEFT JOIN elements e ON e.id = p.vessel_id "
         "JOIN users u ON u.id = p.user_id ORDER BY p.at, p.id;",
         {time("When"), text("Batch"), text("Beer"), key("Package"), number("Change", 0), key("Reason"),
          text("From tank"), text("Note"), text("Who")}},
        {{"keg_log", "Keg log", "Kegs added, cleaned, filled and removed"},
         "SELECT k.at, k.keg_type, k.state, k.quantity, k.reason, COALESCE(b.code, ''), k.note, u.display_name "
         "FROM keg_movements k JOIN users u ON u.id = k.user_id LEFT JOIN batches b ON b.id = k.batch_id "
         "ORDER BY k.at, k.id;",
         {time("When"), key("Keg"), key("State"), number("Change", 0), key("Reason"), text("Batch"), text("Note"),
          text("Who")}},
        {{"customers", "Customers", "Everyone beer is sold to"},
         "SELECT name, channel, contact, notes, CASE active WHEN 1 THEN 'Yes' ELSE 'No' END FROM customers "
         "ORDER BY active DESC, name COLLATE NOCASE;",
         {text("Customer"), key("Channel"), text("Contact"), text("Notes"), text("Still buying")}},
        {{"sales", "Sales", "Each line of each sale: what was sold, to whom, for how much", true},
         "SELECT s.at, s.id, s.channel, COALESCE(c.name, ''), s.number, s.source, l.kind, l.beer, l.package, "
         "l.unit_volume_l, l.quantity, l.pack_size, l.pour_l, l.unit_price, l.quantity * l.unit_price, "
         "l.description, s.note, u.display_name FROM sale_lines l JOIN sales s ON s.id = l.sale_id "
         "LEFT JOIN customers c ON c.id = s.customer_id JOIN users u ON u.id = s.user_id ORDER BY s.at, s.id, l.id;",
         {time("When"), number("Sale", 0), key("Channel"), text("Customer"), text("Number"), key("Recorded by"),
          key("Line"), text("Beer"), key("Package"), number("Each holds (L)", 3), number("Quantity", 2),
          number("In a pack", 0), number("Pour (L)", 3), number("Price each", 2), number("Amount", 2),
          text("Description"), text("Note"), text("Who")}},
        {{"price_list", "Price list", "Every price, earlier ones included", true},
         "SELECT CASE beer WHEN '' THEN 'Every beer' ELSE beer END, channel, priced_as, package, unit_volume_l, "
         "pack_size, yield, price, effective_from FROM prices "
         "ORDER BY channel, beer COLLATE NOCASE, effective_from, id;",
         {text("Beer"), key("Channel"), key("Sold as"), key("Package"), number("Each holds (L)", 3),
          number("In a pack", 0), number("Yield", 2), number("Price (pours: per liter)", 4), text("From")}},
        {{"expenses", "Expenses", "What the brewery spent, by day and category", true},
         "SELECT e.day, e.payee, c.name, e.amount, e.number, e.note, u.display_name FROM expenses e "
         "JOIN expense_categories c ON c.id = e.category_id JOIN users u ON u.id = e.user_id ORDER BY e.day, e.id;",
         {text("Day"), text("Paid to"), text("Category"), number("Amount", 2), text("Invoice"), text("Note"),
          text("Who")}},
        {{"recurring_expenses", "Monthly expenses", "Expenses that recur every month: rent, wages...", true},
         "SELECT r.description, c.name, r.monthly, r.first_month, COALESCE(r.last_month, '') "
         "FROM recurring_expenses r JOIN expense_categories c ON c.id = r.category_id ORDER BY r.first_month, r.id;",
         {text("What"), text("Category"), number("Each month", 2), text("From"), text("Until")}},
        {{"tasks", "Tasks", "Open and done tasks"},
         "SELECT t.title, COALESCE(e.name, ''), COALESCE(b.code, ''), COALESCE(t.due_date, ''), t.created_at, "
         "t.done_at, t.notes FROM tasks t LEFT JOIN elements e ON e.id = t.element_id "
         "LEFT JOIN batches b ON b.id = t.batch_id "
         "ORDER BY t.done_at IS NOT NULL, t.due_date IS NULL, t.due_date, t.created_at;",
         {text("Task"), text("Equipment"), text("Batch"), text("Due"), time("Created"), time("Done"), text("Notes")}},
        {{"activity", "Activity log", "Everything that happened, as the equipment and batch logs show it"},
         "SELECT a.at, a.kind, COALESCE(b.code, ''), COALESCE(e.name, ''), a.text, u.display_name "
         "FROM activity_log a JOIN users u ON u.id = a.user_id LEFT JOIN batches b ON b.id = a.batch_id "
         "LEFT JOIN elements e ON e.id = a.element_id ORDER BY a.at, a.id;",
         {time("When"), key("What"), text("Batch"), text("Equipment"), {"Details", Format::Activity}, text("Who")}},
    };
    return all;
}

#undef NINKASI_STOCK_UNIT

const Spec& spec(std::string_view key) {
    for (const Spec& candidate : specs()) {
        if (candidate.table.key == key) return candidate;
    }
    throw std::invalid_argument("There's no table called " + std::string(key) + " to export.");
}

// "transfer_in" -> "Transfer in".
std::string readable(std::string text) {
    for (char& c : text) {
        if (c == '_') c = ' ';
    }
    if (!text.empty()) text[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(text[0])));
    return text;
}

std::string local_time(const std::string& iso) {
    const std::optional<domain::Timestamp> at = domain::parse_iso8601(iso);
    if (!at) return iso;
    const domain::LocalTime local = domain::to_local(*at);
    char text[32];
    std::snprintf(text, sizeof text, "%04d-%02d-%02d %02d:%02d", local.year, local.month, local.day, local.hour,
                  local.minute);
    return text;
}

std::string header(const Column& column, const calc::UnitPreferences& units) {
    if (column.format != Format::Quantity) return std::string(column.header);
    return std::string(column.header) + " (" + std::string(calc::symbol(units.unit_for(column.measure))) + ")";
}

std::string cell(const data::Statement& stmt, int col, const Column& column, const calc::UnitPreferences& units) {
    if (stmt.column_is_null(col)) return {};
    switch (column.format) {
    case Format::Text:     return stmt.column_text(col);
    case Format::Key:      return readable(stmt.column_text(col));
    case Format::Time:     return local_time(stmt.column_text(col));
    case Format::Activity: return domain::render_quantities(stmt.column_text(col), units);
    case Format::Number: {
        char text[64];
        std::snprintf(text, sizeof text, "%.*f", column.decimals, stmt.column_double(col));
        return text;
    }
    case Format::Quantity:
        return calc::format(stmt.column_double(col), units.unit_for(column.measure), false);
    }
    return stmt.column_text(col);
}

} // namespace

ExportService::ExportService(data::Database& db, const calc::UnitPreferences& units, Session session)
    : db_(db), units_(units), session_(std::move(session)) {}

std::vector<ExportTable> ExportService::tables() const {
    std::vector<ExportTable> list;
    for (const Spec& entry : specs()) {
        if (!entry.table.money || session_.is_admin()) list.push_back(entry.table);
    }
    return list;
}

std::string ExportService::csv(std::string_view key) {
    const Spec& table = spec(key);
    if (table.table.money) require_admin(session_, "export the brewery's money");
    std::vector<std::string> fields;
    for (const Column& column : table.columns) fields.push_back(header(column, units_));
    std::string text = csv_row(fields);

    auto stmt = db_.prepare(table.sql);
    while (stmt.step()) {
        fields.clear();
        for (std::size_t col = 0; col < table.columns.size(); ++col) {
            fields.push_back(cell(stmt, static_cast<int>(col), table.columns[col], units_));
        }
        text += csv_row(fields);
    }
    return text;
}

void ExportService::save(std::string_view key, const fs::path& path) {
    write_csv_file(path, csv(key));
}

std::vector<fs::path> ExportService::save_all(const fs::path& folder) {
    std::error_code error;
    fs::create_directories(folder, error);
    std::vector<fs::path> files;
    for (const ExportTable& table : tables()) {
        const fs::path path = folder / (std::string(table.name) + ".csv");
        save(table.key, path);
        files.push_back(path);
    }
    return files;
}

} // namespace ninkasi::app
