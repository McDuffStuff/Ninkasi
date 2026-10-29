#include "ui/sales/expenses_window.h"

#include "domain/money.h"
#include "ui/date_widgets.h"
#include "ui/display.h"
#include "ui/search_combo.h"
#include "ui/widgets.h"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cfloat>
#include <exception>
#include <map>
#include <optional>

namespace ninkasi::ui {

namespace {

constexpr ImGuiTableFlags kTableFlags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                                        ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_PadOuterX;

bool finished() {
    return ImGui::IsItemDeactivatedAfterEdit();
}

// "< October 2026 >": a month, stepped back or forward. True when it changes.
bool month_picker(const char* id, domain::Month& month, float width) {
    ImGui::PushID(id);
    bool changed = false;
    const float gap = ImGui::GetStyle().ItemInnerSpacing.x;
    if (ImGui::ArrowButton("##earlier", ImGuiDir_Left)) {
        month = domain::add_months(month, -1);
        changed = true;
    }
    ImGui::SameLine(0.0f, gap);
    const std::string name = domain::month_name(month);
    ImGui::BeginDisabled();
    ImGui::Button(name.c_str(), ImVec2(width, 0.0f));
    ImGui::EndDisabled();
    ImGui::SameLine(0.0f, gap);
    if (ImGui::ArrowButton("##later", ImGuiDir_Right)) {
        month = domain::add_months(month, 1);
        changed = true;
    }
    ImGui::PopID();
    return changed;
}

} // namespace

ExpensesWindow::ExpensesWindow(app::ExpenseService& expenses, const app::SalesService& sales, float ui_scale)
    : expenses_(expenses), sales_(sales), ui_scale_(ui_scale) {}

void ExpensesWindow::show() {
    visible_ = true;
    focus_ = true;
    month_ = domain::month_of(domain::today());
    error_.clear();
    revision_ = ~0ull;
}

void ExpensesWindow::reload() {
    rows_ = expenses_.expenses(domain::first_day(month_), domain::last_day(month_));
    recurring_lines_.clear();
    for (domain::ExpenseLine& line : expenses_.lines_between(domain::first_day(month_), domain::last_day(month_))) {
        if (line.recurring) recurring_lines_.push_back(std::move(line));
    }
    recurring_ = expenses_.recurring();
    categories_ = expenses_.categories();
    revision_ = expenses_.revision();
    loaded_month_ = month_;
}

template <typename Change>
void ExpensesWindow::save(Change change) {
    try {
        change();
        error_.clear();
    } catch (const std::exception& e) {
        error_ = e.what();
        revision_ = ~0ull;  // put the saved values back
    }
}

std::int64_t ExpensesWindow::default_category() const {
    std::int64_t first = 0;
    for (const domain::ExpenseCategory& category : expenses_.categories()) {
        if (!category.active) continue;
        if (category.name == "Other") return category.id;
        if (first == 0) first = category.id;
    }
    return first;
}

bool ExpensesWindow::category_combo(const char* label, std::int64_t& category_id) {
    const domain::ExpenseCategory* chosen = expenses_.find_category(category_id);
    if (!begin_search_combo(label, chosen ? chosen->name.c_str() : "Choose")) return false;
    std::vector<std::int64_t> ids;
    std::vector<std::string> names;
    for (const domain::ExpenseCategory& category : expenses_.categories()) {
        if (!category.active && category.id != category_id) continue;
        ids.push_back(category.id);
        names.push_back(category.name);
    }
    bool changed = false;
    for (std::size_t i : search_order(names)) {
        if (search_item(names[i].c_str(), ids[i] == category_id) && ids[i] != category_id) {
            category_id = ids[i];
            changed = true;
        }
    }
    end_search_combo();
    return changed;
}

void ExpensesWindow::draw() {
    if (!visible_) return;
    if (!expenses_.allowed()) {
        visible_ = false;  // signed in as someone who may not see them
        return;
    }
    if (revision_ != expenses_.revision() || loaded_month_ != month_) reload();
    ImGui::SetNextWindowSize(ImVec2(1050.0f, 600.0f) * ui_scale_, ImGuiCond_FirstUseEver);
    const bool raise = focus_;
    if (focus_) {
        ImGui::SetNextWindowFocus();
        focus_ = false;
    }
    const bool expanded = ImGui::Begin("Expenses", &visible_);
    if (raise) raise_os_window();
    if (!expanded) {
        ImGui::End();
        return;
    }
    if (!error_.empty()) ImGui::TextColored(kErrorColor, "%s", error_.c_str());
    if (ImGui::BeginTabBar("##expense_tabs")) {
        if (ImGui::BeginTabItem("By month")) {
            draw_month();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Every month")) {
            draw_recurring();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Categories")) {
            draw_categories();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::End();
}

void ExpensesWindow::draw_month() {
    const domain::Currency& currency = sales_.currency();
    const float s = ui_scale_;
    if (ImGui::Button("Add expense")) {
        save([&] {
            const domain::Date today = domain::today();
            const domain::Date day = domain::month_of(today) == month_ ? today : domain::first_day(month_);
            payee_to_focus_ = expenses_.add_expense({0, day, "", default_category(), 0.0, "", ""});
        });
    }
    ImGui::SameLine();
    month_picker("##month", month_, 150.0f * s);

    // The month's total, by category, recurring expenses included.
    std::map<std::int64_t, double> by_category;
    double total = 0.0;
    for (const data::ExpenseRow& row : rows_) {
        by_category[row.expense.category_id] += row.expense.amount;
        total += row.expense.amount;
    }
    for (const domain::ExpenseLine& line : recurring_lines_) {
        by_category[line.category_id] += line.amount;
        total += line.amount;
    }
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::Text("Total %s", domain::format_money(total, currency).c_str());
    wrapped_note("Ingredients and packaging aren't expenses here: they come in on inventory invoices, and their "
                 "cost goes into the beer.");

    if (ImGui::BeginTable("##expenses", 7, kTableFlags)) {
        ImGui::TableSetupColumn("Day", ImGuiTableColumnFlags_WidthFixed, 120 * s);
        ImGui::TableSetupColumn("Paid to", ImGuiTableColumnFlags_WidthFixed, 200 * s);
        ImGui::TableSetupColumn("Category", ImGuiTableColumnFlags_WidthFixed, 190 * s);
        ImGui::TableSetupColumn("Amount", ImGuiTableColumnFlags_WidthFixed, 120 * s);
        ImGui::TableSetupColumn("Invoice", ImGuiTableColumnFlags_WidthFixed, 100 * s);
        ImGui::TableSetupColumn("Note", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("##remove", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFrameHeight());
        ImGui::TableHeadersRow();
        std::optional<std::int64_t> remove;
        for (data::ExpenseRow& row : rows_) {
            domain::Expense& expense = row.expense;
            bool changed = false;
            ImGui::PushID(static_cast<int>(expense.id));
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            changed |= date_picker("##day", expense.day);
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (expense.id == payee_to_focus_) {
                ImGui::SetKeyboardFocusHere();
                payee_to_focus_ = 0;
            }
            ImGui::InputTextWithHint("##payee", "Who was paid", &expense.payee);
            changed |= finished();
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            changed |= category_combo("##category", expense.category_id);
            ImGui::TableNextColumn();
            edit_money("##amount", expense.amount, currency, 90.0f * s);
            changed |= finished();
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::InputText("##number", &expense.number);
            changed |= finished();
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::InputText("##note", &expense.note);
            changed |= finished();
            ImGui::TableNextColumn();
            if (ImGui::SmallButton("×")) remove = expense.id;
            ImGui::SetItemTooltip("Delete this expense");
            ImGui::PopID();
            if (changed) save([&] { expenses_.update_expense(expense); });
        }
        // The month's recurring expenses, set on the Every month tab.
        for (const domain::ExpenseLine& line : recurring_lines_) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", date_text(line.day).c_str());
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", line.description.c_str());
            ImGui::TableNextColumn();
            const domain::ExpenseCategory* category = expenses_.find_category(line.category_id);
            ImGui::TextDisabled("%s", category ? category->name.c_str() : "");
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", domain::format_money(line.amount, currency).c_str());
            ImGui::TableNextColumn();
            ImGui::TableNextColumn();
            ImGui::TextDisabled("every month");
        }
        ImGui::EndTable();
        if (remove) save([&] { expenses_.remove_expense(*remove); });
    }
    if (rows_.empty() && recurring_lines_.empty()) wrapped_note("No expenses this month.");

    // By category.
    if (by_category.empty()) return;
    ImGui::SeparatorText("By category");
    if (ImGui::BeginTable("##by_category", 2, kTableFlags)) {
        ImGui::TableSetupColumn("Category", ImGuiTableColumnFlags_WidthFixed, 220 * s);
        ImGui::TableSetupColumn("Amount", ImGuiTableColumnFlags_WidthFixed, 120 * s);
        for (const auto& [id, amount] : by_category) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            const domain::ExpenseCategory* category = expenses_.find_category(id);
            ImGui::TextUnformatted(category ? category->name.c_str() : "");
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(domain::format_money(amount, currency).c_str());
        }
        ImGui::EndTable();
    }
}

void ExpensesWindow::draw_recurring() {
    const domain::Currency& currency = sales_.currency();
    const float s = ui_scale_;
    if (ImGui::Button("Add a monthly expense")) {
        save([&] {
            expenses_.add_recurring(
                {0, default_category(), "New expense", 0.0, domain::month_of(domain::today()), std::nullopt});
        });
    }
    ImGui::SameLine();
    ImGui::TextDisabled("Rent, wages, loans...: the same each month, counted on the first.");
    if (!ImGui::BeginTable("##recurring", 6, kTableFlags)) return;
    ImGui::TableSetupColumn("What", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Category", ImGuiTableColumnFlags_WidthFixed, 190 * s);
    ImGui::TableSetupColumn("Each month", ImGuiTableColumnFlags_WidthFixed, 120 * s);
    ImGui::TableSetupColumn("From", ImGuiTableColumnFlags_WidthFixed, 200 * s);
    ImGui::TableSetupColumn("Until", ImGuiTableColumnFlags_WidthFixed, 230 * s);
    ImGui::TableSetupColumn("##remove", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFrameHeight());
    ImGui::TableHeadersRow();
    std::optional<std::int64_t> remove;
    for (domain::RecurringExpense& expense : recurring_) {
        bool changed = false;
        ImGui::PushID(static_cast<int>(expense.id));
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputText("##what", &expense.description);
        changed |= finished();
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-FLT_MIN);
        changed |= category_combo("##category", expense.category_id);
        ImGui::TableNextColumn();
        edit_money("##monthly", expense.monthly, currency, 90.0f * s);
        changed |= finished();
        ImGui::TableNextColumn();
        changed |= month_picker("##first", expense.first, 120.0f * s);
        ImGui::TableNextColumn();
        bool ends = expense.last.has_value();
        if (ImGui::Checkbox("##ends", &ends)) {
            expense.last = ends ? std::optional<domain::Month>(std::max(expense.first, domain::month_of(domain::today())))
                                : std::nullopt;
            changed = true;
        }
        ImGui::SetItemTooltip("It has a last month");
        ImGui::SameLine();
        if (expense.last) {
            changed |= month_picker("##last", *expense.last, 120.0f * s);
        } else {
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("still going");
        }
        ImGui::TableNextColumn();
        if (ImGui::SmallButton("×")) remove = expense.id;
        ImGui::SetItemTooltip("Delete it, from every month");
        ImGui::PopID();
        if (changed) save([&] { expenses_.update_recurring(expense); });
    }
    ImGui::EndTable();
    if (remove) save([&] { expenses_.remove_recurring(*remove); });
}

void ExpensesWindow::draw_categories() {
    const float s = ui_scale_;
    if (ImGui::Button("Add category")) {
        save([&] {
            // A name not taken yet, to change.
            std::string name = "New category";
            for (int n = 2; std::any_of(categories_.begin(), categories_.end(),
                                        [&](const domain::ExpenseCategory& c) { return c.name == name; });
                 ++n) {
                name = "New category " + std::to_string(n);
            }
            expenses_.add_category(name);
        });
    }
    if (!ImGui::BeginTable("##categories", 2, kTableFlags)) return;
    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, 280 * s);
    ImGui::TableSetupColumn("In use", ImGuiTableColumnFlags_WidthFixed, 60 * s);
    ImGui::TableHeadersRow();
    for (domain::ExpenseCategory& category : categories_) {
        bool changed = false;
        ImGui::PushID(static_cast<int>(category.id));
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputText("##name", &category.name);
        changed |= finished();
        ImGui::TableNextColumn();
        changed |= ImGui::Checkbox("##active", &category.active);
        ImGui::SetItemTooltip("Clear it to stop offering it; expenses already in it keep it");
        ImGui::PopID();
        if (changed) save([&] { expenses_.update_category(category); });
    }
    ImGui::EndTable();
}

} // namespace ninkasi::ui
