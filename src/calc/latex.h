#pragma once
// A small LaTeX math reader for the subset Ninkasi's formulas are written in.
//
// It turns LaTeX source into a tree of math nodes. Two things read that tree:
// the formula evaluator (calc/expression.h), and the typesetter that draws
// formulas on screen (ui/math_view.h). Because both read the same tree, a
// formula always shows exactly what it computes.
//
// What it reads:
//   letters and digits; + - * / = < > ( ) [ ] , and |
//   ^ and _, followed by a single character, a command or a {group}
//   \frac{a}{b} (also \dfrac, \tfrac), \sqrt{x}, \sqrt[n]{x}
//   \left( ... \right) with ( ) [ ] | . \{ \}
//   \text{...}, \mathrm{...}, \operatorname{...}
//   \times \cdot \pm \div \le \leq \ge \geq \ne \neq \approx \infty \circ \% \partial
//   Greek letters \alpha ... \omega and \Gamma ... \Omega
//   \ln \log \exp \sin \cos \tan \sinh \cosh \tanh \min \max, \sum and \prod
//   spacing: \, \: \; \! \quad \qquad "\ " and ~
//   \begin{cases} a & \text{if } x < 1 \\ b & \text{otherwise} \end{cases}
// UTF-8 Greek letters and symbols (α, ×, ≤, °...) can be typed directly.

#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace ninkasi::calc {

// Something wrong with a formula: LaTeX that can't be read, or a formula that
// can't be computed. The message says what, for the person who wrote it.
class FormulaError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct MathNode;
using MathList = std::vector<MathNode>;

struct MathNode {
    enum class Kind {
        Letter,       // one letter, Latin or Greek: `text` in UTF-8
        Number,       // digits with an optional decimal point: "0.000125"
        Text,         // \text, \mathrm, \operatorname: upright words
        Operator,     // + − × · / ÷ ± = < > ≤ ≥ ≠ ≈ and the comma; `text` as shown
        Function,     // \ln, \exp, \min...: `text` is its name ("ln")
        Open,         // ( or [ written on its own
        Close,        // ) or ]
        Symbol,       // anything else shown as written: ∞, °, %, |, !
        Fraction,     // args[0] over args[1]
        Root,         // the square root of args[0], or the args[1]-th root
        Group,        // args[0] in {braces}, or between \left`open` and \right`close`
        Cases,        // one row per pair: args[2i] its value, args[2i + 1] its condition
        Space,        // `width` ems of space (negative for \!)
        BigOperator,  // \sum or \prod: `text` is the symbol
    };

    Kind kind = Kind::Symbol;
    std::string text;
    std::vector<MathList> args;
    MathList sup;  // superscript; empty if none
    MathList sub;  // subscript; empty if none
    std::string open, close;  // a Group's delimiters: "" for braces, or "(", "[", "|", "{", "."
    double width = 0.0;       // a Space's width in ems
};

// Reads `latex`. Throws FormulaError saying what's wrong if it can't.
MathList parse_latex(std::string_view latex);

} // namespace ninkasi::calc
