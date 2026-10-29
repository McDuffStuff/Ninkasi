#pragma once
// Sales > Record a sale: who bought what, built up line by line and saved
// with its Submit button, like an invoice. The beer leaves packaged goods
// when it's submitted, and is priced from the price list.
//
// Admins see the prices, and can change them for this sale or add other
// charges (merchandise, delivery...); everyone else records what was sold
// and the price list prices it.

#include "app/packaged_goods_service.h"
#include "app/sales_service.h"
#include "calc/units.h"
#include "domain/sales.h"
#include "domain/timestamp.h"

#include <imgui.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ninkasi::ui {

class SaleDialog {
public:
    SaleDialog(app::SalesService& sales, const app::PackagedGoodsService& goods, const calc::UnitPreferences& units,
               float ui_scale);

    // Opens the form for a new sale.
    void open();
    // Call once per frame, outside any window.
    void draw();

private:
    // A line of beer being entered; blank until a beer is chosen.
    struct BeerLine {
        std::string beer;
        domain::PackageType type = domain::PackageType::HalfBarrel;
        double unit_volume_l = 0.0;
        int packs = 1;
        int pack_size = 1;
        double price = 0.0;      // each pack
        bool price_set = false;  // typed for this sale, rather than from the price list
    };
    // Something else sold (admins); blank until it's described.
    struct OtherLine {
        std::string description;
        double quantity = 1.0;
        double price = 0.0;
    };

    void draw_form();
    void draw_beer_lines(bool admin);
    void draw_other_lines();
    // The form as a sale, its prices left for the price list unless typed.
    domain::Sale sale() const;
    // The price list's price for a line, on the sale's date and channel.
    std::optional<double> list_price(const BeerLine& line) const;
    void close();

    app::SalesService& sales_;
    const app::PackagedGoodsService& goods_;
    const calc::UnitPreferences& units_;
    float ui_scale_;

    bool open_requested_ = false;
    bool is_open_ = false;
    ImVec2 center_{};
    std::string error_;
    std::optional<std::int64_t> customer_id_;
    domain::Channel channel_ = domain::Channel::Distributor;
    domain::Timestamp at_{};
    std::string number_;
    std::string note_;
    std::vector<BeerLine> beer_lines_;
    std::vector<OtherLine> other_lines_;
    bool charge_deposits_ = true;
};

} // namespace ninkasi::ui
