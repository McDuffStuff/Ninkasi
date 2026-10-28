#include "data/formula_repository.h"

namespace ninkasi::data {

std::vector<StoredFormula> FormulaRepository::load_all() {
    std::vector<StoredFormula> formulas;
    auto stmt = db_.prepare(
        "SELECT f.formula, f.latex, u.display_name, f.changed_at "
        "FROM formula_adjustments f JOIN users u ON u.id = f.user_id ORDER BY f.formula;");
    while (stmt.step()) {
        formulas.push_back({stmt.column_text(0), stmt.column_text(1), stmt.column_text(2),
                            domain::parse_iso8601(stmt.column_text(3)).value_or(domain::Timestamp{})});
    }
    return formulas;
}

void FormulaRepository::save(std::string_view key, std::string_view latex, std::int64_t user_id,
                             domain::Timestamp at) {
    auto stmt = db_.prepare(
        "INSERT INTO formula_adjustments (formula, latex, user_id, changed_at) VALUES (?, ?, ?, ?) "
        "ON CONFLICT(formula) DO UPDATE SET latex = excluded.latex, user_id = excluded.user_id, "
        "changed_at = excluded.changed_at;");
    stmt.bind(1, key).bind(2, latex).bind(3, user_id).bind(4, domain::to_iso8601(at));
    stmt.execute();
}

void FormulaRepository::remove(std::string_view key) {
    auto stmt = db_.prepare("DELETE FROM formula_adjustments WHERE formula = ?;");
    stmt.bind(1, key);
    stmt.execute();
}

void FormulaRepository::remove_all() {
    db_.exec("DELETE FROM formula_adjustments;");
}

} // namespace ninkasi::data
