#pragma once
// View > TTB reports: the Brewer's Report of Operations laid out like TTB F
// 5130.9 Part 1, and the excise tax due, for a month, quarter or year; the
// brewery's TTB settings; and export to CSV. Everything is worked out from
// the ledgers, so it's always up to date.

#include "app/batch_service.h"
#include "app/packaged_goods_service.h"
#include "app/ttb_service.h"
#include "domain/ttb.h"

#include <cstdint>
#include <string>

namespace ninkasi::ui {

class TtbWindow {
public:
    TtbWindow(app::TtbService& ttb, const app::BatchService& batches, const app::PackagedGoodsService& goods,
              float ui_scale);

    void show();
    bool* visible() { return &visible_; }
    void draw();

private:
    void draw_toolbar();
    void draw_report();
    void draw_tax();
    void draw_settings();
    void export_csv();
    // Works the report out again if the period or the ledgers changed.
    void refresh();

    app::TtbService& ttb_;
    const app::BatchService& batches_;
    const app::PackagedGoodsService& goods_;
    float ui_scale_;

    bool visible_ = false;
    bool focus_ = false;
    domain::ReportPeriod period_;
    bool stale_ = true;
    std::uint64_t batch_revision_ = 0;
    std::uint64_t goods_revision_ = 0;
    app::TtbReport report_;
    std::string brewery_number_;  // being edited
    std::string status_;          // where the last export went, or why it failed
};

} // namespace ninkasi::ui
