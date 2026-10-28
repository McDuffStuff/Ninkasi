#pragma once
// A formula written in LaTeX (calc/latex.h), compiled so it can be computed.
//
// A formula names its inputs as LaTeX symbols, such as "OG", "t", "T_{s}" or
// "\alpha". Inside the formula, a run of letters is read as the longest input
// name it starts with, so "OG" is one input and not O times G; \mathrm{OG}
// and \text{OG} mean the same. A subscript is part of the name (T_s and
// T_{s} are the same input); a superscript is a power.
//
// The arithmetic:
//   + −  and  × · * /  ÷  as usual; things written side by side multiply ("0.04 t")
//   a^{b} powers, \frac{a}{b}, \sqrt{x}, \sqrt[n]{x}, e^{x}, |x| as \left| x \right|
//   \ln \log (base 10, or \log_{b}) \exp \sin \cos \tan \sinh \cosh \tanh,
//     with their argument in parentheses or as the next factor: \ln x
//   \min(a, b, ...) and \max(a, b, ...)
//   \begin{cases} a & \text{if } x < 1 \\ b & \text{otherwise} \end{cases}:
//     the first row whose condition holds; conditions use < > ≤ ≥ = ≠ and can
//     chain (1 \le x < 3); a row with no condition always holds
//   e (2.71828...) and \pi, unless they're the names of inputs
// Spacing commands and \left \right are for looks and don't change the value.

#include "calc/latex.h"

#include <cstdint>
#include <initializer_list>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ninkasi::calc {

class Expression {
public:
    // Compiles `latex`, whose inputs are named by `inputs`. Throws FormulaError
    // saying what's wrong if it can't be read or computed.
    static Expression compile(std::string_view latex, std::span<const std::string_view> inputs);

    // The value with these inputs, in the order they were named. NaN where the
    // formula is undefined (dividing by zero, the log of a negative...) or if
    // the number of inputs is wrong.
    double evaluate(std::span<const double> inputs) const;
    double evaluate(std::initializer_list<double> inputs) const {
        return evaluate(std::span<const double>(inputs.begin(), inputs.size()));
    }

    std::size_t input_count() const { return input_count_; }

    enum class Op : std::uint8_t;
    struct Node;

private:
    double value(int node, std::span<const double> inputs) const;

    std::vector<Node> nodes_;
    int root_ = -1;
    std::size_t input_count_ = 0;

    friend class ExpressionCompiler;
};

enum class Expression::Op : std::uint8_t {
    Constant, Input, Negate, Add, Subtract, Multiply, Divide, Power, Abs,
    Ln, Log10, Exp, Sqrt, Sin, Cos, Tan, Sinh, Cosh, Tanh, Min, Max,
    Cases,    // args: condition, value, condition, value...; a condition of -1 always holds
    Compare,  // args: operands; relations: the comparisons between them
};

struct Expression::Node {
    Op op = Op::Constant;
    double value = 0.0;   // Constant
    std::size_t input = 0;  // Input
    std::vector<int> args;
    std::vector<char> relations;  // Compare: '<' '>' '=', 'l' for ≤, 'g' for ≥, 'n' for ≠
};

} // namespace ninkasi::calc
