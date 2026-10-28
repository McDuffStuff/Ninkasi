#include "calc/expression.h"
#include "calc/latex.h"
#include "test_helpers.h"

#include <array>
#include <cmath>
#include <string>
#include <string_view>

using namespace ninkasi;
using calc::Expression;
using calc::FormulaError;
using calc::MathNode;

namespace {

// Compiles `latex` with `inputs` and evaluates it at `values`.
double eval(std::string_view latex, std::initializer_list<std::string_view> inputs = {},
            std::initializer_list<double> values = {}) {
    const std::vector<std::string_view> names(inputs);
    return Expression::compile(latex, names).evaluate(std::span<const double>(values.begin(), values.size()));
}

std::string error_of(std::string_view latex, std::initializer_list<std::string_view> inputs = {}) {
    const std::vector<std::string_view> names(inputs);
    try {
        Expression::compile(latex, names);
    } catch (const FormulaError& e) {
        return e.what();
    }
    return "";
}

} // namespace

TEST_CASE("LaTeX is read into fractions, scripts, groups and cases") {
    const calc::MathList list = calc::parse_latex(R"(\frac{a}{b} + x^{2}_{s} \left( y \right) \alpha)");
    REQUIRE(list.size() == 5);
    CHECK(list[0].kind == MathNode::Kind::Fraction);
    CHECK(list[0].args.size() == 2);
    CHECK(list[1].kind == MathNode::Kind::Operator);
    CHECK(list[2].text == "x");
    CHECK(list[2].sup.front().text == "2");
    CHECK(list[2].sub.front().text == "s");
    CHECK(list[3].kind == MathNode::Kind::Group);
    CHECK(list[3].open == "(");
    CHECK(list[3].close == ")");
    CHECK(list[4].text == "α");

    const calc::MathList cases = calc::parse_latex(R"(\begin{cases} 1 & \text{if } x < 2 \\ 3 & \text{otherwise} \end{cases})");
    REQUIRE(cases.size() == 1);
    CHECK(cases[0].kind == MathNode::Kind::Cases);
    CHECK(cases[0].args.size() == 4);
}

TEST_CASE("LaTeX mistakes are explained") {
    CHECK_THROWS_WITH_AS(calc::parse_latex("\\frac{a}{b"), "A { is never closed with a }.", FormulaError);
    CHECK_THROWS_WITH_AS(calc::parse_latex("a}"), "There's a } without a {.", FormulaError);
    CHECK_THROWS_WITH_AS(calc::parse_latex("\\left( a"), "A \\left has no matching \\right.", FormulaError);
    CHECK_THROWS_WITH_AS(calc::parse_latex("\\foo"), "Ninkasi doesn't know the command \\foo.", FormulaError);
    CHECK_THROWS_WITH_AS(calc::parse_latex("x^2^3"), "Two superscripts in a row: use braces, like x^{a^{b}}.",
                         FormulaError);
    CHECK_THROWS_AS(calc::parse_latex(std::string(200, '{')), FormulaError);
}

TEST_CASE("formulas compute with their named inputs") {
    CHECK(eval("(OG - FG) \\times 131.25", {"OG", "FG"}, {1.050, 1.010}) == near(5.25, 1e-12));
    // Multi-letter names are one input, however they're written.
    CHECK(eval("\\mathrm{OG} - \\text{FG}", {"OG", "FG"}, {1.050, 1.010}) == near(0.04, 1e-12));
    // Subscripts are part of the name.
    CHECK(eval("T_s - T_{c}", {"T_{s}", "T_c"}, {20.0, 15.0}) == near(5.0, 1e-12));
    // Greek letters.
    CHECK(eval("\\alpha m", {"\\alpha", "m"}, {0.1, 2.0}) == near(0.2, 1e-12));
    CHECK(eval("α m", {"\\alpha", "m"}, {0.1, 2.0}) == near(0.2, 1e-12));
}

TEST_CASE("arithmetic follows the usual precedence") {
    CHECK(eval("1 + 2 \\cdot 3") == near(7.0, 1e-12));
    CHECK(eval("2 \\times 3 + 4") == near(10.0, 1e-12));
    CHECK(eval("8 / 2 / 2") == near(2.0, 1e-12));
    CHECK(eval("-2^{2}") == near(-4.0, 1e-12));
    CHECK(eval("2^{3^{2}}") == near(512.0, 1e-12));
    CHECK(eval("(1 + 2)^2") == near(9.0, 1e-12));
    CHECK(eval("\\left[ 1 + 2 \\right]^{2}") == near(9.0, 1e-12));
    CHECK(eval("0.04 t", {"t"}, {60.0}) == near(2.4, 1e-12));  // side by side multiplies
    CHECK(eval("2(3 + 1)") == near(8.0, 1e-12));
    CHECK(eval("2.39 \\times 10^{11}") == near(2.39e11, 1.0));
    CHECK(eval("\\frac{1}{4} - -1") == near(1.25, 1e-12));
}

TEST_CASE("functions, roots and constants") {
    CHECK(eval("e^{-0.04 t}", {"t"}, {10.0}) == near(std::exp(-0.4), 1e-12));
    CHECK(eval("\\exp\\left(1\\right)") == near(std::exp(1.0), 1e-12));
    CHECK(eval("\\ln e") == near(1.0, 1e-12));
    CHECK(eval("\\log 1000") == near(3.0, 1e-12));
    CHECK(eval("\\log_{2} 8") == near(3.0, 1e-12));
    CHECK(eval("\\tanh\\left(\\frac{t - 31.32}{18.27}\\right)", {"t"}, {31.32}) == near(0.0, 1e-12));
    CHECK(eval("\\sqrt{16}") == near(4.0, 1e-12));
    CHECK(eval("\\sqrt[3]{27}") == near(3.0, 1e-12));
    CHECK(eval("\\min(3, 1, 2)") == near(1.0, 1e-12));
    CHECK(eval("\\max\\left(0, x\\right)", {"x"}, {-5.0}) == near(0.0, 1e-12));
    CHECK(eval("\\left| x \\right|", {"x"}, {-5.0}) == near(5.0, 1e-12));
    CHECK(eval("2\\pi") == near(2.0 * 3.141592653589793, 1e-12));
    // An input called e is the input, not Euler's number.
    CHECK(eval("e + 1", {"e"}, {2.0}) == near(3.0, 1e-12));
}

TEST_CASE("cases pick the first row whose condition holds") {
    const std::string_view growth = R"(\begin{cases} 1.4 & \text{if } X < 1.4 \\
                                          2.33 - 0.67 X & \text{if } 1.4 \le X < 3.5 \\
                                          0 & \text{otherwise} \end{cases})";
    CHECK(eval(growth, {"X"}, {1.0}) == near(1.4, 1e-12));
    CHECK(eval(growth, {"X"}, {2.0}) == near(2.33 - 1.34, 1e-12));
    CHECK(eval(growth, {"X"}, {5.0}) == near(0.0, 1e-12));
    // No row holding is undefined.
    CHECK(std::isnan(eval(R"(\begin{cases} 1 & x > 0 \end{cases})", {"x"}, {-1.0})));
}

TEST_CASE("undefined results are NaN, never infinities") {
    CHECK(std::isnan(eval("1 / x", {"x"}, {0.0})));
    CHECK(std::isnan(eval("\\ln x", {"x"}, {-1.0})));
    CHECK(std::isnan(eval("\\sqrt{x}", {"x"}, {-1.0})));
    // The wrong number of inputs.
    CHECK(std::isnan(Expression::compile("x", std::array<std::string_view, 1>{"x"}).evaluate({1.0, 2.0})));
}

TEST_CASE("formulas that can't be computed are explained") {
    CHECK(error_of("OG - X", {"OG", "FG"}) == "Ninkasi doesn't know \"X\". This formula's inputs are OG and FG.");
    CHECK(error_of("ABV = OG", {"OG"}) == "Leave out the left side and the =: the formula is what comes after it.");
    CHECK(error_of("x +", {"x"}) == "Something is missing at the end of the formula.");
    CHECK(error_of("(x", {"x"}) == "A ( is never closed.");
    CHECK(error_of("x)", {"x"}) == "There's a ) without a matching opening bracket.");
    CHECK(error_of("\\min x", {"x"}) == "\\min needs its arguments in parentheses: \\min(a, b).");
    CHECK(error_of("\\sum_i x_i", {"x_i"}) ==
          "Sums and products (∑, ∏) can't be computed in an adjusted formula.");
    CHECK(error_of("x < 1", {"x"}) == "Comparisons such as < only go in the conditions of \\begin{cases}.");
    CHECK(error_of("1 \\pm 2") == "± gives two answers, so it can't be computed.");
    CHECK(error_of("") == "Part of the formula is empty.");
}
