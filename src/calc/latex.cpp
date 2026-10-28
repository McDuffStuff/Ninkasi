#include "calc/latex.h"

#include <utility>

namespace ninkasi::calc {

namespace {

// Deep enough for any real formula, shallow enough that nonsense like a
// thousand nested braces fails cleanly instead of overflowing the stack.
constexpr int kMaxDepth = 48;

struct Named {
    std::string_view name;
    std::string_view text;
};

constexpr Named kGreek[] = {
    {"alpha", "α"},   {"beta", "β"},  {"gamma", "γ"},   {"delta", "δ"},   {"epsilon", "ε"}, {"varepsilon", "ε"},
    {"zeta", "ζ"},    {"eta", "η"},   {"theta", "θ"},   {"iota", "ι"},    {"kappa", "κ"},   {"lambda", "λ"},
    {"mu", "μ"},      {"nu", "ν"},    {"xi", "ξ"},      {"pi", "π"},      {"rho", "ρ"},     {"sigma", "σ"},
    {"tau", "τ"},     {"upsilon", "υ"}, {"phi", "φ"},   {"varphi", "φ"},  {"chi", "χ"},     {"psi", "ψ"},
    {"omega", "ω"},   {"Gamma", "Γ"}, {"Delta", "Δ"},   {"Theta", "Θ"},   {"Lambda", "Λ"},  {"Xi", "Ξ"},
    {"Pi", "Π"},      {"Sigma", "Σ"}, {"Phi", "Φ"},     {"Psi", "Ψ"},     {"Omega", "Ω"},
};

constexpr Named kOperators[] = {
    {"times", "×"}, {"cdot", "·"}, {"pm", "±"},  {"div", "÷"},  {"le", "≤"},     {"leq", "≤"}, {"ge", "≥"},
    {"geq", "≥"},   {"ne", "≠"},   {"neq", "≠"}, {"lt", "<"},   {"gt", ">"},     {"approx", "≈"},
};

constexpr Named kSymbols[] = {
    {"infty", "∞"}, {"circ", "°"}, {"degree", "°"}, {"%", "%"}, {"partial", "∂"}, {"{", "{"}, {"}", "}"},
};

constexpr std::string_view kFunctions[] = {"ln",  "log",  "exp",  "sin", "cos",
                                           "tan", "sinh", "cosh", "tanh", "min", "max"};

constexpr Named kBigOperators[] = {{"sum", "∑"}, {"prod", "∏"}};

// Words for upright text.
constexpr std::string_view kTextCommands[] = {"text",   "mathrm", "textrm", "operatorname",
                                              "mathit", "textit", "mathbf", "textbf"};

struct Spacing {
    std::string_view name;
    double ems;
};
constexpr Spacing kSpaces[] = {
    {",", 3.0 / 18.0}, {":", 4.0 / 18.0}, {";", 5.0 / 18.0}, {"!", -3.0 / 18.0},
    {" ", 1.0 / 3.0},  {"quad", 1.0},     {"qquad", 2.0},
};

// Commands that only change TeX's style; the typesetter has one style.
constexpr std::string_view kIgnored[] = {"displaystyle", "textstyle", "limits", "nolimits"};

template <typename Table>
const Named* find_named(const Table& table, std::string_view name) {
    for (const Named& entry : table) {
        if (entry.name == name) return &entry;
    }
    return nullptr;
}

bool is_ascii_letter(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

bool is_digit(char c) {
    return c >= '0' && c <= '9';
}

MathNode node(MathNode::Kind kind, std::string text = {}) {
    MathNode made;
    made.kind = kind;
    made.text = std::move(text);
    return made;
}

MathNode space(double ems) {
    MathNode made = node(MathNode::Kind::Space);
    made.width = ems;
    return made;
}

// The length of the UTF-8 character that starts with byte `lead`.
std::size_t utf8_length(unsigned char lead) {
    if (lead >= 0xF0) return 4;
    if (lead >= 0xE0) return 3;
    if (lead >= 0xC0) return 2;
    return 1;
}

// A UTF-8 character typed directly: Greek letters are letters, the familiar
// operators are operators, and anything else is shown as written.
MathNode from_utf8(std::string_view character) {
    static constexpr std::string_view kOperatorCharacters[] = {"×", "·", "÷", "±", "−", "≤", "≥", "≠", "≈"};
    for (std::string_view op : kOperatorCharacters) {
        if (character == op) return node(MathNode::Kind::Operator, std::string(character));
    }
    for (const Named& letter : kGreek) {
        if (character == letter.text) return node(MathNode::Kind::Letter, std::string(character));
    }
    return node(MathNode::Kind::Symbol, std::string(character));
}

class Reader {
public:
    explicit Reader(std::string_view latex) : s_(latex) {}

    MathList read_all() { return read_list(Stop::End); }

private:
    // What ends the list being read.
    enum class Stop { End, Brace, Bracket, Right, CasesCell };

    bool at_end() const { return pos_ >= s_.size(); }
    char peek() const { return at_end() ? '\0' : s_[pos_]; }
    bool starts_with(std::string_view text) const { return s_.substr(pos_).starts_with(text); }

    void skip_spaces() {
        while (!at_end() && (s_[pos_] == ' ' || s_[pos_] == '\t' || s_[pos_] == '\n' || s_[pos_] == '\r')) ++pos_;
    }

    // Whether a command starting here is \`name` (and not a longer name).
    bool at_command(std::string_view name) const {
        if (!starts_with("\\") || !s_.substr(pos_ + 1).starts_with(name)) return false;
        const std::size_t after = pos_ + 1 + name.size();
        return after >= s_.size() || !is_ascii_letter(s_[after]);
    }

    MathList read_list(Stop stop) {
        if (++depth_ > kMaxDepth) throw FormulaError("The formula is nested too deeply.");
        MathList list;
        while (true) {
            skip_spaces();
            if (at_end()) {
                switch (stop) {
                case Stop::End:       --depth_; return list;
                case Stop::Brace:     throw FormulaError("A { is never closed with a }.");
                case Stop::Bracket:   throw FormulaError("\\sqrt[ is never closed with a ].");
                case Stop::Right:     throw FormulaError("A \\left has no matching \\right.");
                case Stop::CasesCell: throw FormulaError("\\begin{cases} has no matching \\end{cases}.");
                }
            }
            const char c = peek();
            if (c == '}') {
                if (stop != Stop::Brace) throw FormulaError("There's a } without a {.");
                ++pos_;
                --depth_;
                return list;
            }
            if (c == ']' && stop == Stop::Bracket) {
                ++pos_;
                --depth_;
                return list;
            }
            if (c == '&' || starts_with("\\\\") || at_command("end")) {
                if (stop != Stop::CasesCell) {
                    throw FormulaError(c == '&' ? "& is only used inside \\begin{cases}."
                                       : c == '\\' && starts_with("\\\\")
                                           ? "\\\\ is only used inside \\begin{cases}."
                                           : "There's an \\end without a \\begin.");
                }
                --depth_;
                return list;  // the cases reader takes it from here
            }
            if (at_command("right")) {
                if (stop != Stop::Right) throw FormulaError("There's a \\right without a \\left.");
                --depth_;
                return list;
            }
            if (c == '^' || c == '_') {
                ++pos_;
                attach_script(list, c == '^');
                continue;
            }
            list.push_back(read_atom());
        }
    }

    // A superscript or subscript belongs to whatever comes just before it.
    void attach_script(MathList& list, bool superscript) {
        if (list.empty() || list.back().kind == MathNode::Kind::Space) list.push_back(node(MathNode::Kind::Symbol));
        MathList& script = superscript ? list.back().sup : list.back().sub;
        if (!script.empty()) {
            throw FormulaError(superscript ? "Two superscripts in a row: use braces, like x^{a^{b}}."
                                           : "Two subscripts in a row: use braces, like x_{ab}.");
        }
        script = read_argument();
        if (script.empty()) throw FormulaError(superscript ? "A ^ needs something after it." : "A _ needs something after it.");
    }

    // A command's or script's argument: a {group}, or a single character or command.
    MathList read_argument() {
        skip_spaces();
        if (at_end()) return {};
        if (peek() == '{') {
            ++pos_;
            return read_list(Stop::Brace);
        }
        if (is_digit(peek())) return {node(MathNode::Kind::Number, std::string(1, s_[pos_++]))};  // as in x^23 = x^{2}3
        if (peek() == '}' || peek() == '&') return {};
        return {read_atom()};
    }

    MathNode read_atom() {
        const char c = peek();
        if (c == '{') {
            ++pos_;
            MathNode group = node(MathNode::Kind::Group);
            group.args.push_back(read_list(Stop::Brace));
            return group;
        }
        if (c == '\\') return read_command();
        if (is_digit(c) || (c == '.' && pos_ + 1 < s_.size() && is_digit(s_[pos_ + 1]))) return read_number();
        if (is_ascii_letter(c)) return node(MathNode::Kind::Letter, std::string(1, s_[pos_++]));
        ++pos_;
        switch (c) {
        case '(': case '[': return node(MathNode::Kind::Open, std::string(1, c));
        case ')': case ']': return node(MathNode::Kind::Close, std::string(1, c));
        case '+': case '/': case '=': case '<': case '>': case ',':
            return node(MathNode::Kind::Operator, std::string(1, c));
        case '-': return node(MathNode::Kind::Operator, "−");
        case '*': return node(MathNode::Kind::Operator, "·");
        case '|': case '!': case '\'': case ':': case ';': case '.':
            return node(MathNode::Kind::Symbol, std::string(1, c));
        case '~': return space(1.0 / 3.0);
        default: break;
        }
        if (static_cast<unsigned char>(c) >= 0x80) {
            --pos_;
            const std::size_t length = utf8_length(static_cast<unsigned char>(c));
            const std::string_view character = s_.substr(pos_, length);
            pos_ += length;
            return from_utf8(character);
        }
        throw FormulaError(std::string("Ninkasi can't read the character ") + c + " in a formula.");
    }

    MathNode read_number() {
        const std::size_t start = pos_;
        while (!at_end() && is_digit(peek())) ++pos_;
        if (peek() == '.' && pos_ + 1 < s_.size() && is_digit(s_[pos_ + 1])) {
            ++pos_;
            while (!at_end() && is_digit(peek())) ++pos_;
        }
        return node(MathNode::Kind::Number, std::string(s_.substr(start, pos_ - start)));
    }

    std::string read_command_name() {
        ++pos_;  // the backslash
        if (at_end()) throw FormulaError("A formula can't end with a \\.");
        const std::size_t start = pos_;
        if (is_ascii_letter(peek())) {
            while (!at_end() && is_ascii_letter(peek())) ++pos_;
        } else {
            ++pos_;  // a one-character command: \, \{ \% ...
        }
        return std::string(s_.substr(start, pos_ - start));
    }

    MathNode read_command() {
        const std::string name = read_command_name();
        if (name == "frac" || name == "dfrac" || name == "tfrac") {
            MathNode fraction = node(MathNode::Kind::Fraction);
            fraction.args.push_back(read_argument());
            fraction.args.push_back(read_argument());
            if (fraction.args[0].empty() || fraction.args[1].empty()) {
                throw FormulaError("\\frac needs a top and a bottom: \\frac{a}{b}.");
            }
            return fraction;
        }
        if (name == "sqrt") {
            MathNode root = node(MathNode::Kind::Root);
            MathList index;
            skip_spaces();
            if (peek() == '[') {
                ++pos_;
                index = read_list(Stop::Bracket);
            }
            root.args.push_back(read_argument());
            if (root.args[0].empty()) throw FormulaError("\\sqrt needs something under it: \\sqrt{x}.");
            if (!index.empty()) root.args.push_back(std::move(index));
            return root;
        }
        if (name == "left") return read_delimited();
        if (name == "begin") return read_environment();
        for (std::string_view command : kTextCommands) {
            if (name == command) return node(MathNode::Kind::Text, read_braced_text(name));
        }
        if (const Named* letter = find_named(kGreek, name)) return node(MathNode::Kind::Letter, std::string(letter->text));
        if (const Named* op = find_named(kOperators, name)) return node(MathNode::Kind::Operator, std::string(op->text));
        if (const Named* symbol = find_named(kSymbols, name)) return node(MathNode::Kind::Symbol, std::string(symbol->text));
        if (const Named* big = find_named(kBigOperators, name)) return node(MathNode::Kind::BigOperator, std::string(big->text));
        for (std::string_view function : kFunctions) {
            if (name == function) return node(MathNode::Kind::Function, name);
        }
        for (const Spacing& spacing : kSpaces) {
            if (name == spacing.name) return space(spacing.ems);
        }
        for (std::string_view ignored : kIgnored) {
            if (name == ignored) return space(0.0);
        }
        if (name == "right") throw FormulaError("There's a \\right without a \\left.");
        if (name == "end") throw FormulaError("There's an \\end without a \\begin.");
        throw FormulaError("Ninkasi doesn't know the command \\" + name + ".");
    }

    std::string read_braced_text(std::string_view command) {
        skip_spaces();
        if (peek() != '{') throw FormulaError("\\" + std::string(command) + " needs its text in braces: \\" + std::string(command) + "{...}.");
        ++pos_;
        const std::size_t start = pos_;
        int depth = 1;
        while (!at_end()) {
            if (peek() == '{') ++depth;
            if (peek() == '}' && --depth == 0) break;
            ++pos_;
        }
        if (at_end()) throw FormulaError("A { is never closed with a }.");
        std::string text(s_.substr(start, pos_ - start));
        ++pos_;
        return text;
    }

    std::string read_delimiter() {
        skip_spaces();
        if (at_end()) throw FormulaError("\\left and \\right need a delimiter, such as ( or [.");
        const char c = peek();
        if (c == '(' || c == ')' || c == '[' || c == ']' || c == '|' || c == '.') {
            ++pos_;
            return std::string(1, c);
        }
        if (c == '\\') {
            const std::string name = read_command_name();
            if (name == "{" || name == "lbrace") return "{";
            if (name == "}" || name == "rbrace") return "}";
        }
        throw FormulaError("\\left and \\right need a delimiter, such as ( or [.");
    }

    // \left<open> ... \right<close>
    MathNode read_delimited() {
        MathNode group = node(MathNode::Kind::Group);
        group.open = read_delimiter();
        group.args.push_back(read_list(Stop::Right));
        read_command_name();  // \right, where the list stopped
        group.close = read_delimiter();
        return group;
    }

    MathNode read_environment() {
        if (read_braced_text("begin") != "cases") throw FormulaError("The only \\begin Ninkasi knows is \\begin{cases}.");
        MathNode cases = node(MathNode::Kind::Cases);
        while (true) {
            MathList value = read_list(Stop::CasesCell);
            MathList condition;
            if (peek() == '&') {
                ++pos_;
                condition = read_list(Stop::CasesCell);
                if (peek() == '&') throw FormulaError("A row of \\begin{cases} has one &, between its value and its condition.");
            }
            if (!value.empty() || !condition.empty()) {
                cases.args.push_back(std::move(value));
                cases.args.push_back(std::move(condition));
            }
            if (starts_with("\\\\")) {
                pos_ += 2;
                continue;
            }
            read_command_name();  // \end
            if (read_braced_text("end") != "cases") throw FormulaError("\\begin{cases} ends with \\end{cases}.");
            break;
        }
        if (cases.args.empty()) throw FormulaError("\\begin{cases} needs at least one row.");
        return cases;
    }

    std::string_view s_;
    std::size_t pos_ = 0;
    int depth_ = 0;
};

} // namespace

MathList parse_latex(std::string_view latex) {
    // Allow the formula to be wrapped in $...$, as it often is when copied.
    while (!latex.empty() && (latex.front() == '$' || latex.front() == ' ')) latex.remove_prefix(1);
    while (!latex.empty() && (latex.back() == '$' || latex.back() == ' ')) latex.remove_suffix(1);
    return Reader(latex).read_all();
}

} // namespace ninkasi::calc
