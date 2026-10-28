#include "ui/math_view.h"

#include "calc/latex.h"

#include <imgui.h>

#include <algorithm>
#include <cfloat>
#include <string>
#include <utility>
#include <vector>

namespace ninkasi::ui {

namespace {

using calc::MathList;
using calc::MathNode;
using Kind = MathNode::Kind;

// Proportions of the font size, in ems, after TeX's.
constexpr float kAscent = 0.74f;   // letters and digits above the baseline
constexpr float kDescent = 0.22f;  // and below it
constexpr float kAxis = 0.27f;     // the math axis, where fraction bars and minus signs sit
constexpr float kScriptScale = 0.7f;
constexpr float kThin = 3.0f / 18.0f;
constexpr float kMedium = 4.0f / 18.0f;
constexpr float kThick = 5.0f / 18.0f;

// Something a box draws, relative to its baseline origin (y down).
struct Item {
    enum class Shape { Text, Line, Curve };
    Shape shape = Shape::Text;
    ImVec2 a, b, c;  // Text: its baseline origin. Line: a to b. Curve: a quadratic from a, pulled toward b, to c.
    std::string text;
    float size = 0.0f;
    float thickness = 1.0f;
};

struct Box {
    float width = 0.0f;
    float ascent = 0.0f;   // above the baseline
    float descent = 0.0f;  // below it
    std::vector<Item> items;

    // Adds `child` with its baseline origin at (x, y) in this box.
    void place(const Box& child, float x, float y) {
        const ImVec2 offset(x, y);
        for (Item item : child.items) {
            item.a = item.a + offset;
            item.b = item.b + offset;
            item.c = item.c + offset;
            items.push_back(std::move(item));
        }
        ascent = std::max(ascent, child.ascent - y);
        descent = std::max(descent, child.descent + y);
        width = std::max(width, x + child.width);
    }

    void line(ImVec2 from, ImVec2 to, float thickness) {
        Item item;
        item.shape = Item::Shape::Line;
        item.a = from;
        item.b = to;
        item.thickness = thickness;
        items.push_back(std::move(item));
    }

    void curve(ImVec2 from, ImVec2 toward, ImVec2 to, float thickness) {
        Item item;
        item.shape = Item::Shape::Curve;
        item.a = from;
        item.b = toward;
        item.c = to;
        item.thickness = thickness;
        items.push_back(std::move(item));
    }
};

// The loaded font nearest `size` from above, so text is scaled down rather than up.
ImFont* font_for(float size) {
    ImFont* best = nullptr;
    ImFont* largest = nullptr;
    for (ImFont* font : ImGui::GetIO().Fonts->Fonts) {
        if (!largest || font->FontSize > largest->FontSize) largest = font;
        if (font->FontSize >= size * 0.98f && (!best || font->FontSize < best->FontSize)) best = font;
    }
    return best ? best : largest;
}

Box text_box(std::string_view text, float size) {
    Box box;
    box.width = font_for(size)->CalcTextSizeA(size, FLT_MAX, 0.0f, text.data(), text.data() + text.size()).x;
    box.ascent = kAscent * size;
    box.descent = kDescent * size;
    Item item;
    item.text = std::string(text);
    item.size = size;
    box.items.push_back(std::move(item));
    return box;
}

// TeX's classes of atom, which decide the spacing between them.
enum class Atom { None, Ordinary, Function, Binary, Relation, Open, Close, Punctuation };

Atom atom_of(const MathNode& node) {
    switch (node.kind) {
    case Kind::Operator:
        if (node.text == ",") return Atom::Punctuation;
        if (node.text == "/") return Atom::Ordinary;
        for (std::string_view relation : {"=", "<", ">", "≤", "≥", "≠", "≈"}) {
            if (node.text == relation) return Atom::Relation;
        }
        return Atom::Binary;
    case Kind::Function:
    case Kind::BigOperator:
        return Atom::Function;
    case Kind::Open:
        return Atom::Open;
    case Kind::Close:
        return Atom::Close;
    default:
        return Atom::Ordinary;
    }
}

// Space before `right` when it follows `left`, in ems.
float space_between(Atom left, Atom right) {
    if (left == Atom::None) return 0.0f;
    if (left == Atom::Binary || right == Atom::Binary) return kMedium;
    if (left == Atom::Relation || right == Atom::Relation) return kThick;
    if (left == Atom::Punctuation) return kThin;
    if (left == Atom::Function && right == Atom::Ordinary) return kThin;
    if ((left == Atom::Ordinary || left == Atom::Close) && right == Atom::Function) return kThin;
    return 0.0f;
}

class Typesetter {
public:
    explicit Typesetter(float base_size) : base_(base_size) {}

    // A row of nodes. `fractions` counts the fractions it's inside.
    Box row(const MathList& nodes, float size, int fractions) {
        Box box;
        float x = 0.0f;
        Atom previous = Atom::None;
        for (const MathNode& node : nodes) {
            if (node.kind == Kind::Space) {
                x += static_cast<float>(node.width) * size;
                continue;
            }
            Atom atom = atom_of(node);
            // A minus with nothing before it to subtract from is a sign, not an operator.
            if (atom == Atom::Binary && (previous == Atom::None || previous == Atom::Binary ||
                                         previous == Atom::Relation || previous == Atom::Open ||
                                         previous == Atom::Punctuation || previous == Atom::Function)) {
                atom = Atom::Ordinary;
            }
            float gap = space_between(previous, atom);
            if (previous == Atom::Function && node.kind == Kind::Group && !node.open.empty()) gap = 0.0f;  // ln(x)
            x += gap * size;
            const Box piece = with_scripts(node, size, fractions);
            box.place(piece, x, 0.0f);
            x += piece.width;
            previous = atom;
        }
        box.width = std::max(box.width, x);
        // Even an empty row is a line tall.
        box.ascent = std::max(box.ascent, kAscent * size);
        box.descent = std::max(box.descent, kDescent * size);
        return box;
    }

private:
    float smaller(float size, float scale) const { return std::max(size * scale, base_ * 0.5f); }

    Box with_scripts(const MathNode& node, float size, int fractions) {
        const Box base = nucleus(node, size, fractions);
        if (node.sup.empty() && node.sub.empty()) return base;
        const float script = smaller(size, kScriptScale);
        if (node.kind == Kind::BigOperator) return limits(base, node, size, script, fractions);

        Box result = base;
        const float x = base.width + 0.04f * size;
        Box sup;
        Box sub;
        float raise = 0.0f;
        float lower = 0.0f;
        if (!node.sup.empty()) {
            sup = row(node.sup, script, fractions);
            raise = std::max(0.42f * size, base.ascent - 0.32f * size);
        }
        if (!node.sub.empty()) {
            sub = row(node.sub, script, fractions);
            lower = std::max(0.2f * size, base.descent - 0.1f * size);
        }
        if (!node.sup.empty() && !node.sub.empty()) {
            // Keep the two apart.
            const float gap = (raise - sup.descent) - (sub.ascent - lower);
            if (gap < 0.12f * size) lower += 0.12f * size - gap;
        }
        if (!node.sup.empty()) result.place(sup, x, -raise);
        if (!node.sub.empty()) result.place(sub, x, lower);
        return result;
    }

    // ∑ and ∏ with their limits below and above.
    Box limits(const Box& symbol, const MathNode& node, float size, float script, int fractions) {
        const Box sup = node.sup.empty() ? Box{} : row(node.sup, script, fractions);
        const Box sub = node.sub.empty() ? Box{} : row(node.sub, script, fractions);
        const float width = std::max({symbol.width, sup.width, sub.width});
        Box result;
        result.place(symbol, (width - symbol.width) / 2.0f, 0.0f);
        if (!node.sup.empty()) {
            result.place(sup, (width - sup.width) / 2.0f, -(symbol.ascent + 0.1f * size + sup.descent));
        }
        if (!node.sub.empty()) {
            result.place(sub, (width - sub.width) / 2.0f, symbol.descent + 0.1f * size + sub.ascent);
        }
        return result;
    }

    Box nucleus(const MathNode& node, float size, int fractions) {
        switch (node.kind) {
        case Kind::Fraction: return fraction(node, size, fractions);
        case Kind::Root:     return root(node, size, fractions);
        case Kind::Group:    return group(node, size, fractions);
        case Kind::Cases:    return cases(node, size, fractions);
        case Kind::BigOperator: {
            // Larger, and lowered to sit around the axis.
            Box symbol;
            symbol.place(text_box(node.text, 1.3f * size), 0.0f, 0.12f * size);
            return symbol;
        }
        default:
            return text_box(node.text, size);
        }
    }

    Box fraction(const MathNode& node, float size, int fractions) {
        // A fraction inside a fraction is set smaller.
        const float inner = fractions == 0 ? size : smaller(size, 0.8f);
        const Box top = row(node.args[0], inner, fractions + 1);
        const Box bottom = row(node.args[1], inner, fractions + 1);
        const float rule = std::max(1.0f, 0.06f * size);
        const float gap = 0.14f * size;
        const float pad = 0.1f * size;
        const float width = std::max(top.width, bottom.width) + 2.0f * pad;
        const float axis = kAxis * size;
        Box box;
        box.place(top, (width - top.width) / 2.0f, -(axis + rule / 2.0f + gap + top.descent));
        box.place(bottom, (width - bottom.width) / 2.0f, -axis + rule / 2.0f + gap + bottom.ascent);
        box.line(ImVec2(pad / 2.0f, -axis), ImVec2(width - pad / 2.0f, -axis), rule);
        box.width = width;
        return box;
    }

    Box root(const MathNode& node, float size, int fractions) {
        const Box inside = row(node.args[0], size, fractions);
        const float rule = std::max(1.0f, 0.06f * size);
        const float gap = 0.12f * size;
        const float sign = 0.55f * size;  // the radical's width
        const float top = -(inside.ascent + gap + rule / 2.0f);
        const float bottom = inside.descent;
        const float middle = bottom - 0.45f * (bottom - top);

        Box box;
        float x = 0.0f;
        if (node.args.size() > 1) {
            // The index sits in the crook of the radical.
            const Box index = row(node.args[1], smaller(size, 0.55f), fractions);
            x = std::max(0.0f, index.width - 0.3f * sign);
            box.place(index, x + 0.3f * sign - index.width, middle - 0.15f * size);
        }
        box.line(ImVec2(x, middle), ImVec2(x + 0.3f * sign, bottom), rule);
        box.line(ImVec2(x + 0.3f * sign, bottom), ImVec2(x + sign, top), rule);
        box.line(ImVec2(x + sign, top), ImVec2(x + sign + inside.width + 0.1f * size, top), rule);
        box.place(inside, x + sign + 0.05f * size, 0.0f);
        box.width = x + sign + inside.width + 0.15f * size;
        box.ascent = std::max(box.ascent, -top + rule);
        box.descent = std::max(box.descent, bottom);
        return box;
    }

    Box group(const MathNode& node, float size, int fractions) {
        const Box inside = row(node.args[0], size, fractions);
        if (node.open.empty() && node.close.empty()) return inside;  // {braces} only group
        // \left and \right brackets reach as far above and below the axis as what they hold.
        const float axis = kAxis * size;
        const float half = std::max(inside.ascent - axis, inside.descent + axis);
        Box box;
        float x = 0.0f;
        const Box left = delimiter(node.open, half, size);
        box.place(left, x, 0.0f);
        x += left.width;
        box.place(inside, x, 0.0f);
        x += inside.width;
        const Box right = delimiter(node.close, half, size);
        box.place(right, x, 0.0f);
        box.width = x + right.width;
        return box;
    }

    // A bracket reaching `half` above and below the math axis.
    Box delimiter(const std::string& kind, float half, float size) {
        if (kind.empty() || kind == ".") return Box{};
        // Short ones are the font's own.
        if (half <= 0.62f * size) return text_box(kind, size);

        const float axis = kAxis * size;
        const float top = -(axis + half);
        const float bottom = -axis + half;
        const float height = bottom - top;
        const float width = std::min(0.55f * size, 0.28f * size + 0.04f * height);
        const float thickness = std::max(1.0f, 0.075f * size);
        const float middle = (top + bottom) / 2.0f;
        Box box;
        box.width = width;
        box.ascent = -top;
        box.descent = bottom;
        // Drawn for the left bracket, and mirrored for the right one.
        const bool left = kind == "(" || kind == "[" || kind == "{";
        const auto at = [&](float fraction_of_width, float y) {
            return ImVec2((left ? fraction_of_width : 1.0f - fraction_of_width) * width, y);
        };
        if (kind == "(" || kind == ")") {
            box.curve(at(0.8f, top), at(-0.25f, middle), at(0.8f, bottom), thickness);
        } else if (kind == "[" || kind == "]") {
            box.line(at(0.8f, top), at(0.25f, top), thickness);
            box.line(at(0.25f, top), at(0.25f, bottom), thickness);
            box.line(at(0.25f, bottom), at(0.8f, bottom), thickness);
        } else if (kind == "|") {
            box.line(ImVec2(width / 2.0f, top), ImVec2(width / 2.0f, bottom), thickness);
        } else if (kind == "{" || kind == "}") {
            const float bend = std::min(0.12f * height, 0.3f * size);
            box.curve(at(0.85f, top), at(0.5f, top), at(0.5f, top + bend), thickness);
            box.line(at(0.5f, top + bend), at(0.5f, middle - bend), thickness);
            box.curve(at(0.5f, middle - bend), at(0.5f, middle), at(0.1f, middle), thickness);
            box.curve(at(0.1f, middle), at(0.5f, middle), at(0.5f, middle + bend), thickness);
            box.line(at(0.5f, middle + bend), at(0.5f, bottom - bend), thickness);
            box.curve(at(0.5f, bottom - bend), at(0.5f, bottom), at(0.85f, bottom), thickness);
        } else {
            return text_box(kind, size);
        }
        return box;
    }

    // Rows of "value  condition" behind a brace.
    Box cases(const MathNode& node, float size, int fractions) {
        std::vector<Box> values;
        std::vector<Box> conditions;
        float value_width = 0.0f;
        for (std::size_t i = 0; i + 1 < node.args.size(); i += 2) {
            values.push_back(row(node.args[i], size, fractions));
            conditions.push_back(row(node.args[i + 1], size, fractions));
            value_width = std::max(value_width, values.back().width);
        }
        const float column = value_width + 1.0f * size;
        Box rows;
        float y = 0.0f;
        for (std::size_t k = 0; k < values.size(); ++k) {
            if (k > 0) {
                const float above = std::max(values[k - 1].descent, conditions[k - 1].descent);
                const float below = std::max(values[k].ascent, conditions[k].ascent);
                y += above + 0.3f * size + below;
            }
            rows.place(values[k], 0.0f, y);
            rows.place(conditions[k], column, y);
        }
        // The block centered on the axis, with a brace as tall as it.
        const float axis = kAxis * size;
        const float offset = -axis - (rows.descent - rows.ascent) / 2.0f;
        const float half = (rows.ascent + rows.descent) / 2.0f + 0.05f * size;
        Box box;
        const Box brace = delimiter("{", std::max(half, 0.63f * size), size);
        box.place(brace, 0.0f, 0.0f);
        box.place(rows, brace.width + 0.2f * size, offset);
        return box;
    }

    float base_;
};

void draw(const Box& box, ImVec2 origin) {
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    const ImU32 color = ImGui::GetColorU32(ImGuiCol_Text);
    for (const Item& item : box.items) {
        switch (item.shape) {
        case Item::Shape::Text: {
            if (item.text.empty()) break;
            ImFont* font = font_for(item.size);
            // ImGui places text by its top; the box by its baseline.
            const float ascent = font->Ascent * item.size / font->FontSize;
            draw_list->AddText(font, item.size, origin + item.a - ImVec2(0.0f, ascent), color, item.text.c_str());
            break;
        }
        case Item::Shape::Line:
            draw_list->AddLine(origin + item.a, origin + item.b, color, item.thickness);
            break;
        case Item::Shape::Curve:
            draw_list->AddBezierQuadratic(origin + item.a, origin + item.b, origin + item.c, color, item.thickness);
            break;
        }
    }
}

float size_or_default(float size) {
    return size > 0.0f ? size : ImGui::GetFontSize() * 1.3f;
}

// "result = latex" as one row of nodes. Throws calc::FormulaError.
MathList equation_nodes(std::string_view result, std::string_view latex) {
    MathList list = calc::parse_latex(result);
    MathNode equals;
    equals.kind = Kind::Operator;
    equals.text = "=";
    list.push_back(std::move(equals));
    for (MathNode& node : calc::parse_latex(latex)) list.push_back(std::move(node));
    return list;
}

void show(const MathList& list, float size) {
    const Box box = Typesetter(size).row(list, size, 0);
    const ImVec2 position = ImGui::GetCursorScreenPos();
    draw(box, position + ImVec2(0.0f, box.ascent));
    ImGui::Dummy(ImVec2(box.width, box.ascent + box.descent));
}

} // namespace

bool math_text(std::string_view latex, float size) {
    try {
        show(calc::parse_latex(latex), size_or_default(size));
        return true;
    } catch (const calc::FormulaError&) {
        ImGui::TextUnformatted(latex.data(), latex.data() + latex.size());
        return false;
    }
}

bool math_equation(std::string_view result, std::string_view latex, float size) {
    try {
        show(equation_nodes(result, latex), size_or_default(size));
        return true;
    } catch (const calc::FormulaError&) {
        const std::string plain = std::string(result) + " = " + std::string(latex);
        ImGui::TextUnformatted(plain.c_str());
        return false;
    }
}

ImVec2 math_equation_size(std::string_view result, std::string_view latex, float size) {
    try {
        const float pixels = size_or_default(size);
        const Box box = Typesetter(pixels).row(equation_nodes(result, latex), pixels, 0);
        return ImVec2(box.width, box.ascent + box.descent);
    } catch (const calc::FormulaError&) {
        const std::string plain = std::string(result) + " = " + std::string(latex);
        return ImGui::CalcTextSize(plain.c_str());
    }
}

} // namespace ninkasi::ui
