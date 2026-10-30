#include "ui/sales/sale_dialog.h"

#include "domain/money.h"
#include "ui/date_widgets.h"
#include "ui/display.h"
#include "ui/inventory/inventory_views.h"
#include "ui/sales/sales_widgets.h"
#include "ui/search_combo.h"
#include "ui/widgets.h"

#include <imgui_stdlib.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <exception>

namespace ninkasi::ui {

namespace {

using domain::PackageType;

constexpr const char* kPopupId = "Record a sale###sale_dialog";

constexpr ImGuiTableFlags kListFlags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                                       ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_PadOuterX;

void label_cell(const char* label) {
    ImGui::TableNextColumn();
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s", label);
}

bool is_blank(const std::string& text) {
    return text.find_first_not_of(" \t") == std::string::npos;
}

} // namespace

SaleDialog::SaleDialog(app::SalesService& sales, const app::PackagedGoodsService& goods,
                       const calc::UnitPreferences& units, float ui_scale)
    : sales_(sales), goods_(goods), units_(units), ui_scale_(ui_scale) {}

void SaleDialog::open() {
    error_.clear();
    customer_id_.reset();
    channel_ = domain::Channel::Distributor;
    at_ = domain::now();
    number_.clear();
    note_.clear();
    beer_lines_.clear();
    other_lines_.clear();
    charge_deposits_ = true;
    center_ = ImGui::GetMainViewport()->GetCenter();
    open_requested_ = true;
    is_open_ = true;
}

void SaleDialog::close() {
    ImGui::CloseCurrentPopup();
    is_open_ = false;
}

void SaleDialog::draw() {
    if (open_requested_) {
        ImGui::OpenPopup(kPopupId);
        open_requested_ = false;
    }
    if (!is_open_) return;
    ImGui::SetNextWindowPos(center_, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    bool keep_open = true;
    if (!ImGui::BeginPopupModal(kPopupId, &keep_open, ImGuiWindowFlags_AlwaysAutoResize)) {
        is_open_ = false;
        return;
    }
    draw_form();
    ImGui::EndPopup();
}

std::optional<double> SaleDialog::list_price(const BeerLine& line) const {
    domain::Sale sale;
    sale.at = at_;
    sale.channel = channel_;
    domain::SaleLine priced;
    priced.beer = line.beer;
    priced.package = line.type;
    priced.unit_volume_l = line.unit_volume_l;
    priced.quantity = line.packs;
    priced.pack_size = line.pack_size;
    return domain::list_price(sales_.prices(), sale, priced);
}

bool SaleDialog::expected(const BeerLine& line) const {
    return channel_ == domain::Channel::Taproom && domain::is_keg(line.type);
}

domain::Sale SaleDialog::sale() const {
    domain::Sale sale;
    sale.at = at_;
    sale.channel = channel_;
    sale.customer_id = customer_id_;
    sale.number = number_;
    sale.note = note_;
    for (const BeerLine& draft : beer_lines_) {
        if (draft.beer.empty()) continue;
        domain::SaleLine line;
        line.beer = draft.beer;
        line.package = draft.type;
        line.unit_volume_l = draft.unit_volume_l;
        line.quantity = draft.packs;
        line.pack_size = draft.pack_size;
        // Otherwise the price list prices it; a keg to the taproom always is.
        if (draft.price_set && !expected(draft)) line.unit_price = draft.price;
        sale.lines.push_back(std::move(line));
    }
    for (const OtherLine& draft : other_lines_) {
        if (is_blank(draft.description)) continue;
        domain::SaleLine line;
        line.kind = domain::LineKind::Other;
        line.description = draft.description;
        line.quantity = draft.quantity;
        line.unit_price = draft.price;
        sale.lines.push_back(std::move(line));
    }
    return sale;
}

void SaleDialog::draw_form() {
    const bool admin = sales_.can_set_prices();
    const float field = 240.0f * ui_scale_;

    // Who bought it, and when.
    if (ImGui::BeginTable("##sale", 4, ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableNextRow();
        label_cell("Customer");
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(field);
        if (customer_combo("##customer", customer_id_, sales_, "No customer", channel_, error_) && customer_id_) {
            if (const domain::Customer* customer = sales_.find_customer(*customer_id_)) channel_ = customer->channel;
        }
        label_cell("Channel");
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(field);
        enum_combo("##channel", channel_, domain::kChannels);
        ImGui::TableNextRow();
        label_cell("When");
        ImGui::TableNextColumn();
        time_picker("##at", at_);
        label_cell("Invoice or order");
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(field);
        ImGui::InputText("##number", &number_);
        ImGui::TableNextRow();
        label_cell("Note");
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(field);
        ImGui::InputText("##note", &note_);
        ImGui::EndTable();
    }

    ImGui::SeparatorText("Beer");
    draw_beer_lines(admin);
    if (admin) {
        ImGui::SeparatorText("Anything else");
        draw_other_lines();
    }

    // Keg deposits, when the brewery charges them on kegs in this sale (to
    // a customer, who has them back when the kegs come back).
    const domain::KegDeposits& deposits = sales_.keg_deposits();
    const domain::Sale draft = sale();
    std::vector<domain::SaleLine> deposit_lines;
    if (customer_id_) deposit_lines = domain::deposit_lines(draft.lines, deposits);
    ImGui::Spacing();
    if (!deposit_lines.empty()) {
        ImGui::Checkbox("Charge keg deposits", &charge_deposits_);
        ImGui::SetItemTooltip("Refunded when the kegs come back (Keg cleaner, Add kegs, from this customer)");
        double total = 0.0;
        std::string kegs;
        for (const domain::SaleLine& line : deposit_lines) {
            total += line.amount();
            if (!kegs.empty()) kegs += ", ";
            kegs += domain::count_text(line.package, static_cast<int>(line.quantity));
        }
        ImGui::SameLine();
        if (admin) {
            ImGui::TextDisabled("on %s: %s", kegs.c_str(), domain::format_money(total, sales_.currency()).c_str());
        } else {
            ImGui::TextDisabled("on %s", kegs.c_str());
        }
        if (!charge_deposits_) deposit_lines.clear();
    }

    // What it comes to.
    if (admin) {
        double total = 0.0;
        double expected_total = 0.0;
        int unpriced = 0;
        for (const BeerLine& line : beer_lines_) {
            if (line.beer.empty()) continue;
            const std::optional<double> price =
                line.price_set && !expected(line) ? std::optional<double>(line.price) : list_price(line);
            if (!price) ++unpriced;
            total += line.packs * price.value_or(0.0);
            if (expected(line)) expected_total += line.packs * price.value_or(0.0);
        }
        for (const OtherLine& line : other_lines_) {
            if (!is_blank(line.description)) total += line.quantity * line.price;
        }
        ImGui::Text("Total %s", domain::format_money(total, sales_.currency()).c_str());
        if (expected_total > 0.0) {
            ImGui::SameLine();
            ImGui::TextDisabled("(%s of it expected)", domain::format_money(expected_total, sales_.currency()).c_str());
        }
        double deposit_total = 0.0;
        for (const domain::SaleLine& line : deposit_lines) deposit_total += line.amount();
        if (deposit_total > 0.0) {
            ImGui::SameLine();
            ImGui::TextDisabled("and %s in deposits", domain::format_money(deposit_total, sales_.currency()).c_str());
        }
        if (unpriced > 0) {
            ImGui::SameLine();
            ImGui::TextColored(kWarningColor, "%d %s no price yet", unpriced, unpriced == 1 ? "line has" : "lines have");
        }
    }

    // Submit, or leave it.
    if (!error_.empty()) ImGui::TextColored(kErrorColor, "%s", error_.c_str());
    ImGui::Spacing();
    ImGui::BeginDisabled(draft.lines.empty());
    const bool submit = ImGui::Button("Submit");
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel") || dialog_escape_pressed()) {
        close();
        return;
    }
    if (!submit) return;
    try {
        sales_.record_sale(draft, charge_deposits_);
        close();
    } catch (const std::exception& e) {
        error_ = e.what();
    }
}

void SaleDialog::draw_beer_lines(bool admin) {
    // Picking a beer on the blank last line adds another.
    if (beer_lines_.empty() || !beer_lines_.back().beer.empty()) beer_lines_.push_back({});
    const std::vector<domain::PackagedProduct> products = domain::combine_by_beer(goods_.packaged());
    const calc::Unit small = fill_volume_unit(units_);
    const auto label = [&](const std::string& beer, PackageType type, double unit_volume_l) {
        return beer + " · " +
               (domain::is_keg(type) ? std::string(domain::name(type)) : "Cans, " + calc::format(unit_volume_l, small));
    };
    const float s = ui_scale_;
    std::optional<std::size_t> remove;
    if (!ImGui::BeginTable("##beer_lines", admin ? 7 : 5, kListFlags)) return;
    ImGui::TableSetupColumn("Beer", ImGuiTableColumnFlags_WidthFixed, 300 * s);
    ImGui::TableSetupColumn("On hand", ImGuiTableColumnFlags_WidthFixed, 70 * s);
    ImGui::TableSetupColumn("How many", ImGuiTableColumnFlags_WidthFixed, 80 * s);
    ImGui::TableSetupColumn("In a pack", ImGuiTableColumnFlags_WidthFixed, 80 * s);
    if (admin) {
        ImGui::TableSetupColumn("Price each", ImGuiTableColumnFlags_WidthFixed, 120 * s);
        ImGui::TableSetupColumn("Amount", ImGuiTableColumnFlags_WidthFixed, 110 * s);
    }
    ImGui::TableSetupColumn("##remove", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFrameHeight());
    ImGui::TableHeadersRow();
    for (std::size_t i = 0; i < beer_lines_.size(); ++i) {
        BeerLine& line = beer_lines_[i];
        ImGui::PushID(static_cast<int>(i));
        ImGui::TableNextRow();

        // The beer, from what's on hand.
        ImGui::TableNextColumn();
        const auto product = std::find_if(products.begin(), products.end(), [&](const domain::PackagedProduct& p) {
            return !line.beer.empty() && p.type == line.type && p.unit_volume_l == line.unit_volume_l &&
                   domain::same_beer(p.beer, line.beer);
        });
        ImGui::SetNextItemWidth(-FLT_MIN);
        const std::string shown =
            line.beer.empty() ? std::string("Choose the beer") : label(line.beer, line.type, line.unit_volume_l);
        if (begin_search_combo("##beer", shown.c_str())) {
            std::vector<std::string> labels;
            for (const domain::PackagedProduct& p : products) labels.push_back(label(p.beer, p.type, p.unit_volume_l));
            const std::size_t chosen_index = static_cast<std::size_t>(product - products.begin());
            for (std::size_t j : search_order(labels)) {
                ImGui::PushID(static_cast<int>(j));
                if (search_item(labels[j].c_str(), j == chosen_index)) {
                    line.beer = products[j].beer;
                    line.type = products[j].type;
                    line.unit_volume_l = products[j].unit_volume_l;
                    line.pack_size = 1;
                    line.price_set = false;
                }
                ImGui::PopID();
            }
            end_search_combo();
        }
        const bool chosen = !line.beer.empty();
        ImGui::BeginDisabled(!chosen);

        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        if (chosen) ImGui::TextDisabled("%d", product != products.end() ? product->count : 0);

        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputInt("##packs", &line.packs, 0, 0, ImGuiInputTextFlags_AutoSelectAll);
        line.packs = std::max(line.packs, 1);

        ImGui::TableNextColumn();
        if (chosen && !domain::is_keg(line.type)) {
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::InputInt("##pack", &line.pack_size, 0, 0, ImGuiInputTextFlags_AutoSelectAll)) {
                line.price_set = false;  // a different pack has its own price
            }
            line.pack_size = std::max(line.pack_size, 1);
            ImGui::SetItemTooltip("Cans in each pack sold: 1 for single cans, 4 for a four-pack, 24 for a case");
        }

        if (admin) {
            // The price list's price, until another is typed for this sale.
            const std::optional<double> listed = chosen ? list_price(line) : std::nullopt;
            const bool is_expected = chosen && expected(line);
            if (!line.price_set || is_expected) line.price = listed.value_or(0.0);
            ImGui::TableNextColumn();
            if (is_expected) {
                // What the keg should bring in by the pour; not typed.
                ImGui::AlignTextToFramePadding();
                if (listed) ImGui::TextDisabled("%s", domain::format_money(line.price, sales_.currency()).c_str());
                ImGui::SetItemTooltip("Its volume times the yield at the taproom's price per fl oz");
            } else if (chosen && edit_money("##price", line.price, sales_.currency(), 80.0f * s)) {
                line.price_set = true;
            }
            if (chosen && !is_expected && !line.price_set && !listed) {
                ImGui::SetItemTooltip("Not on the price list: type a price, or leave it to price later");
            }
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            if (chosen && (line.price_set || listed)) {
                ImGui::TextUnformatted(domain::format_money(line.packs * line.price, sales_.currency()).c_str());
                if (is_expected) {
                    ImGui::SameLine();
                    ImGui::TextDisabled("expected");
                }
            } else if (chosen) {
                ImGui::TextColored(kWarningColor, "No price");
            }
        }
        ImGui::EndDisabled();

        ImGui::TableNextColumn();
        if (chosen) {
            if (ImGui::SmallButton("×")) remove = i;
            ImGui::SetItemTooltip("Take this line off the sale");
        }
        ImGui::PopID();
    }
    ImGui::EndTable();
    if (remove) beer_lines_.erase(beer_lines_.begin() + static_cast<std::ptrdiff_t>(*remove));
    if (products.empty()) wrapped_note("There's no packaged beer on hand to sell.");
    if (std::any_of(beer_lines_.begin(), beer_lines_.end(),
                    [this](const BeerLine& line) { return !line.beer.empty() && expected(line); })) {
        wrapped_note("Kegs sold to the taproom count as poured: they're expected to bring in their volume times "
                     "the yield at the taproom's price per fl oz, until the point of sale is imported (or the "
                     "takings entered) for the day.");
    }
}

void SaleDialog::draw_other_lines() {
    // Typing on the blank last line adds another.
    if (other_lines_.empty() || !is_blank(other_lines_.back().description)) other_lines_.push_back({});
    const float s = ui_scale_;
    std::optional<std::size_t> remove;
    if (!ImGui::BeginTable("##other_lines", 5, kListFlags)) return;
    ImGui::TableSetupColumn("What", ImGuiTableColumnFlags_WidthFixed, 300 * s);
    ImGui::TableSetupColumn("How many", ImGuiTableColumnFlags_WidthFixed, 80 * s);
    ImGui::TableSetupColumn("Price each", ImGuiTableColumnFlags_WidthFixed, 120 * s);
    ImGui::TableSetupColumn("Amount", ImGuiTableColumnFlags_WidthFixed, 110 * s);
    ImGui::TableSetupColumn("##remove", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFrameHeight());
    ImGui::TableHeadersRow();
    for (std::size_t i = 0; i < other_lines_.size(); ++i) {
        OtherLine& line = other_lines_[i];
        const bool described = !is_blank(line.description);
        ImGui::PushID(static_cast<int>(i));
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputTextWithHint("##what", "Merchandise, delivery...", &line.description);
        ImGui::BeginDisabled(!described);
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-FLT_MIN);
        edit_number("##quantity", line.quantity, 0);
        line.quantity = std::max(line.quantity, 0.0);
        ImGui::TableNextColumn();
        edit_money("##price", line.price, sales_.currency(), 80.0f * s);
        line.price = std::max(line.price, 0.0);
        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        if (described) {
            ImGui::TextUnformatted(domain::format_money(line.quantity * line.price, sales_.currency()).c_str());
        }
        ImGui::EndDisabled();
        ImGui::TableNextColumn();
        if (described) {
            if (ImGui::SmallButton("×")) remove = i;
            ImGui::SetItemTooltip("Take this line off the sale");
        }
        ImGui::PopID();
    }
    ImGui::EndTable();
    if (remove) other_lines_.erase(other_lines_.begin() + static_cast<std::ptrdiff_t>(*remove));
}

} // namespace ninkasi::ui
