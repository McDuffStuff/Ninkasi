#include "ui/sales/sales_widgets.h"

#include "ui/display.h"
#include "ui/inventory/inventory_views.h"
#include "ui/search_combo.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <exception>
#include <vector>

namespace ninkasi::ui {

namespace {

// "3", or "2.5" for a part of something.
std::string quantity_text(double quantity) {
    return quantity == std::round(quantity) ? number_text(quantity, 0) : number_text(quantity, 2);
}

std::string trimmed(const std::string& text) {
    const auto first = text.find_first_not_of(" \t");
    if (first == std::string::npos) return {};
    return text.substr(first, text.find_last_not_of(" \t") - first + 1);
}

} // namespace

std::string line_text(const domain::SaleLine& line, const calc::UnitPreferences& units) {
    using domain::LineKind;
    const calc::Unit small = fill_volume_unit(units);
    switch (line.kind) {
    case LineKind::Package: {
        if (domain::is_keg(line.package)) return domain::count_text(line.package, line.packages()) + ", " + line.beer;
        const std::string cans = calc::format(line.unit_volume_l, small) + " cans, " + line.beer;
        if (line.pack_size == 1) return quantity_text(line.quantity) + " × " + cans;
        return quantity_text(line.quantity) + (line.quantity == 1.0 ? " pack of " : " packs of ") +
               std::to_string(line.pack_size) + " × " + cans;
    }
    case LineKind::Pour:
        return quantity_text(line.quantity) + (line.quantity == 1.0 ? " pour of " : " pours of ") +
               calc::format(line.pour_l, small) + ", " + line.beer;
    case LineKind::Deposit: {
        const int kegs = static_cast<int>(std::lround(std::abs(line.quantity)));
        return (line.quantity < 0.0 ? "Deposit refunded on " : "Deposit on ") + domain::count_text(line.package, kegs);
    }
    case LineKind::Other:
        return line.quantity == 1.0 ? line.description : quantity_text(line.quantity) + " × " + line.description;
    }
    return {};
}

std::string sale_text(const domain::Sale& sale, const calc::UnitPreferences& units) {
    std::string text;
    for (std::size_t i = 0; i < sale.lines.size() && i < 2; ++i) {
        if (i > 0) text += "; ";
        text += line_text(sale.lines[i], units);
    }
    if (sale.lines.size() > 2) text += "; and " + std::to_string(sale.lines.size() - 2) + " more";
    return text;
}

bool customer_combo(const char* label, std::optional<std::int64_t>& customer_id, app::SalesService& sales,
                    const char* none, domain::Channel channel, std::string& error) {
    const domain::Customer* chosen = customer_id ? sales.find_customer(*customer_id) : nullptr;
    if (!begin_search_combo(label, chosen ? chosen->name.c_str() : none)) return false;

    // No customer, then those still buying (and the one chosen, even if not).
    std::vector<std::optional<std::int64_t>> ids{std::nullopt};
    std::vector<std::string> names{none};
    for (const domain::Customer& customer : sales.customers()) {
        if (!customer.active && customer.id != customer_id) continue;
        ids.emplace_back(customer.id);
        names.push_back(customer.name);
    }
    bool changed = false;
    for (std::size_t i : search_order(names)) {
        ImGui::PushID(static_cast<int>(i));
        if (search_item(names[i].c_str(), ids[i] == customer_id) && ids[i] != customer_id) {
            customer_id = ids[i];
            changed = true;
        }
        ImGui::PopID();
    }

    // Someone new, named as typed.
    const std::string typed = trimmed(search_text());
    bool known = false;
    for (std::size_t i = 1; i < names.size(); ++i) known = known || domain::same_beer(names[i], typed);
    if (searching() && !typed.empty() && !known) {
        ImGui::Separator();
        const std::string add = "Add \"" + typed + "\" as a customer";
        if (search_item(add.c_str(), false)) {
            try {
                customer_id = sales.add_customer({0, typed, channel, "", "", true});
                changed = true;
            } catch (const std::exception& e) {
                error = e.what();
            }
        }
    }
    end_search_combo();
    return changed;
}

std::vector<std::string> beer_names(app::RecipeService& recipes, app::BatchService& batches) {
    std::vector<std::string> beers;
    const auto add = [&](const std::string& name) {
        if (name.empty()) return;
        const bool known =
            std::any_of(beers.begin(), beers.end(), [&](const std::string& beer) { return domain::same_beer(beer, name); });
        if (!known) beers.push_back(name);
    };
    for (const auto& recipe : recipes.list()) add(recipe.name);
    for (const auto& batch : batches.list()) add(batch.name);
    std::sort(beers.begin(), beers.end());
    return beers;
}

std::vector<double> can_sizes(const app::InventoryService& inventory) {
    std::vector<double> sizes;
    for (const app::ItemStock& stock : inventory.stock()) {
        const auto& size = stock.item.fill_volume_l;
        if (stock.item.category != domain::ItemCategory::Can || !size) continue;
        const bool known =
            std::any_of(sizes.begin(), sizes.end(), [&](double other) { return std::abs(other - *size) < 0.0005; });
        if (!known) sizes.push_back(*size);
    }
    return sizes;
}

double default_pour_l(const calc::UnitPreferences& units) {
    return fill_volume_unit(units) == calc::Unit::UsFluidOunce ? calc::to_canonical(16.0, calc::Unit::UsFluidOunce)
                                                               : 0.5;
}

bool beer_combo(const char* label, std::string& beer, const std::vector<std::string>& beers, const char* every) {
    const char* shown = beer.empty() ? (every ? every : "Choose the beer") : beer.c_str();
    if (!begin_search_combo(label, shown)) return false;
    bool changed = false;
    if (every && !searching() && search_item(every, beer.empty())) {
        beer.clear();
        changed = true;
    }
    const std::vector<std::size_t> order = search_order(beers);
    for (std::size_t i : order) {
        ImGui::PushID(static_cast<int>(i));
        if (search_item(beers[i].c_str(), domain::same_beer(beers[i], beer))) {
            beer = beers[i];
            changed = true;
        }
        ImGui::PopID();
    }
    // A beer not brewed yet, under the name typed.
    if (searching() && order.empty()) {
        const std::string typed = trimmed(search_text());
        if (!typed.empty() && search_item(("Use \"" + typed + "\"").c_str(), false)) {
            beer = typed;
            changed = true;
        }
    }
    end_search_combo();
    return changed;
}

bool can_size_combo(const char* label, double& size_l, const std::vector<double>& sizes, calc::Unit unit,
                    const char* any) {
    const std::string shown = size_l > 0.0 ? calc::format(size_l, unit) : (any ? any : "Choose the size");
    if (!begin_search_combo(label, shown.c_str())) return false;
    std::vector<double> choices;
    std::vector<std::string> names;
    if (any) {
        choices.push_back(0.0);
        names.emplace_back(any);
    }
    for (double size : sizes) {
        choices.push_back(size);
        names.push_back(calc::format(size, unit));
    }
    bool changed = false;
    for (std::size_t i : search_order(names)) {
        ImGui::PushID(static_cast<int>(i));
        if (search_item(names[i].c_str(), std::abs(choices[i] - size_l) < 0.0005)) {
            size_l = choices[i];
            changed = true;
        }
        ImGui::PopID();
    }
    if (sizes.empty()) ImGui::TextDisabled("No cans in inventory yet");
    end_search_combo();
    return changed;
}

} // namespace ninkasi::ui
