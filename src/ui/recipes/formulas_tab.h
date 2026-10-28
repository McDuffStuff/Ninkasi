#pragma once
// The Formulas tab in Brewing settings: every formula Ninkasi calculates
// with, typeset from LaTeX, with what goes into it and where it comes from.
// Anyone can browse them and try them out; admins can adjust a formula, which
// then replaces the original everywhere, and put one or all of them back.

#include "app/formula_service.h"
#include "calc/expression.h"
#include "calc/formulas.h"

#include <optional>
#include <string>
#include <vector>

namespace ninkasi::ui {

class FormulasTab {
public:
    FormulasTab(app::FormulaService& formulas, float ui_scale);

    void draw();

private:
    void draw_list();
    void draw_details();
    void draw_editor(const calc::FormulaInfo& formula);
    void draw_try_it(const calc::FormulaInfo& formula);
    void draw_reset_all_confirmation();
    void select(calc::Formula formula);
    // Compiles the draft, keeping the result or the problem with it.
    void check_draft();

    app::FormulaService& formulas_;
    float ui_scale_;

    calc::Formula selected_ = calc::Formula::SgToPlato;
    std::string search_;
    std::vector<double> inputs_;  // the "try it" values, one per input of the selected formula
    std::string status_;

    // Adjusting the selected formula.
    bool editing_ = false;
    std::string draft_;
    std::optional<calc::Expression> compiled_draft_;
    std::string draft_problem_;
    std::string save_error_;
    bool confirm_reset_all_ = false;
};

} // namespace ninkasi::ui
