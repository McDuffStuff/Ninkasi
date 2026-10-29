#pragma once
// Sales > Customers: the distributors, bars, stores and others beer is sold
// to. Anyone can add and edit them; each field is saved when it's finished.

#include "app/sales_service.h"
#include "domain/sales.h"

#include <cstdint>
#include <string>
#include <vector>

namespace ninkasi::ui {

class CustomersWindow {
public:
    CustomersWindow(app::SalesService& sales, float ui_scale);

    void show();
    void draw();

private:
    void reload();
    void save(const domain::Customer& customer);

    app::SalesService& sales_;
    float ui_scale_;

    bool visible_ = false;
    bool focus_ = false;
    std::string filter_;
    bool show_former_ = false;
    std::string error_;
    std::vector<domain::Customer> customers_;  // working copies, saved field by field
    std::uint64_t revision_ = ~0ull;           // of the sales service when last read
    std::int64_t name_to_focus_ = 0;           // a just-added customer, to type its name
};

} // namespace ninkasi::ui
