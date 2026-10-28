#pragma once
// Packaged beer and kegs on screen: the packaged goods window (what's on
// hand and the packaging log), the keg cleaner's window (dirty kegs, clean
// kegs and the keg log) and the keg cleaner's dialogs for cleaning kegs,
// adding kegs and removing them. Like the other dialogs, these log what
// already happened: fill them in and press Enter.

#include "app/packaged_goods_service.h"
#include "calc/units.h"
#include "domain/packaging.h"
#include "domain/timestamp.h"

#include <imgui.h>

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ninkasi::ui {

class PackagingViews {
public:
    PackagingViews(app::PackagedGoodsService& goods, float ui_scale);

    // The packaged goods tabs.
    void draw_packaged_stock(const calc::UnitPreferences& units);
    void draw_packaging_log(const calc::UnitPreferences& units);

    // The keg cleaner's tabs.
    void draw_dirty_kegs();
    void draw_clean_kegs();
    void draw_keg_history();

    // Keg actions, also offered by the floor's right-click menu.
    void clean_kegs();
    void clean_all();
    void add_kegs();
    void remove_kegs(domain::KegState state);

    // Call once per frame, outside any window: the dialogs.
    void draw();

private:
    enum class Kind { None, Clean, Add, Remove };

    // Empty kegs of each size in one state, as a small table.
    void draw_counts(domain::KegState state);
    void open(Kind kind);
    void close();
    // A whole-number field for each keg size. With a `limit`, each is capped
    // at how many kegs of that size are in that state (what can be cleaned
    // or removed).
    void keg_fields(std::optional<domain::KegState> limit);
    // The error, if any, then the confirm and Cancel buttons; true when the
    // confirm button or Enter is pressed.
    bool buttons(const char* confirm, bool enabled);
    void draw_clean();
    void draw_add();
    void draw_remove();

    app::PackagedGoodsService& goods_;
    float ui_scale_;

    // The packaged product whose batches are shown.
    struct Selection {
        std::string beer;
        domain::PackageType type = domain::PackageType::HalfBarrel;
        double unit_volume_l = 0.0;
    };
    std::optional<Selection> selected_;

    // The keg and packaging logs, re-read when they change.
    std::uint64_t history_revision_ = 0;
    std::vector<data::KegMovementRow> history_;
    std::uint64_t packaging_revision_ = 0;
    std::vector<data::PackagedMovementRow> packaging_log_;

    // The open dialog and its form.
    Kind kind_ = Kind::None;
    bool open_requested_ = false;
    ImVec2 center_{};
    std::string error_;
    std::array<int, domain::kKegTypes.size()> counts_{};  // by position in kKegTypes
    domain::KegState state_ = domain::KegState::Dirty;   // removing from
    domain::Timestamp at_{};
    std::string note_;
};

} // namespace ninkasi::ui
