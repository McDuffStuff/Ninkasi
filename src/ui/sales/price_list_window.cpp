#include "ui/sales/price_list_window.h"

#include "domain/fuzzy_match.h"
#include "ui/date_widgets.h"
#include "ui/display.h"
#include "ui/inventory/inventory_views.h"
#include "ui/sales/sales_widgets.h"
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
    beers_ = beer_names(recipes_, batches_);
    can_sizes_ = can_sizes(inventory_);
    reload();
}

void PriceListWindow::reload() {
    prices_ = sales_.prices();
    currency_ = sales_.currency();
    deposits_ = sales_.keg_deposits();
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
        price.pour_l = default_pour_l(units_);
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
    draw_deposits();
    draw_taproom_revenue();
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

void PriceListWindow::draw_deposits() {
    const bool admin = sales_.can_set_prices();
    ImGui::BeginDisabled(!admin);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Keg deposits");
    bool changed = false;
    for (PackageType type : domain::kKegTypes) {
        ImGui::PushID(static_cast<int>(type));
        ImGui::SameLine();
        ImGui::TextDisabled("%s", domain::name(type).data());
        ImGui::SameLine();
        double& deposit = type == PackageType::HalfBarrel ? deposits_.half_barrel : deposits_.sixtel;
        edit_money("##deposit", deposit, currency_, 80.0f * ui_scale_);
        changed |= finished();
        ImGui::PopID();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    ImGui::SetItemTooltip("Charged on each keg sold, when the sale asks for it, and refunded when the keg comes "
                          "back. 0 for none.");
    if (changed && admin) {
        try {
            sales_.set_keg_deposits(deposits_);
            error_.clear();
        } catch (const std::exception& e) {
            error_ = e.what();
            revision_ = ~0ull;  // put the saved deposits back
        }
    }
}

void PriceListWindow::draw_taproom_revenue() {
    const bool admin = sales_.can_set_prices();
    ImGui::BeginDisabled(!admin);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Taproom revenue from");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(260.0f * ui_scale_);
    domain::TaproomRevenue revenue = sales_.taproom_revenue();
    if (enum_combo("##taproom_revenue", revenue, domain::kTaproomRevenues) && admin) {
        try {
            sales_.set_taproom_revenue(revenue);
        } catch (const std::exception& e) {
            error_ = e.what();
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    ImGui::SetItemTooltip("Kegs used up: each keg taken out for the taproom counts as the pours it gives, at the "
                          "taproom's pour price and yield.\nPoint-of-sale imports or takings: kegs taken out count "
                          "only as beer used, and the imports or takings say what came in.\nUse one, or the "
                          "taproom's revenue is counted twice.");
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
            changed |= beer_combo("##beer", price.beer, beers_, "Every beer");
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
            edit_money("##price", price.price, sales_.currency(), 80.0f * ui_scale_);
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
    ImGui::SetNextItemWidth(-FLT_MIN);
    return can_size_combo("##can", price.unit_volume_l, can_sizes_, unit, "Any can");
}

} // namespace ninkasi::ui
