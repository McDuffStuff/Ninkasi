#pragma once
// The brewery's adjusted formulas (see calc/formulas.h): saving an admin's
// adjustment, putting formulas back to their originals, and installing the
// adjustments so every calculation uses them.
//
// The originals are never changed or removed, so any formula, or all of
// them, can be put back.

#include "app/session.h"
#include "calc/formulas.h"
#include "data/database.h"
#include "data/formula_repository.h"
#include "domain/timestamp.h"

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ninkasi::app {

struct FormulaAdjustment {
    std::string latex;
    std::string by;  // who adjusted it
    domain::Timestamp at{};
};

class FormulaService {
public:
    // Loads the adjustments and installs them for every calculation. One that
    // can't be computed any more (after an update to Ninkasi, say) is left
    // out, and listed in problems().
    FormulaService(data::Database& db, Session session);

    // Whether the signed-in person can adjust formulas (admins can).
    bool can_adjust() const { return session_.is_admin(); }

    std::optional<FormulaAdjustment> adjustment(calc::Formula formula) const;
    bool any_adjusted() const;
    // Stored adjustments that couldn't be used, and why.
    const std::vector<std::string>& problems() const { return problems_; }

    // --- Admins only (NotAllowed otherwise) -------------------------------------------
    // Saves `latex` as the formula and uses it from now on. LaTeX the same as
    // the original puts the original back. Throws calc::FormulaError, saving
    // nothing, if it can't be computed or the formula is reference only.
    void adjust(calc::Formula formula, std::string_view latex);
    // Puts one formula, or all of them, back to the original.
    void reset(calc::Formula formula);
    void reset_all();

private:
    // Reads the adjustments and installs them.
    void load();

    data::Database& db_;
    Session session_;
    data::FormulaRepository repository_;
    std::array<std::optional<FormulaAdjustment>, calc::kFormulaCount> adjustments_;
    std::vector<std::string> problems_;
};

} // namespace ninkasi::app
