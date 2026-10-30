#pragma once
// Sales > Taproom takings: what the taproom (or the to-go counter) took in a
// day, entered by hand, for a brewery without a point of sale to import.
// Like an import, they replace the day's expected taproom revenue. Fill it
// in and press Enter.

#include "app/sales_service.h"
#include "domain/sales.h"
#include "domain/timestamp.h"

#include <imgui.h>

#include <string>

namespace ninkasi::ui {

class TakingsDialog {
public:
    TakingsDialog(app::SalesService& sales, float ui_scale);

    void open();
    // Call once per frame, outside any window.
    void draw();

private:
    void draw_form();
    void close();

    app::SalesService& sales_;
    float ui_scale_;

    bool open_requested_ = false;
    bool is_open_ = false;
    ImVec2 center_{};
    std::string error_;
    domain::Date day_{};
    domain::Channel channel_ = domain::Channel::Taproom;
    double amount_ = 0.0;
    std::string note_;
};

} // namespace ninkasi::ui
