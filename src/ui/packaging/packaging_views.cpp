#include "ui/packaging/packaging_views.h"

#include "ui/date_widgets.h"
#include "ui/display.h"
#include "ui/inventory/inventory_views.h"
#include "ui/widgets.h"

#include <imgui_stdlib.h>

#include <algorithm>
#include <cstdlib>
#include <exception>

namespace ninkasi::ui {

namespace {

using domain::KegState;
using domain::PackageType;

constexpr const char* kPopupId = "###keg_dialog";

constexpr ImGuiTableFlags kListFlags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                                       ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_PadOuterX;

int count_in(domain::KegCounts counts, KegState state) {
    return state == KegState::Dirty ? counts.dirty : counts.clean;
}

const char* state_word(KegState state) {
    return state == KegState::Dirty ? "dirty" : "clean";
}

// "Half-barrels", or "Cans, 16.0 fl oz": the package and, for cans, their size.
std::string package_text(PackageType type, double unit_volume_l, const calc::UnitPreferences& units) {
    std::string text(domain::name(type));
    if (!domain::is_keg(type)) text += ", " + calc::format(unit_volume_l, fill_volume_unit(units));
    return text;
}

} // namespace

PackagingViews::PackagingViews(app::PackagedGoodsService& goods, float ui_scale)
    : goods_(goods), ui_scale_(ui_scale) {}

// --- Packaged goods -----------------------------------------------------------------

void PackagingViews::draw_packaged_stock(const calc::UnitPreferences& units) {
    const std::vector<domain::PackagedStock>& stock = goods_.packaged();
    if (stock.empty()) {
        wrapped_note("No packaged beer yet. Packaging a tank (its Package... button) puts the kegs and cans here.");
        return;
    }

    // Totals of each package, then each beer's.
    const calc::Unit volume = units.unit_for(calc::Measure::Volume);
    std::string totals;
    double total_l = 0.0;
    for (PackageType type : domain::kPackageTypes) {
        int count = 0;
        for (const domain::PackagedStock& entry : stock) {
            if (entry.type == type) count += entry.count;
        }
        if (count == 0) continue;
        if (!totals.empty()) totals += " · ";
        totals += domain::count_text(type, count);
    }
    for (const domain::PackagedStock& entry : stock) total_l += entry.volume_l();
    ImGui::TextUnformatted(totals.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("%s in all", calc::format(total_l, volume).c_str());
    ImGui::Spacing();

    // Each beer in each package, whatever batches it came from; clicking one
    // shows its batches below.
    const std::vector<domain::PackagedProduct> products = domain::combine_by_beer(stock);
    const auto is_selected = [this](const domain::PackagedProduct& product) {
        return selected_ && selected_->beer == product.beer && selected_->type == product.type &&
               selected_->unit_volume_l == product.unit_volume_l;
    };
    const auto selected = std::find_if(products.begin(), products.end(), is_selected);
    const float list_height = selected != products.end() ? ImGui::GetContentRegionAvail().y * 0.5f : 0.0f;
    if (ImGui::BeginChild("##products", ImVec2(0.0f, list_height)) &&
        ImGui::BeginTable("##packaged", 5, kListFlags | ImGuiTableFlags_ScrollY)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Beer", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Package");
        ImGui::TableSetupColumn("Count");
        ImGui::TableSetupColumn("Volume");
        ImGui::TableSetupColumn("Batches");
        ImGui::TableHeadersRow();
        for (std::size_t i = 0; i < products.size(); ++i) {
            const domain::PackagedProduct& product = products[i];
            ImGui::PushID(static_cast<int>(i));
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            if (ImGui::Selectable(product.beer.c_str(), is_selected(product), ImGuiSelectableFlags_SpanAllColumns)) {
                selected_ = Selection{product.beer, product.type, product.unit_volume_l};
            }
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(package_text(product.type, product.unit_volume_l, units).c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%d", product.count);
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", calc::format(product.volume_l(), volume).c_str());
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%zu", product.batches.size());
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();
    if (selected == products.end()) return;

    // The selected product's batches.
    ImGui::Separator();
    ImGui::Text("%s, %s", selected->beer.c_str(), package_text(selected->type, selected->unit_volume_l, units).c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("%s from %zu %s", domain::count_text(selected->type, selected->count).c_str(),
                        selected->batches.size(), selected->batches.size() == 1 ? "batch" : "batches");
    if (!ImGui::BeginTable("##batches", 4, kListFlags | ImGuiTableFlags_ScrollY)) return;
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Batch", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Count");
    ImGui::TableSetupColumn("Volume");
    ImGui::TableSetupColumn("Packaged");
    ImGui::TableHeadersRow();
    for (const domain::PackagedStock& entry : selected->batches) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(domain::batch_label(entry.batch_code, entry.beer).c_str());
        ImGui::TableNextColumn();
        ImGui::Text("%d", entry.count);
        ImGui::TableNextColumn();
        ImGui::TextDisabled("%s", calc::format(entry.volume_l(), volume).c_str());
        ImGui::TableNextColumn();
        ImGui::TextDisabled("%s", date_text(domain::local_date(entry.packaged_at)).c_str());
    }
    ImGui::EndTable();
}

void PackagingViews::draw_packaging_log(const calc::UnitPreferences& units) {
    if (packaging_revision_ != goods_.revision()) {
        packaging_log_ = goods_.packaged_history(300);
        packaging_revision_ = goods_.revision();
    }
    if (packaging_log_.empty()) {
        wrapped_note("Nothing has been packaged yet.");
        return;
    }
    if (!ImGui::BeginTable("##packaging_log", 6, kListFlags | ImGuiTableFlags_ScrollY)) return;
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("When");
    ImGui::TableSetupColumn("Change");
    ImGui::TableSetupColumn("Beer");
    ImGui::TableSetupColumn("What");
    ImGui::TableSetupColumn("Note", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Who");
    ImGui::TableHeadersRow();
    for (const data::PackagedMovementRow& row : packaging_log_) {
        const domain::PackagedMovement& movement = row.movement;
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextDisabled("%s", time_text(movement.at).c_str());
        ImGui::TableNextColumn();
        std::string change = (movement.quantity > 0 ? "+" : "-") +
                             domain::count_text(movement.type, std::abs(movement.quantity));
        if (!domain::is_keg(movement.type)) {
            change += " (" + calc::format(movement.unit_volume_l, fill_volume_unit(units)) + ")";
        }
        ImGui::TextColored(movement.quantity > 0 ? kGoodColor : ImGui::GetStyleColorVec4(ImGuiCol_Text), "%s",
                           change.c_str());
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(domain::batch_label(row.batch_code, row.beer).c_str());
        ImGui::TableNextColumn();
        std::string what(domain::name(movement.reason));
        if (!row.vessel_name.empty()) what += " from " + row.vessel_name;
        ImGui::TextUnformatted(what.c_str());
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(movement.note.c_str());
        ImGui::TableNextColumn();
        ImGui::TextDisabled("%s", movement.user.c_str());
    }
    ImGui::EndTable();
}

// --- The keg cleaner ----------------------------------------------------------------

void PackagingViews::draw_counts(KegState state) {
    if (!ImGui::BeginTable("##kegs", 2, kListFlags)) return;
    ImGui::TableSetupColumn("Keg", ImGuiTableColumnFlags_WidthFixed, 160.0f * ui_scale_);
    ImGui::TableSetupColumn(state == KegState::Dirty ? "Dirty" : "Clean", ImGuiTableColumnFlags_WidthFixed,
                            80.0f * ui_scale_);
    ImGui::TableHeadersRow();
    for (PackageType type : domain::kKegTypes) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(domain::name(type).data());
        ImGui::TableNextColumn();
        ImGui::Text("%d", count_in(goods_.kegs(type), state));
    }
    ImGui::EndTable();
}

void PackagingViews::draw_dirty_kegs() {
    int dirty = 0;
    for (PackageType type : domain::kKegTypes) dirty += goods_.kegs(type).dirty;

    if (ImGui::Button("Clean kegs...")) clean_kegs();
    ImGui::SameLine();
    ImGui::BeginDisabled(dirty == 0);
    if (ImGui::Button("Clean all")) clean_all();
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("Cleans all %d dirty kegs now", dirty);
    ImGui::SameLine();
    if (ImGui::Button("Add kegs...")) add_kegs();
    ImGui::SameLine();
    if (ImGui::Button("Remove kegs...")) remove_kegs(KegState::Dirty);
    ImGui::Spacing();
    draw_counts(KegState::Dirty);
    ImGui::Spacing();
    wrapped_note("Empty kegs wait here until they're cleaned. Add kegs as they come in: new kegs, and kegs back "
                 "from customers.");
}

void PackagingViews::draw_clean_kegs() {
    if (ImGui::Button("Remove kegs...")) remove_kegs(KegState::Clean);
    ImGui::Spacing();
    draw_counts(KegState::Clean);
    ImGui::Spacing();
    wrapped_note("Packaging into kegs takes clean kegs from here. If more are filled than are clean here, the "
                 "rest come from the dirty kegs, as cleaned without it being logged.");
}

void PackagingViews::draw_keg_history() {
    if (history_revision_ != goods_.revision()) {
        history_ = goods_.keg_history(300);
        history_revision_ = goods_.revision();
    }
    if (history_.empty()) {
        wrapped_note("No kegs yet. Add them on the Dirty kegs tab.");
        return;
    }
    if (!ImGui::BeginTable("##keg_history", 5, kListFlags | ImGuiTableFlags_ScrollY)) return;
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("When");
    ImGui::TableSetupColumn("Change");
    ImGui::TableSetupColumn("What");
    ImGui::TableSetupColumn("Note", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Who");
    ImGui::TableHeadersRow();
    for (const data::KegMovementRow& row : history_) {
        const domain::KegMovement& movement = row.movement;
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextDisabled("%s", time_text(movement.at).c_str());
        ImGui::TableNextColumn();
        // "+6 clean sixtels"
        const std::string kegs = domain::count_text(movement.type, std::abs(movement.quantity));
        const std::size_t space = kegs.find(' ');
        const std::string change = (movement.quantity > 0 ? "+" : "-") + kegs.substr(0, space + 1) +
                                   state_word(movement.state) + kegs.substr(space);
        ImGui::TextColored(movement.quantity > 0 ? kGoodColor : ImGui::GetStyleColorVec4(ImGuiCol_Text), "%s",
                           change.c_str());
        ImGui::TableNextColumn();
        std::string what(domain::name(movement.reason));
        if (!row.batch_code.empty()) what += " with " + domain::batch_label(row.batch_code, "");
        ImGui::TextUnformatted(what.c_str());
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(movement.note.c_str());
        ImGui::TableNextColumn();
        ImGui::TextDisabled("%s", movement.user.c_str());
    }
    ImGui::EndTable();
}

// --- Actions ------------------------------------------------------------------------

void PackagingViews::clean_kegs() {
    counts_ = {};
    at_ = domain::now();
    note_.clear();
    open(Kind::Clean);
}

void PackagingViews::clean_all() {
    goods_.clean_all(domain::now());
}

void PackagingViews::add_kegs() {
    counts_ = {};
    at_ = domain::now();
    note_.clear();
    open(Kind::Add);
}

void PackagingViews::remove_kegs(KegState state) {
    counts_ = {};
    state_ = state;
    at_ = domain::now();
    note_.clear();
    open(Kind::Remove);
}

// --- Dialogs ------------------------------------------------------------------------

void PackagingViews::open(Kind kind) {
    kind_ = kind;
    open_requested_ = true;
    error_.clear();
    center_ = ImGui::GetWindowViewport()->GetCenter();
}

void PackagingViews::close() {
    ImGui::CloseCurrentPopup();
    kind_ = Kind::None;
    error_.clear();
}

void PackagingViews::draw() {
    if (open_requested_) {
        ImGui::OpenPopup(kPopupId);
        open_requested_ = false;
    }
    if (kind_ == Kind::None) return;

    const char* title = "";
    switch (kind_) {
    case Kind::Clean:  title = "Clean kegs"; break;
    case Kind::Add:    title = "Add kegs"; break;
    case Kind::Remove: title = "Remove kegs"; break;
    case Kind::None:   break;
    }
    ImGui::SetNextWindowPos(center_, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    bool keep_open = true;
    if (!ImGui::BeginPopupModal((std::string(title) + kPopupId).c_str(), &keep_open,
                                ImGuiWindowFlags_AlwaysAutoResize)) {
        kind_ = Kind::None;
        return;
    }
    switch (kind_) {
    case Kind::Clean:  draw_clean(); break;
    case Kind::Add:    draw_add(); break;
    case Kind::Remove: draw_remove(); break;
    case Kind::None:   break;
    }
    ImGui::EndPopup();
}

void PackagingViews::keg_fields(std::optional<KegState> limit) {
    for (std::size_t i = 0; i < domain::kKegTypes.size(); ++i) {
        const PackageType type = domain::kKegTypes[i];
        table_row(domain::name(type).data());
        ImGui::PushID(static_cast<int>(i));
        ImGui::SetNextItemWidth(80.0f * ui_scale_);
        if (i == 0 && ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        // No +/- buttons, so Tab goes straight to the next size.
        ImGui::InputInt("##count", &counts_[i], 0, 0, ImGuiInputTextFlags_AutoSelectAll);
        const int most = limit ? count_in(goods_.kegs(type), *limit) : 9999;
        counts_[i] = std::clamp(counts_[i], 0, std::max(most, 0));
        if (limit) {
            ImGui::SameLine();
            ImGui::TextDisabled("of %d %s", most, state_word(*limit));
        }
        ImGui::PopID();
    }
}

bool PackagingViews::buttons(const char* confirm, bool enabled) {
    if (!error_.empty()) ImGui::TextColored(kErrorColor, "%s", error_.c_str());
    ImGui::Spacing();
    ImGui::BeginDisabled(!enabled);
    bool pressed = ImGui::Button(confirm);
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) close();
    // Enter saves; a field being typed in has already taken its value.
    const bool enter = ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false);
    if (enabled && enter && !ImGui::IsAnyItemActive()) pressed = true;
    if (enabled) {
        ImGui::SameLine();
        ImGui::TextDisabled("Enter saves");
    }
    return pressed;
}

namespace {

// The dialog's counts as the service takes them, leaving out sizes with none.
std::vector<domain::KegCount> keg_counts(const std::array<int, domain::kKegTypes.size()>& counts) {
    std::vector<domain::KegCount> kegs;
    for (std::size_t i = 0; i < counts.size(); ++i) {
        if (counts[i] > 0) kegs.push_back({domain::kKegTypes[i], counts[i]});
    }
    return kegs;
}

} // namespace

void PackagingViews::draw_clean() {
    ImGui::TextUnformatted("How many kegs were cleaned?");
    if (ImGui::BeginTable("##clean", 2, ImGuiTableFlags_SizingFixedFit)) {
        keg_fields(KegState::Dirty);
        table_row("When");
        time_picker("##at", at_);
        ImGui::EndTable();
    }
    const std::vector<domain::KegCount> kegs = keg_counts(counts_);
    if (!buttons("Save", !kegs.empty())) return;
    try {
        goods_.clean_kegs(kegs, at_);
        close();
    } catch (const std::exception& e) {
        error_ = e.what();
    }
}

void PackagingViews::draw_add() {
    ImGui::TextUnformatted("Kegs come in dirty: new kegs, or kegs back from customers.");
    if (ImGui::BeginTable("##add", 2, ImGuiTableFlags_SizingFixedFit)) {
        keg_fields(std::nullopt);
        table_row("When");
        time_picker("##at", at_);
        table_row("Note");
        ImGui::SetNextItemWidth(260.0f * ui_scale_);
        ImGui::InputTextWithHint("##note", "Where they came from", &note_);
        ImGui::EndTable();
    }
    const std::vector<domain::KegCount> kegs = keg_counts(counts_);
    if (!buttons("Add", !kegs.empty())) return;
    try {
        goods_.add_kegs(kegs, at_, note_);
        close();
    } catch (const std::exception& e) {
        error_ = e.what();
    }
}

void PackagingViews::draw_remove() {
    ImGui::Text("Kegs lost, damaged or retired, from the %s kegs.", state_word(state_));
    if (ImGui::BeginTable("##remove", 2, ImGuiTableFlags_SizingFixedFit)) {
        keg_fields(state_);
        table_row("When");
        time_picker("##at", at_);
        table_row("Note");
        ImGui::SetNextItemWidth(260.0f * ui_scale_);
        ImGui::InputTextWithHint("##note", "Why", &note_);
        ImGui::EndTable();
    }
    const std::vector<domain::KegCount> kegs = keg_counts(counts_);
    if (!buttons("Remove", !kegs.empty())) return;
    try {
        goods_.remove_kegs(kegs, state_, at_, note_);
        close();
    } catch (const std::exception& e) {
        error_ = e.what();
    }
}

} // namespace ninkasi::ui
