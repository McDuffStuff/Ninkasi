#include "app/formula_service.h"

#include "data/audit_log.h"

#include <utility>

namespace ninkasi::app {

namespace {

constexpr std::string_view kEntity = "formula";

std::string_view trim(std::string_view text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) return {};
    const auto last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

std::int64_t entity_id(calc::Formula formula) {
    return static_cast<std::int64_t>(formula);
}

} // namespace

FormulaService::FormulaService(data::Database& db, Session session)
    : db_(db), session_(std::move(session)), repository_(db) {
    load();
}

void FormulaService::load() {
    calc::clear_adjusted_formulas();
    for (std::optional<FormulaAdjustment>& adjustment : adjustments_) adjustment.reset();
    problems_.clear();
    for (data::StoredFormula& stored : repository_.load_all()) {
        const std::optional<calc::Formula> formula = calc::formula_from_key(stored.key);
        if (!formula) continue;  // from a newer version of Ninkasi
        try {
            calc::set_adjusted_formula(*formula, calc::compile_formula(*formula, stored.latex));
            adjustments_[static_cast<std::size_t>(*formula)] =
                FormulaAdjustment{std::move(stored.latex), std::move(stored.by), stored.at};
        } catch (const calc::FormulaError& e) {
            problems_.push_back(std::string(calc::info(*formula).name) + ": " + e.what() +
                                " The original formula is being used.");
        }
    }
}

std::optional<FormulaAdjustment> FormulaService::adjustment(calc::Formula formula) const {
    return adjustments_[static_cast<std::size_t>(formula)];
}

bool FormulaService::any_adjusted() const {
    for (const std::optional<FormulaAdjustment>& adjustment : adjustments_) {
        if (adjustment) return true;
    }
    return false;
}

void FormulaService::adjust(calc::Formula formula, std::string_view latex) {
    require_admin(session_, "adjust formulas");
    const calc::FormulaInfo& info = calc::info(formula);
    const std::string_view text = trim(latex);
    if (text == trim(info.latex)) {
        reset(formula);
        return;
    }
    calc::Expression compiled = calc::compile_formula(formula, text);  // throws before anything is saved

    data::Transaction tx(db_);
    repository_.save(info.key, text, session_.user_id, domain::now());
    data::record_audit(db_, session_.user_id, "formula.adjust", kEntity, entity_id(formula),
                       std::string(info.name) + ": " + std::string(text));
    tx.commit();
    calc::set_adjusted_formula(formula, std::move(compiled));
    adjustments_[static_cast<std::size_t>(formula)] =
        FormulaAdjustment{std::string(text), session_.display_name, domain::now()};
}

void FormulaService::reset(calc::Formula formula) {
    require_admin(session_, "put formulas back");
    const calc::FormulaInfo& info = calc::info(formula);
    if (!adjustments_[static_cast<std::size_t>(formula)]) return;

    data::Transaction tx(db_);
    repository_.remove(info.key);
    data::record_audit(db_, session_.user_id, "formula.reset", kEntity, entity_id(formula),
                       std::string(info.name) + ": back to the original");
    tx.commit();
    calc::set_adjusted_formula(formula, std::nullopt);
    adjustments_[static_cast<std::size_t>(formula)].reset();
}

void FormulaService::reset_all() {
    require_admin(session_, "put formulas back");
    data::Transaction tx(db_);
    repository_.remove_all();
    data::record_audit(db_, session_.user_id, "formula.reset_all", kEntity, 0, "every formula back to the original");
    tx.commit();
    load();
}

} // namespace ninkasi::app
