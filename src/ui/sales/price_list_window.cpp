#include "ui/sales/price_list_window.h"

#include "domain/fuzzy_match.h"
#include "ui/date_widgets.h"
#include "ui/display.h"
#include "ui/inventory/inventory_views.h"
#include "ui/search_combo.h"
#include "ui/widgets.h"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <exception>
#include <optional>
#include <set>

namespace ninkasi::ui {

namespace {

using domain::PackageType;
using domain::PricedAs;

constexpr ImGuiTableFlags kTableFlags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                                        ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit;

// The ways something can be sold, as the "Sold as" dropdown offers them.
enum class SoldAs { HalfBarrel, Sixtel, Cans, Pour };
constexpr const char* kSoldAsNames[] = {"Half-barrel", "Sixtel", "Cans", "Pour"};

SoldAs sold_as(const domain::PriceEntry& price) {
    if (price.priced_as == PricedAs::Pour) return SoldAs::Pour;
    switch (price.package) {
    case PackageType::HalfBarrel: return SoldAs::HalfBarrel;
    case PackageType::Sixtel:     return SoldAs::Sixtel;
    case PackageType::Can:        return SoldAs::Cans;
    }
    return SoldAs::HalfBarrel;
}

bool finished() {
    return ImGui::IsItemDeactivatedAfterEdit();
}

} // namespace

PriceListWindow::PriceListWindow(app::SalesService& sales, app::BatchService& batches, app::RecipeService& recipes,
                                 const app::InventoryService& inventory, const calc::UnitPreferences& units,
                                 float ui_scale)
    : sales_(sales), batches_(batches), recipes_(recipes), inventory_(inventory), units_(units), ui_scale_(ui_scale) {}

void PriceListWindow::show() {
    visible_ = true;
    focus_ = true;
    // The beers there are to price: from recipes and batches, each name once.
    beers_.clear();
    const auto add_beer = [&](const std::string& name) {
        if (name.empty()) return;
        const bool known = std::any_of(beers_.begin(), beers_.end(),
                                       [&](const std::string& beer) { return domain::same_beer(beer, name); });
        if (!known) beers_.push_back(name);
    };
    for (const auto& recipe : recipes_.list()) add_beer(recipe.name);
    for (const auto& batch : batches_.list()) add_beer(batch.name);
    std::sort(beers_.begin(), beers_.end());
    // The sizes of can in inventory.
    can_sizes_.clear();
    for (const app::ItemStock& stock : inventory_.stock()) {
        const auto& size = stock.item.fill_volume_l;
        if (stock.item.category != domain::ItemCategory::Can || !size) continue;
        if (std::none_of(can_sizes_.begin(), can_sizes_.end(), [&](double known) { return std::abs(known - *size) < 0.0005; })) {
            can_sizes_.push_back(*size);
        }
    }
    reload();
}

void PriceListWindow::reload() {
    prices_ = sales_.prices();
    currency_ = sales_.currency();
    revision_ = sales_.revision();
}

void PriceListWindow::save(const domain::PriceEntry& price) {
    try {
        sales_.update_price(price);
        error_.clear();
    } catch (const std::exception& e) {
        error_ = e.what();
        revision_ = ~0ull;  // put the saved values back
    }
}

void PriceListWindow::add(const domain::PriceEntry& price) {
    try {
        sales_.add_price(price);
        error_.clear();
    } catch (const std::exception& e) {
        error_ = e.what();
    }
}

domain::PriceEntry PriceListWindow::starting_price(domain::Channel channel) const {
    domain::PriceEntry price;
    price.channel = channel;
    price.effective_from = domain::today();
    if (channel == domain::Channel::Taproom) {
        price.priced_as = PricedAs::Pour;
        // A pint: 16 US fl oz where beer is measured in US units, half a liter elsewhere.
        price.pour_l = fill_volume_unit(units_) == calc::Unit::UsFluidOunce
                           ? calc::to_canonical(16.0, calc::Unit::UsFluidOunce)
                           : 0.5;
        price.yield = 0.9;
    } else if (channel == domain::Channel::ToGo) {
        price.package = PackageType::Can;
        price.pack_size = 4;
    }
    return price;
}

void PriceListWindow::draw() {
    if (!visible_) return;
    if (revision_ != sales_.revision()) reload();
    ImGui::SetNextWindowSize(ImVec2(940.0f, 540.0f) * ui_scale_, ImGuiCond_FirstUseEver);
    const bool raise = focus_;
    if (focus_) {
        ImGui::SetNextWindowFocus();
        focus_ = false;
    }
    const bool expanded = ImGui::Begin("Price list", &visible_);
    if (raise) raise_os_window();
    if (!expanded) {
        ImGui::End();
        return;
    }

    const bool admin = sales_.can_set_prices();
    if (!admin) wrapped_note("Only an admin can change prices.");
    draw_currency();
    ImGui::Spacing();
    wrapped_note("A price applies from its date. To change one and keep earlier sales at the old price, use New "
                 "price; to correct a mistake, edit it. A beer's own price comes before the one for every beer.");
    ImGui::Checkbox("Show earlier prices", &show_earlier_);
    if (!error_.empty()) ImGui::TextColored(kErrorColor, "%s", error_.c_str());

    if (ImGui::BeginTabBar("##channels")) {
        for (domain::Channel channel : domain::kChannels) {
            if (ImGui::BeginTabItem(domain::name(channel).data())) {
                draw_channel(channel);
                ImGui::EndTabItem();
            }
        }
        ImGui::EndTabBar();
    }
    ImGui::End();
}

void PriceListWindow::draw_currency() {
    const bool admin = sales_.can_set_prices();
    ImGui::BeginDisabled(!admin);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Currency");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(60.0f * ui_scale_);
    ImGui::InputText("##symbol", &currency_.symbol);
    bool changed = finished();
    ImGui::SameLine();
    changed |= ImGui::Checkbox("After the amount", &currency_.symbol_after);
    ImGui::SameLine();
    changed |= ImGui::Checkbox("Decimal comma", &currency_.decimal_comma);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90.0f * ui_scale_);
    ImGui::InputInt("Decimals", &currency_.decimals, 1, 1);
    changed |= finished() || ImGui::IsItemEdited();
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled("%s", domain::format_money(1234.5, currency_).c_str());
    if (changed && admin) {
        try {
            sales_.set_currency(currency_);
        } catch (const std::exception& e) {
            error_ = e.what();
        }
    }
}

void PriceListWindow::draw_channel(domain::Channel channel) {
    const bool admin = sales_.can_set_prices();
    if (admin && ImGui::Button("Add a price")) add(starting_price(channel));

    // What's shown: the prices in effect today and any starting later, or every price.
    const domain::Date today = domain::today();
    std::set<std::int64_t> current;
    for (const domain::PriceEntry& price : domain::prices_in_effect(prices_, today)) current.insert(price.id);

    std::optional<std::int64_t> remove;
    std::optional<domain::PriceEntry> renew;
    if (ImGui::BeginTable("##prices", 8, kTableFlags, ImVec2(0.0f, -FLT_MIN))) {
        const float s = ui_scale_;
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Beer", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Sold as", ImGuiTableColumnFlags_WidthFixed, 110 * s);
        ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 120 * s);
        ImGui::TableSetupColumn("In a pack", ImGuiTableColumnFlags_WidthFixed, 80 * s);
        ImGui::TableSetupColumn("Yield", ImGuiTableColumnFlags_WidthFixed, 70 * s);
        ImGui::TableSetupColumn("Price", ImGuiTableColumnFlags_WidthFixed, 110 * s);
        ImGui::TableSetupColumn("From", ImGuiTableColumnFlags_WidthFixed, 130 * s);
        ImGui::TableSetupColumn("##actions", ImGuiTableColumnFlags_WidthFixed, 110 * s);
        ImGui::TableHeadersRow();

        for (domain::PriceEntry& price : prices_) {
            if (price.channel != channel) continue;
            const bool upcoming = price.effective_from > today;
            const bool earlier = !current.count(price.id) && !upcoming;
            if (earlier && !show_earlier_) continue;
            bool changed = false;
            ImGui::PushID(static_cast<int>(price.id));
            ImGui::TableNextRow();
            if (earlier) ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.55f);
            ImGui::BeginDisabled(!admin);

            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            changed |= beer_combo(price);
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            changed |= sold_as_combo(price);
            ImGui::TableNextColumn();
            changed |= size_field(price);
            ImGui::TableNextColumn();
            if (price.priced_as == PricedAs::Package && price.package == PackageType::Can) {
                ImGui::SetNextItemWidth(-FLT_MIN);
                ImGui::InputInt("##pack", &price.pack_size, 0, 0);
                changed |= finished();
                ImGui::SetItemTooltip("Cans in each unit sold: 1 for single cans, 4 for a 4-pack, 24 for a case");
            }
            ImGui::TableNextColumn();
            if (price.priced_as == PricedAs::Pour) {
                ImGui::SetNextItemWidth(-FLT_MIN);
                edit_percent("##yield", price.yield, 0);
                changed |= finished();
                ImGui::SetItemTooltip("The share of a keg sold as pours, after foam, spills and the last drops");
            }
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(80.0f * ui_scale_);
            edit_money("##price", price.price, sales_.currency());
            changed |= finished();
            ImGui::TableNextColumn();
            changed |= date_picker("##from", price.effective_from);
            ImGui::EndDisabled();

            ImGui::TableNextColumn();
            if (admin) {
                if (ImGui::SmallButton("New price")) {
                    renew = price;
                    renew->id = 0;
                    renew->effective_from = today;
                }
                ImGui::SetItemTooltip("Start a new price from today, keeping this one for earlier sales");
                ImGui::SameLine();
                if (ImGui::SmallButton("×")) remove = price.id;
            } else if (upcoming) {
                ImGui::TextDisabled("upcoming");
            }
            if (earlier) ImGui::PopStyleVar();
            ImGui::PopID();
            if (changed && admin) save(price);
        }
        ImGui::EndTable();
    }
    if (renew) add(*renew);
    if (remove) {
        try {
            sales_.remove_price(*remove);
        } catch (const std::exception& e) {
            error_ = e.what();
        }
    }
}

bool PriceListWindow::beer_combo(domain::PriceEntry& price) {
    bool changed = false;
    if (begin_search_combo("##beer", price.beer.empty() ? "Every beer" : price.beer.c_str())) {
        if (!searching() && search_item("Every beer", price.beer.empty())) {
            price.beer.clear();
            changed = true;
        }
        const std::vector<std::size_t> order = search_order(beers_);
        for (std::size_t i : order) {
            if (search_item(beers_[i].c_str(), domain::same_beer(beers_[i], price.beer))) {
                price.beer = beers_[i];
                changed = true;
            }
        }
        // A beer not brewed yet: priced under the name typed.
        if (searching() && order.empty()) {
            const std::string typed = search_text();
            if (search_item(("Use \"" + typed + "\"").c_str(), false)) {
                price.beer = typed;
                changed = true;
            }
        }
        end_search_combo();
    }
    return changed;
}

bool PriceListWindow::sold_as_combo(domain::PriceEntry& price) {
    const SoldAs current = sold_as(price);
    bool changed = false;
    if (begin_search_combo("##sold_as", kSoldAsNames[static_cast<int>(current)])) {
        const std::vector<std::string> names(std::begin(kSoldAsNames), std::end(kSoldAsNames));
        for (std::size_t i : search_order(names)) {
            const auto choice = static_cast<SoldAs>(i);
            if (!search_item(names[i].c_str(), choice == current) || choice == current) continue;
            changed = true;
            price.priced_as = choice == SoldAs::Pour ? PricedAs::Pour : PricedAs::Package;
            price.pack_size = 1;
            if (choice == SoldAs::HalfBarrel) price.package = PackageType::HalfBarrel;
            if (choice == SoldAs::Sixtel) price.package = PackageType::Sixtel;
            if (choice == SoldAs::Cans) price.package = PackageType::Can;
            if (choice == SoldAs::Pour && !(price.pour_l > 0.0)) price.pour_l = starting_price(domain::Channel::Taproom).pour_l;
        }
        end_search_combo();
    }
    return changed;
}

bool PriceListWindow::size_field(domain::PriceEntry& price) {
    const calc::Unit unit = fill_volume_unit(units_);
    if (price.priced_as == PricedAs::Pour) {
        ImGui::SetNextItemWidth(70.0f * ui_scale_);
        edit_quantity("##pour", price.pour_l, unit);
        return finished();
    }
    if (price.package != PackageType::Can) {
        // A keg's size, in gallons where cans are in fluid ounces, else liters.
        const calc::Unit keg_unit = unit == calc::Unit::UsFluidOunce ? calc::Unit::UsGallon : calc::Unit::Liter;
        ImGui::TextDisabled("%s", calc::format(domain::keg_volume_l(price.package), keg_unit).c_str());
        return false;
    }
    // A size of can from inventory, or any can.
    bool changed = false;
    const std::string shown = price.unit_volume_l > 0.0 ? calc::format(price.unit_volume_l, unit) : "Any can";
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (begin_search_combo("##can", shown.c_str())) {
        std::vector<std::string> names = {"Any can"};
        for (double size : can_sizes_) names.push_back(calc::format(size, unit));
        for (std::size_t i : search_order(names)) {
            const double size = i == 0 ? 0.0 : can_sizes_[i - 1];
            if (search_item(names[i].c_str(), std::abs(size - price.unit_volume_l) < 0.0005)) {
                price.unit_volume_l = size;
                changed = true;
            }
        }
        end_search_combo();
    }
    return changed;
}

} // namespace ninkasi::ui
