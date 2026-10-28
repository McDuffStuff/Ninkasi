#include "calc/expression.h"

#include "calc/common.h"

#include <charconv>
#include <cmath>
#include <numbers>
#include <optional>
#include <utility>

namespace ninkasi::calc {

namespace {

using Kind = MathNode::Kind;
using Op = Expression::Op;

// An input's name as the compiler compares it: its letters, and its subscript.
struct InputName {
    std::string letters;
    std::string subscript;
};

std::string without_spaces(std::string_view text) {
    std::string kept;
    for (const char c : text) {
        if (c != ' ') kept += c;
    }
    return kept;
}

// The letters, digits and words of a subscript, which is all that tells
// subscripts apart: T_s, T_{s} and T_{\text{s}} are the same.
std::string flatten(const MathList& list) {
    std::string text;
    for (const MathNode& node : list) {
        switch (node.kind) {
        case Kind::Letter:
        case Kind::Number:
        case Kind::Text:
        case Kind::Operator:
        case Kind::Symbol:
            text += without_spaces(node.text);
            break;
        case Kind::Group:
            for (const MathList& arg : node.args) text += flatten(arg);
            break;
        default:
            break;
        }
        if (!node.sub.empty()) text += "_" + flatten(node.sub);
    }
    return text;
}

bool is_name_part(const MathNode& node) {
    return node.kind == Kind::Letter || node.kind == Kind::Text;
}

bool is_operator(const MathNode* node, std::initializer_list<std::string_view> texts) {
    if (!node || node->kind != Kind::Operator) return false;
    for (std::string_view text : texts) {
        if (node->text == text) return true;
    }
    return false;
}

constexpr std::string_view kRelations[] = {"<", ">", "=", "≤", "≥", "≠"};

char relation_code(std::string_view text) {
    if (text == "≤") return 'l';
    if (text == "≥") return 'g';
    if (text == "≠") return 'n';
    return text.front();
}

InputName input_name(std::string_view symbol) {
    InputName name;
    std::vector<const MathNode*> parts;
    const MathList list = parse_latex(symbol);
    for (const MathNode& node : list) {
        if (node.kind != Kind::Space) parts.push_back(&node);
    }
    if (parts.empty()) throw FormulaError("An input needs a name.");
    for (std::size_t i = 0; i < parts.size(); ++i) {
        const MathNode& node = *parts[i];
        if (!is_name_part(node) || !node.sup.empty() || (!node.sub.empty() && i + 1 < parts.size())) {
            throw FormulaError("\"" + std::string(symbol) + "\" can't be the name of an input.");
        }
        name.letters += without_spaces(node.text);
        name.subscript = flatten(node.sub);
    }
    return name;
}

} // namespace

// Reads a formula's tree into the expression's nodes, by precedence:
// sums of products of (signed) factors, each factor possibly raised to a power.
class ExpressionCompiler {
public:
    ExpressionCompiler(Expression& expression, std::span<const std::string_view> symbols)
        : expression_(expression), symbols_(symbols.begin(), symbols.end()) {
        for (std::string_view symbol : symbols) inputs_.push_back(input_name(symbol));
    }

    // Compiles all of `list`.
    int whole(const MathList& list) {
        Cursor cursor{&list};
        if (!cursor.peek()) throw FormulaError("Part of the formula is empty.");
        const int result = additive(cursor);
        if (const MathNode* left_over = cursor.peek()) throw unexpected(*left_over);
        return result;
    }

private:
    // Walks a list, stepping over spacing.
    struct Cursor {
        const MathList* list;
        std::size_t index = 0;

        const MathNode* peek() {
            while (index < list->size() && (*list)[index].kind == Kind::Space) ++index;
            return index < list->size() ? &(*list)[index] : nullptr;
        }
        const MathNode& take() {
            const MathNode* node = peek();
            ++index;
            return *node;
        }
    };

    int add(Expression::Node node) {
        expression_.nodes_.push_back(std::move(node));
        return static_cast<int>(expression_.nodes_.size()) - 1;
    }
    int constant(double value) {
        Expression::Node node;
        node.value = value;
        return add(std::move(node));
    }
    int input(std::size_t index) {
        Expression::Node node;
        node.op = Op::Input;
        node.input = index;
        return add(std::move(node));
    }
    int apply(Op op, std::vector<int> args) {
        Expression::Node node;
        node.op = op;
        node.args = std::move(args);
        return add(std::move(node));
    }

    FormulaError unexpected(const MathNode& node) const {
        switch (node.kind) {
        case Kind::Close:
            return FormulaError("There's a " + node.text + " without a matching opening bracket.");
        case Kind::BigOperator:
            return FormulaError("Sums and products (∑, ∏) can't be computed in an adjusted formula.");
        case Kind::Operator:
            if (node.text == "=") {
                return FormulaError("Leave out the left side and the =: the formula is what comes after it.");
            }
            if (node.text == ",") return FormulaError("A comma only separates the arguments of \\min and \\max.");
            for (std::string_view relation : kRelations) {
                if (node.text == relation) {
                    return FormulaError("Comparisons such as " + node.text +
                                        " only go in the conditions of \\begin{cases}.");
                }
            }
            return FormulaError("Something is missing next to " + node.text + ".");
        default:
            return FormulaError("Ninkasi can't compute with \"" + node.text + "\" here.");
        }
    }

    int additive(Cursor& cursor) {
        int left = multiplicative(cursor);
        while (const MathNode* next = cursor.peek()) {
            if (is_operator(next, {"+"})) {
                cursor.take();
                left = apply(Op::Add, {left, multiplicative(cursor)});
            } else if (is_operator(next, {"−"})) {
                cursor.take();
                left = apply(Op::Subtract, {left, multiplicative(cursor)});
            } else if (is_operator(next, {"±"})) {
                throw FormulaError("± gives two answers, so it can't be computed.");
            } else {
                break;
            }
        }
        return left;
    }

    // Things written side by side multiply, like × and ·.
    static bool starts_factor(const MathNode& node) {
        switch (node.kind) {
        case Kind::Letter: case Kind::Text: case Kind::Number: case Kind::Function: case Kind::Fraction:
        case Kind::Root: case Kind::Group: case Kind::Open: case Kind::Cases:
            return true;
        default:
            return false;
        }
    }

    int multiplicative(Cursor& cursor) {
        int left = signed_factor(cursor);
        while (const MathNode* next = cursor.peek()) {
            if (is_operator(next, {"×", "·"})) {
                cursor.take();
                left = apply(Op::Multiply, {left, signed_factor(cursor)});
            } else if (is_operator(next, {"/", "÷"})) {
                cursor.take();
                left = apply(Op::Divide, {left, signed_factor(cursor)});
            } else if (starts_factor(*next)) {
                left = apply(Op::Multiply, {left, factor(cursor)});
            } else {
                break;
            }
        }
        return left;
    }

    int signed_factor(Cursor& cursor) {
        const MathNode* next = cursor.peek();
        if (!next) throw FormulaError("Something is missing at the end of the formula.");
        if (is_operator(next, {"−"})) {
            cursor.take();
            return apply(Op::Negate, {signed_factor(cursor)});
        }
        if (is_operator(next, {"+"})) {
            cursor.take();
            return signed_factor(cursor);
        }
        return factor(cursor);
    }

    // `base` raised to `node`'s superscript, if it has one.
    int with_power(int base, const MathNode& node) {
        if (node.sup.empty()) return base;
        return apply(Op::Power, {base, whole(node.sup)});
    }

    static void no_subscript(const MathNode& node) {
        if (!node.sub.empty()) throw FormulaError("Only names have subscripts, like T_s.");
    }

    int factor(Cursor& cursor) {
        const MathNode* next = cursor.peek();
        if (!next) throw FormulaError("Something is missing at the end of the formula.");
        if (is_name_part(*next)) return name(cursor);
        const MathNode& node = cursor.take();
        switch (node.kind) {
        case Kind::Number: {
            no_subscript(node);
            double value = 0.0;
            const auto [end, error] = std::from_chars(node.text.data(), node.text.data() + node.text.size(), value);
            if (error != std::errc()) throw FormulaError("Ninkasi can't read the number " + node.text + ".");
            return with_power(constant(value), node);
        }
        case Kind::Fraction:
            no_subscript(node);
            return with_power(apply(Op::Divide, {whole(node.args[0]), whole(node.args[1])}), node);
        case Kind::Root: {
            no_subscript(node);
            const int radicand = whole(node.args[0]);
            const int root = node.args.size() > 1
                                 ? apply(Op::Power, {radicand, apply(Op::Divide, {constant(1.0), whole(node.args[1])})})
                                 : apply(Op::Sqrt, {radicand});
            return with_power(root, node);
        }
        case Kind::Group: {
            no_subscript(node);
            if (node.args[0].empty()) throw FormulaError("There's nothing between a pair of brackets.");
            const int inside = whole(node.args[0]);
            return with_power(node.open == "|" ? apply(Op::Abs, {inside}) : inside, node);
        }
        case Kind::Open:
            return parenthesized(cursor, node);
        case Kind::Function:
            return function(cursor, node);
        case Kind::Cases:
            return with_power(cases(node), node);
        default:
            throw unexpected(node);
        }
    }

    // An input, or the constants e and π: the longest input name the letters here start with.
    int name(Cursor& cursor) {
        const MathList& list = *cursor.list;
        const std::size_t start = cursor.index;
        std::optional<std::pair<std::size_t, std::size_t>> found;  // last node of the name, input
        std::string letters;
        for (std::size_t i = start; i < list.size() && is_name_part(list[i]); ++i) {
            letters += without_spaces(list[i].text);
            const std::string subscript = flatten(list[i].sub);
            for (std::size_t k = 0; k < inputs_.size(); ++k) {
                if (inputs_[k].letters == letters && inputs_[k].subscript == subscript) found = {i, k};
            }
            if (!list[i].sub.empty() || !list[i].sup.empty()) break;  // a name ends at its subscript or power
        }
        if (found) {
            cursor.index = found->first + 1;
            return with_power(input(found->second), list[found->first]);
        }

        const MathNode& first = list[start];
        if (first.kind == Kind::Letter && first.sub.empty() && (first.text == "e" || first.text == "π")) {
            cursor.index = start + 1;
            if (first.text == "e" && !first.sup.empty()) return apply(Op::Exp, {whole(first.sup)});
            return with_power(constant(first.text == "e" ? std::numbers::e : std::numbers::pi), first);
        }

        std::string shown = without_spaces(first.text);
        if (!first.sub.empty()) shown += "_" + flatten(first.sub);
        std::string message = "Ninkasi doesn't know \"" + shown + "\".";
        if (!symbols_.empty()) {
            message += symbols_.size() == 1 ? " This formula's input is " : " This formula's inputs are ";
            for (std::size_t k = 0; k < symbols_.size(); ++k) {
                message += (k == 0 ? "" : k + 1 == symbols_.size() ? " and " : ", ") + std::string(symbols_[k]);
            }
            message += ".";
        }
        throw FormulaError(message);
    }

    // ( ... ) or [ ... ] written without \left and \right.
    int parenthesized(Cursor& cursor, const MathNode& open) {
        const int inside = additive(cursor);
        const MathNode* close = cursor.peek();
        if (is_operator(close, {","})) throw unexpected(*close);
        if (!close || close->kind != Kind::Close) throw FormulaError("A " + open.text + " is never closed.");
        const MathNode& closing = cursor.take();
        if ((open.text == "(") != (closing.text == ")")) {
            throw FormulaError("A " + open.text + " is closed with a " + closing.text + ".");
        }
        return with_power(inside, closing);
    }

    // Splits a list at its commas, for \min(a, b).
    static std::vector<MathList> split_at_commas(const MathList& list) {
        std::vector<MathList> parts(1);
        for (const MathNode& node : list) {
            if (is_operator(&node, {","})) {
                parts.emplace_back();
            } else {
                parts.back().push_back(node);
            }
        }
        return parts;
    }

    int function(Cursor& cursor, const MathNode& function_node) {
        const std::string& name = function_node.text;
        if (!function_node.sup.empty()) {
            throw FormulaError("Write a power of \\" + name + " with parentheses, like (\\" + name + " x)^2.");
        }
        const bool many = name == "min" || name == "max";

        // The arguments, and the node whose power applies to the result.
        std::vector<int> args;
        const MathNode* powered = nullptr;
        const MathNode* next = cursor.peek();
        if (next && next->kind == Kind::Open && next->text == "(") {
            cursor.take();
            while (true) {
                args.push_back(additive(cursor));
                const MathNode* after = cursor.peek();
                if (is_operator(after, {","})) {
                    cursor.take();
                    continue;
                }
                if (!after || after->kind != Kind::Close || after->text != ")") {
                    throw FormulaError("The ( after \\" + name + " is never closed.");
                }
                powered = &cursor.take();
                break;
            }
        } else if (next && next->kind == Kind::Group && (next->open == "(" || next->open == "[")) {
            powered = &cursor.take();
            for (const MathList& part : split_at_commas(powered->args[0])) args.push_back(whole(part));
        } else {
            if (many) throw FormulaError("\\" + name + " needs its arguments in parentheses: \\" + name + "(a, b).");
            if (!next) throw FormulaError("\\" + name + " needs something to work on.");
            args.push_back(factor(cursor));
        }

        if (!many && args.size() != 1) throw FormulaError("\\" + name + " takes one argument.");
        int result = 0;
        if (name == "min" || name == "max") {
            result = apply(name == "min" ? Op::Min : Op::Max, std::move(args));
        } else if (name == "log" && !function_node.sub.empty()) {
            result = apply(Op::Divide, {apply(Op::Ln, {args[0]}), apply(Op::Ln, {whole(function_node.sub)})});
        } else {
            static constexpr std::pair<std::string_view, Op> kOps[] = {
                {"ln", Op::Ln},   {"log", Op::Log10}, {"exp", Op::Exp},   {"sin", Op::Sin},   {"cos", Op::Cos},
                {"tan", Op::Tan}, {"sinh", Op::Sinh}, {"cosh", Op::Cosh}, {"tanh", Op::Tanh},
            };
            Op op = Op::Ln;
            for (const auto& [function_name, function_op] : kOps) {
                if (function_name == name) op = function_op;
            }
            result = apply(op, {args[0]});
        }
        return powered ? with_power(result, *powered) : result;
    }

    int cases(const MathNode& node) {
        std::vector<int> args;
        for (std::size_t i = 0; i + 1 < node.args.size(); i += 2) {
            if (node.args[i].empty()) throw FormulaError("Every row of \\begin{cases} needs a value.");
            args.push_back(condition(node.args[i + 1]));
            args.push_back(whole(node.args[i]));
        }
        return apply(Op::Cases, std::move(args));
    }

    // A row's condition, or -1 for one that always holds ("otherwise").
    int condition(const MathList& list) {
        static constexpr std::string_view kWords[] = {"if", "when", "for", "otherwise", "else", ""};
        MathList kept;
        for (const MathNode& node : list) {
            if (node.kind == Kind::Space || is_operator(&node, {","})) continue;
            if (node.kind == Kind::Text) {
                const std::string word = without_spaces(node.text);
                bool filler = false;
                for (std::string_view known : kWords) filler = filler || word == known;
                if (filler) continue;
            }
            kept.push_back(node);
        }
        if (kept.empty()) return -1;

        Cursor cursor{&kept};
        Expression::Node compare;
        compare.op = Op::Compare;
        compare.args.push_back(additive(cursor));
        while (const MathNode* next = cursor.peek()) {
            bool relation = false;
            for (std::string_view text : kRelations) relation = relation || is_operator(next, {text});
            if (!relation) throw unexpected(*next);
            compare.relations.push_back(relation_code(cursor.take().text));
            compare.args.push_back(additive(cursor));
        }
        if (compare.relations.empty()) throw FormulaError("A condition needs a comparison, like x < 1.4.");
        return add(std::move(compare));
    }

    Expression& expression_;
    std::vector<std::string_view> symbols_;
    std::vector<InputName> inputs_;
};

Expression Expression::compile(std::string_view latex, std::span<const std::string_view> inputs) {
    const MathList list = parse_latex(latex);
    for (const MathNode& node : list) {
        if (is_operator(&node, {"="})) {
            throw FormulaError("Leave out the left side and the =: the formula is what comes after it.");
        }
    }
    Expression expression;
    ExpressionCompiler compiler(expression, inputs);
    expression.root_ = compiler.whole(list);
    expression.input_count_ = inputs.size();
    return expression;
}

double Expression::evaluate(std::span<const double> inputs) const {
    if (root_ < 0 || inputs.size() != input_count_) return kInvalid;
    const double result = value(root_, inputs);
    return std::isfinite(result) ? result : kInvalid;
}

double Expression::value(int index, std::span<const double> inputs) const {
    const Node& node = nodes_[static_cast<std::size_t>(index)];
    const auto arg = [&](std::size_t k) { return value(node.args[k], inputs); };
    switch (node.op) {
    case Op::Constant: return node.value;
    case Op::Input:    return inputs[node.input];
    case Op::Negate:   return -arg(0);
    case Op::Add:      return arg(0) + arg(1);
    case Op::Subtract: return arg(0) - arg(1);
    case Op::Multiply: return arg(0) * arg(1);
    case Op::Divide:   return arg(0) / arg(1);
    case Op::Power:    return std::pow(arg(0), arg(1));
    case Op::Abs:      return std::abs(arg(0));
    case Op::Ln:       return std::log(arg(0));
    case Op::Log10:    return std::log10(arg(0));
    case Op::Exp:      return std::exp(arg(0));
    case Op::Sqrt:     return std::sqrt(arg(0));
    case Op::Sin:      return std::sin(arg(0));
    case Op::Cos:      return std::cos(arg(0));
    case Op::Tan:      return std::tan(arg(0));
    case Op::Sinh:     return std::sinh(arg(0));
    case Op::Cosh:     return std::cosh(arg(0));
    case Op::Tanh:     return std::tanh(arg(0));
    case Op::Min:
    case Op::Max: {
        double result = arg(0);
        for (std::size_t k = 1; k < node.args.size(); ++k) {
            const double next = arg(k);
            if (std::isnan(next) || std::isnan(result)) return kInvalid;
            result = node.op == Op::Min ? std::min(result, next) : std::max(result, next);
        }
        return result;
    }
    case Op::Cases:
        for (std::size_t k = 0; k + 1 < node.args.size(); k += 2) {
            if (node.args[k] < 0 || value(node.args[k], inputs) != 0.0) return arg(k + 1);
        }
        return kInvalid;  // no row's condition holds
    case Op::Compare: {
        double left = arg(0);
        for (std::size_t k = 0; k < node.relations.size(); ++k) {
            const double right = arg(k + 1);
            bool holds = false;
            switch (node.relations[k]) {
            case '<': holds = left < right; break;
            case '>': holds = left > right; break;
            case '=': holds = left == right; break;
            case 'l': holds = left <= right; break;
            case 'g': holds = left >= right; break;
            case 'n': holds = left != right; break;
            default: break;
            }
            if (!holds) return 0.0;
            left = right;
        }
        return 1.0;
    }
    }
    return kInvalid;
}

} // namespace ninkasi::calc
