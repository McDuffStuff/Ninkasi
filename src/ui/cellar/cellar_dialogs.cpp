#include "ui/cellar/cellar_dialogs.h"

#include "calc/common.h"
#include "calc/gravity.h"
#include "ui/cellar/batch_views.h"
#include "ui/date_widgets.h"
#include "ui/display.h"
#include "ui/inventory/inventory_views.h"
#include "ui/widgets.h"

#include <imgui_stdlib.h>

#include <algorithm>
#include <chrono>
#include <exception>

namespace ninkasi::ui {

namespace {

using calc::Measure;
using domain::ElementType;
using domain::kEmptyVolumeL;

// One popup ID for every dialog: only one is open at a time.
constexpr const char* kPopupId = "###cellar_dialog";

constexpr ImGuiTableFlags kListFlags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                                       ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_PadOuterX;

bool is_blank(const std::string& text) {
    return text.find_first_not_of(" \t\r\n") == std::string::npos;
}

// "Fermenter 2" or "Fermenter 2, Brite tank 1": where a batch is.
std::string where_text(const app::BatchService& batches, const app::FloorService& floor, std::int64_t batch_id) {
    std::string text;
    for (const auto& [vessel_id, volume_l] : batches.locations(batch_id)) {
        if (const domain::Element* vessel = floor.find(vessel_id)) {
            if (!text.empty()) text += ", ";
            text += vessel->name;
        }
    }
    return text;
}

} // namespace

CellarDialogs::CellarDialogs(app::BatchService& batches, const app::FloorService& floor, app::RecipeService& recipes,
                             const app::InventoryService& inventory, const calc::UnitPreferences& units,
                             const domain::CalcSettings& calc_settings, float ui_scale)
    : batches_(batches),
      floor_(floor),
      recipes_(recipes),
      inventory_(inventory),
      units_(units),
      calc_settings_(calc_settings),
      ui_scale_(ui_scale) {}

// --- Opening a dialog -----------------------------------------------------------

void CellarDialogs::log_brew(std::int64_t brewhouse_id) {
    element_id_ = brewhouse_id;
    recipe_list_ = recipes_.list();
    recipe_id_ = 0;
    recipe_.reset();
    code_ = batches_.suggest_code();
    add_to_batch_ = false;
    batch_id_ = 0;
    at_ = domain::now();
    with_knock_out_ = true;
    knocked_out_at_ = at_;
    knock_out_time_set_ = false;
    prepared_for_.clear();
    text_.clear();
    open(Kind::LogBrew);
}

void CellarDialogs::knock_out(std::int64_t brew_id) {
    const auto [batch, brew] = batches_.find_brew(brew_id);
    if (!brew || !brew->in_progress()) return;
    brew_id_ = brew_id;
    batch_id_ = batch->id;
    element_id_ = brew->brewhouse_id.value_or(0);
    at_ = brew->started_at;
    // Usually logged after the fact: suggest later on brew day, not now.
    knocked_out_at_ = std::min(domain::now(), brew->started_at + std::chrono::hours(8));
    knock_out_time_set_ = true;
    text_.clear();
    prepare_knock_out(batch->recipe, batch->id);
    open(Kind::KnockOut);
}

void CellarDialogs::delete_brew(std::int64_t brew_id) {
    const auto [batch, brew] = batches_.find_brew(brew_id);
    if (!brew || !brew->in_progress()) return;
    brew_id_ = brew_id;
    batch_id_ = batch->id;
    element_id_ = brew->brewhouse_id.value_or(0);
    open(Kind::DeleteBrew);
}

void CellarDialogs::transfer(std::int64_t vessel_id) {
    const std::optional<app::VesselContents> held = batches_.contents(vessel_id);
    if (!held) return;
    element_id_ = vessel_id;
    batch_id_ = held->batch_id;
    volume_l_ = held->volume_l;
    empties_ = true;
    text_.clear();
    at_ = domain::now();

    // Suggest the first vessel that can take it: brite tanks first when
    // moving out of a fermenter.
    const domain::Element* source = floor_.find(vessel_id);
    const bool from_fermenter = source && source->type == ElementType::Fermenter;
    target_id_ = 0;
    for (ElementType type : {from_fermenter ? ElementType::BriteTank : ElementType::Fermenter,
                             from_fermenter ? ElementType::Fermenter : ElementType::BriteTank}) {
        for (const domain::Element& element : floor_.elements()) {
            if (target_id_ == 0 && element.type == type &&
                batches_.can_transfer(vessel_id, element.id, volume_l_).allowed) {
                target_id_ = element.id;
            }
        }
    }
    open(Kind::Transfer);
}

void CellarDialogs::package(std::int64_t vessel_id) {
    const std::optional<app::VesselContents> held = batches_.contents(vessel_id);
    if (!held) return;
    element_id_ = vessel_id;
    batch_id_ = held->batch_id;
    volume_l_ = held->volume_l;
    empties_ = true;
    text_.clear();
    at_ = domain::now();
    open(Kind::Package);
}

void CellarDialogs::dump(std::int64_t vessel_id) {
    const std::optional<app::VesselContents> held = batches_.contents(vessel_id);
    if (!held) return;
    element_id_ = vessel_id;
    batch_id_ = held->batch_id;
    text_.clear();
    at_ = domain::now();
    open(Kind::Dump);
}

void CellarDialogs::correct_volume(std::int64_t vessel_id) {
    const std::optional<app::VesselContents> held = batches_.contents(vessel_id);
    if (!held) return;
    element_id_ = vessel_id;
    batch_id_ = held->batch_id;
    volume_l_ = held->volume_l;
    text_.clear();
    at_ = domain::now();
    open(Kind::CorrectVolume);
}

void CellarDialogs::log_reading(std::int64_t batch_id, std::optional<std::int64_t> vessel_id) {
    const domain::Batch* batch = batches_.find_active(batch_id);
    if (!batch) return;
    batch_id_ = batch_id;
    element_id_ = vessel_id.value_or(0);
    if (!vessel_id) {
        if (const auto held = batches_.locations(batch_id); !held.empty()) element_id_ = held.front().first;
    }
    // Start from the latest values: they change slowly between readings.
    if (const domain::Reading* latest = batch->latest_gravity()) {
        gravity_sg_ = *latest->gravity_sg;
    } else if (const std::optional<double> og = batch->og()) {
        gravity_sg_ = *og;
    }
    if (const domain::Reading* latest = batch->latest_temperature()) temperature_c_ = *latest->temperature_c;
    has_gravity_ = true;
    has_temperature_ = true;
    has_ph_ = false;
    text_.clear();
    at_ = domain::now();
    open(Kind::Reading);
}

void CellarDialogs::add_note(std::optional<std::int64_t> batch_id, std::optional<std::int64_t> element_id) {
    batch_id_ = batch_id.value_or(0);
    element_id_ = element_id.value_or(0);
    text_.clear();
    at_ = domain::now();
    open(Kind::Note);
}

void CellarDialogs::open(Kind kind) {
    kind_ = kind;
    open_requested_ = true;
    error_.clear();
    // Over the window that asked, which may have been popped out onto another monitor.
    center_ = ImGui::GetWindowViewport()->GetCenter();
}

void CellarDialogs::close() {
    ImGui::CloseCurrentPopup();
    kind_ = Kind::None;
    error_.clear();
}

// --- Drawing ----------------------------------------------------------------

void CellarDialogs::draw() {
    if (open_requested_) {
        ImGui::OpenPopup(kPopupId);
        open_requested_ = false;
    }
    if (kind_ == Kind::None) return;

    const char* title = "";
    switch (kind_) {
    case Kind::LogBrew:       title = "Log a brew"; break;
    case Kind::KnockOut:      title = "Log the knock-out"; break;
    case Kind::DeleteBrew:    title = "Delete the brew"; break;
    case Kind::Transfer:      title = "Transfer"; break;
    case Kind::Package:       title = "Package"; break;
    case Kind::Dump:          title = "Dump"; break;
    case Kind::CorrectVolume: title = "Correct the volume"; break;
    case Kind::Reading:       title = "Log a reading"; break;
    case Kind::Note:          title = "Add a note"; break;
    case Kind::None:          break;
    }

    // Centered, and kept centered as it grows or shrinks (choosing a recipe
    // adds the knock-out fields) so it never runs off the screen.
    ImGui::SetNextWindowPos(center_, recenter_ ? ImGuiCond_Always : ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    recenter_ = false;
    bool keep_open = true;
    if (!ImGui::BeginPopupModal((std::string(title) + kPopupId).c_str(), &keep_open,
                                ImGuiWindowFlags_AlwaysAutoResize)) {
        kind_ = Kind::None;  // closed with the title bar's button
        return;
    }
    const ImVec2 size = ImGui::GetWindowSize();
    if (!ImGui::IsWindowAppearing() && (size.x != shown_size_.x || size.y != shown_size_.y)) {
        center_ = ImGui::GetWindowPos() + shown_size_ * 0.5f;  // where it is now, wherever it was moved
        recenter_ = true;
    }
    shown_size_ = size;
    switch (kind_) {
    case Kind::LogBrew:       draw_log_brew(); break;
    case Kind::KnockOut:      draw_knock_out(); break;
    case Kind::DeleteBrew:    draw_delete_brew(); break;
    case Kind::Transfer:      draw_transfer(); break;
    case Kind::Package:       draw_package(); break;
    case Kind::Dump:          draw_dump(); break;
    case Kind::CorrectVolume: draw_correct_volume(); break;
    case Kind::Reading:       draw_reading(); break;
    case Kind::Note:          draw_note(); break;
    case Kind::None:          break;
    }
    ImGui::EndPopup();
}

void CellarDialogs::draw_log_brew() {
    const domain::Element* brewhouse = floor_.find(element_id_);
    if (!brewhouse) return close();
    const calc::UnitPreferences units = units_for(element_id_);

    // Batches another brew can be added to.
    std::vector<const domain::Batch*> open_batches;
    for (const domain::Batch& batch : batches_.active_batches()) open_batches.push_back(&batch);
    if (open_batches.empty()) add_to_batch_ = false;

    ImGui::TextDisabled("On %s", brewhouse->name.c_str());
    if (ImGui::RadioButton("A new batch", !add_to_batch_)) add_to_batch_ = false;
    ImGui::SameLine();
    ImGui::BeginDisabled(open_batches.empty());
    if (ImGui::RadioButton("Another brew for a batch", add_to_batch_)) add_to_batch_ = true;
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("For filling a fermenter with several brews of one batch");
    ImGui::Spacing();

    const float field = 280.0f * ui_scale_;
    if (ImGui::BeginTable("##brew", 2, ImGuiTableFlags_SizingFixedFit)) {
        if (!add_to_batch_) {
            table_row("Recipe");
            ImGui::SetNextItemWidth(field);
            if (ImGui::BeginCombo("##recipe", recipe_ ? recipe_->name.c_str() : "Choose a recipe")) {
                for (const data::RecipeSummary& recipe : recipe_list_) {
                    ImGui::PushID(static_cast<int>(recipe.id));
                    if (ImGui::Selectable(recipe.name.c_str(), recipe.id == recipe_id_)) {
                        recipe_id_ = recipe.id;
                        recipe_ = recipes_.load(recipe.id);
                    }
                    if (!recipe.style.empty()) {
                        ImGui::SameLine();
                        ImGui::TextDisabled("%s", recipe.style.c_str());
                    }
                    ImGui::PopID();
                }
                ImGui::EndCombo();
            }
            table_row("Batch code");
            ImGui::SetNextItemWidth(field * 0.5f);
            ImGui::InputText("##code", &code_);
        } else {
            const bool listed = std::any_of(open_batches.begin(), open_batches.end(),
                                            [this](const domain::Batch* b) { return b->id == batch_id_; });
            if (!listed) batch_id_ = open_batches.front()->id;
            const domain::Batch* chosen = batches_.find_active(batch_id_);
            table_row("Batch");
            ImGui::SetNextItemWidth(field);
            if (ImGui::BeginCombo("##batch", chosen ? domain::label(*chosen).c_str() : "")) {
                for (const domain::Batch* batch : open_batches) {
                    ImGui::PushID(static_cast<int>(batch->id));
                    if (ImGui::Selectable(domain::label(*batch).c_str(), batch->id == batch_id_)) batch_id_ = batch->id;
                    const std::string where = where_text(batches_, floor_, batch->id);
                    if (!where.empty()) {
                        ImGui::SameLine();
                        ImGui::TextDisabled("in %s", where.c_str());
                    }
                    ImGui::PopID();
                }
                ImGui::EndCombo();
            }
        }
        table_row("Brewed");
        time_picker("##brewed", at_);
        ImGui::EndTable();
    }
    if (!add_to_batch_ && recipe_list_.empty()) note("There are no recipes yet: create one in Recipes > Recipe library.");

    // What's being brewed, and which batch the wort goes to (nullopt: a new one).
    const domain::Recipe* recipe = nullptr;
    std::optional<std::int64_t> filling;
    std::string source;
    if (add_to_batch_) {
        if (const domain::Batch* batch = batches_.find_active(batch_id_)) {
            recipe = &batch->recipe;
            filling = batch->id;
            source = "batch " + std::to_string(batch->id);
        }
    } else if (recipe_) {
        recipe = &*recipe_;
        source = "recipe " + std::to_string(recipe_id_);
    }
    if (recipe && source != prepared_for_) {
        prepare_knock_out(*recipe, filling);
        prepared_for_ = source;
    }
    // Until it's been set, the knock-out time follows the brew's.
    if (!knock_out_time_set_) knocked_out_at_ = at_;

    ImGui::SeparatorText("Knock-out");
    ImGui::Checkbox("Log the knock-out now", &with_knock_out_);
    bool knock_out_ready = true;
    if (!with_knock_out_) {
        note("The brew will wait on the brewhouse for its knock-out to be logged.");
    } else if (recipe) {
        knock_out_ready = draw_knock_out_fields(*recipe, filling, units);
    } else {
        note(add_to_batch_ ? "Choose a batch." : "Choose a recipe.");
        knock_out_ready = false;
    }

    const bool ready = recipe != nullptr && knock_out_ready;
    if (!buttons(with_knock_out_ ? "Save brew" : "Save, knock out later", ready)) return;

    app::StartBrew start{.brewhouse_id = element_id_, .at = at_};
    if (add_to_batch_) {
        start.add_to_batch = batch_id_;
    } else {
        start.code = code_;
        start.recipe = *recipe_;
    }
    std::optional<app::KnockOut> knock_out;
    if (with_knock_out_) {
        knock_out = app::KnockOut{.vessel_id = target_id_,
                                  .at = knocked_out_at_,
                                  .volume_l = volume_l_,
                                  .og = has_og_ ? std::optional<double>(og_) : std::nullopt,
                                  .ingredients = ingredients_,
                                  .notes = text_};
    }
    attempt([&] { batches_.log_brew(start, knock_out); });
}

void CellarDialogs::draw_knock_out() {
    const auto [batch, brew] = batches_.find_brew(brew_id_);
    if (!brew || !brew->in_progress()) return close();
    const calc::UnitPreferences units = units_for(element_id_);

    ImGui::Text("%s", domain::label(*batch).c_str());
    ImGui::TextDisabled("Brewed %s on %s", time_text(brew->started_at).c_str(),
                        floor_.find(element_id_) ? floor_.find(element_id_)->name.c_str() : "a removed brewhouse");
    ImGui::Spacing();
    const bool ready = draw_knock_out_fields(batch->recipe, batch->id, units);
    if (!buttons("Save knock-out", ready)) return;

    const app::KnockOut request{.brew_id = brew_id_,
                                .vessel_id = target_id_,
                                .at = knocked_out_at_,
                                .volume_l = volume_l_,
                                .og = has_og_ ? std::optional<double>(og_) : std::nullopt,
                                .ingredients = ingredients_,
                                .notes = text_};
    attempt([&] { batches_.knock_out(request); });
}

void CellarDialogs::draw_delete_brew() {
    const auto [batch, brew] = batches_.find_brew(brew_id_);
    if (!brew || !brew->in_progress()) return close();

    ImGui::Text("Delete the brew of %s, brewed %s?", domain::label(*batch).c_str(),
                time_text(brew->started_at).c_str());
    const bool only_brew = batch->status == domain::BatchStatus::Brewing && batch->brews.size() == 1;
    note(only_brew ? "Nothing has gone into a tank yet, so the batch is deleted too and its code can be used again."
                   : "The batch keeps its other brews.");
    if (buttons("Delete brew", true, "Keep it", false)) attempt([this] { batches_.delete_brew(brew_id_); });
}

void CellarDialogs::prepare_knock_out(const domain::Recipe& recipe, std::optional<std::int64_t> batch_id) {
    const domain::RecipeStats predicted = domain::calculate(recipe, calc_settings_);
    volume_l_ = recipe.batch_volume_l;
    has_og_ = true;
    og_ = calc::is_valid(predicted.og) ? predicted.og : 1.050;
    ingredients_ = domain::planned_ingredients(recipe);

    // The tank: the one already holding the batch, or the first fermenter
    // (then brite tank) that can take it.
    target_id_ = 0;
    if (batch_id) {
        if (const auto held = batches_.locations(*batch_id); !held.empty()) target_id_ = held.front().first;
    }
    for (ElementType type : {ElementType::Fermenter, ElementType::BriteTank}) {
        for (const domain::Element& element : floor_.elements()) {
            if (target_id_ == 0 && element.type == type && batches_.can_fill(element.id, volume_l_, batch_id).allowed) {
                target_id_ = element.id;
            }
        }
    }
}

bool CellarDialogs::draw_knock_out_fields(const domain::Recipe& recipe, std::optional<std::int64_t> batch_id,
                                          const calc::UnitPreferences& units) {
    const auto check = [&](std::int64_t vessel_id) { return batches_.can_fill(vessel_id, volume_l_, batch_id); };
    if (ImGui::BeginTable("##knock_out", 2, ImGuiTableFlags_SizingFixedFit)) {
        table_row("Into");
        vessel_combo("##into", target_id_, check, units);
        table_row("Volume");
        ImGui::SetNextItemWidth(120.0f * ui_scale_);
        edit_quantity("##volume", volume_l_, units.unit_for(Measure::Volume));
        table_row("OG");
        ImGui::Checkbox("##has_og", &has_og_);
        ImGui::SetItemTooltip("Untick if the OG wasn't measured");
        ImGui::SameLine();
        ImGui::BeginDisabled(!has_og_);
        ImGui::SetNextItemWidth(100.0f * ui_scale_);
        edit_quantity("##og", og_, units.unit_for(Measure::Gravity));
        ImGui::EndDisabled();
        table_row("Knocked out");
        if (time_picker("##knocked_out", knocked_out_at_)) knock_out_time_set_ = true;
        ImGui::EndTable();
    }
    const bool in_order = knocked_out_at_ >= at_;
    if (!in_order) ImGui::TextColored(kWarningColor, "The knock-out is before the brew started.");

    // The ingredients actually used, starting from the recipe's amounts.
    ImGui::Spacing();
    ImGui::TextUnformatted("Ingredients used");
    ImGui::SameLine();
    if (ImGui::SmallButton("Reset to the recipe")) ingredients_ = domain::planned_ingredients(recipe);
    note("Change any amount that differs from what actually went in. These amounts are taken out of inventory.");

    // A fixed size, scrolling past eight rows: the dialog sizes itself to fit.
    const ImGuiStyle& style = ImGui::GetStyle();
    const float row_height = ImGui::GetFrameHeight() + style.CellPadding.y * 2.0f;
    const float rows = static_cast<float>(std::min<std::size_t>(std::max<std::size_t>(ingredients_.size(), 1), 8));
    const ImVec2 table_size(680.0f * ui_scale_, row_height * (rows + 1.0f) + style.CellPadding.y * 2.0f);
    const std::vector<domain::InventoryItem> stocked = inventory_.items();
    if (ImGui::BeginTable("##ingredients", 5, kListFlags | ImGuiTableFlags_ScrollY, table_size)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Ingredient", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Addition");
        ImGui::TableSetupColumn("Recipe");
        ImGui::TableSetupColumn("Used", ImGuiTableColumnFlags_WidthFixed, 150.0f * ui_scale_);
        ImGui::TableSetupColumn("On hand");
        ImGui::TableHeadersRow();
        for (std::size_t i = 0; i < ingredients_.size(); ++i) {
            domain::IngredientUse& use = ingredients_[i];
            ImGui::PushID(static_cast<int>(i));
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(use.name.c_str());
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", use.detail.c_str());
            ImGui::TableNextColumn();
            if (use.amount_kind != domain::AmountKind::Text) {
                ImGui::TextUnformatted(amount_text(use, use.planned, units).c_str());
            }
            ImGui::TableNextColumn();
            switch (use.amount_kind) {
            case domain::AmountKind::Text:
                ImGui::SetNextItemWidth(-FLT_MIN);
                ImGui::InputText("##used", &use.text);
                break;
            case domain::AmountKind::Count:
                ImGui::SetNextItemWidth(80.0f * ui_scale_);
                edit_number("##used", use.actual, 1, "packs");
                break;
            case domain::AmountKind::Mass:
            case domain::AmountKind::Volume:
                ImGui::SetNextItemWidth(90.0f * ui_scale_);
                edit_quantity("##used", use.actual, *amount_unit(use, units));
                break;
            }
            // What inventory holds of it, red if it's not enough.
            ImGui::TableNextColumn();
            if (const domain::InventoryItem* item = domain::find_stock_for(use, stocked)) {
                const double on_hand = inventory_.find(item->id)->on_hand;
                const std::string text = stock_text(*item, on_hand, units);
                if (on_hand < use.actual) {
                    ImGui::TextColored(kErrorColor, "%s", text.c_str());
                    ImGui::SetItemTooltip("Not enough on record. It's taken anyway; the shortfall shows as "
                                          "negative stock until the delivery it came from is logged.");
                } else {
                    ImGui::TextUnformatted(text.c_str());
                }
            } else if (use.amount_kind != domain::AmountKind::Text) {
                ImGui::TextDisabled("not stocked");
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    ImGui::SetNextItemWidth(table_size.x);
    ImGui::InputTextWithHint("##notes", "Notes", &text_);

    return in_order && volume_l_ > 0.0 && check(target_id_).allowed;
}

void CellarDialogs::draw_transfer() {
    const domain::Element* source = floor_.find(element_id_);
    const std::optional<app::VesselContents> held = batches_.contents(element_id_);
    const domain::Batch* batch = held ? batches_.find_active(held->batch_id) : nullptr;
    if (!source || !batch) return close();
    const calc::UnitPreferences units = units_for(element_id_);
    const calc::Unit volume = units.unit_for(Measure::Volume);

    ImGui::Text("%s in %s: %s", domain::label(*batch).c_str(), source->name.c_str(),
                calc::format(held->volume_l, volume).c_str());
    ImGui::Spacing();
    if (ImGui::BeginTable("##transfer", 2, ImGuiTableFlags_SizingFixedFit)) {
        table_row("To");
        vessel_combo("##to", target_id_,
                     [this](std::int64_t id) { return batches_.can_transfer(element_id_, id, volume_l_); }, units);
        table_row("Volume moved");
        ImGui::SetNextItemWidth(120.0f * ui_scale_);
        edit_quantity("##volume", volume_l_, volume);
        table_row("");
        ImGui::Checkbox(("Empties " + source->name).c_str(), &empties_);
        leftover_note(held->volume_l, source->name, units);
        table_row("When");
        time_picker("##at", at_);
        table_row("Note");
        ImGui::SetNextItemWidth(280.0f * ui_scale_);
        ImGui::InputText("##note", &text_);
        ImGui::EndTable();
    }

    const bool ready = volume_l_ > 0.0 && batches_.can_transfer(element_id_, target_id_, volume_l_).allowed &&
                       (empties_ || volume_l_ < held->volume_l - kEmptyVolumeL);
    if (!buttons("Transfer", ready)) return;
    const app::Transfer request{.from_vessel_id = element_id_,
                                .to_vessel_id = target_id_,
                                .at = at_,
                                .volume_l = volume_l_,
                                .empties_source = empties_,
                                .note = text_};
    attempt([&] { batches_.transfer(request); });
}

void CellarDialogs::draw_package() {
    const domain::Element* vessel = floor_.find(element_id_);
    const std::optional<app::VesselContents> held = batches_.contents(element_id_);
    const domain::Batch* batch = held ? batches_.find_active(held->batch_id) : nullptr;
    if (!vessel || !batch) return close();
    const calc::UnitPreferences units = units_for(element_id_);
    const calc::Unit volume = units.unit_for(Measure::Volume);

    ImGui::Text("%s in %s: %s", domain::label(*batch).c_str(), vessel->name.c_str(),
                calc::format(held->volume_l, volume).c_str());
    ImGui::Spacing();
    if (ImGui::BeginTable("##package", 2, ImGuiTableFlags_SizingFixedFit)) {
        table_row("Into");
        for (domain::PackageFormat format : domain::kPackageFormats) {
            if (ImGui::RadioButton(domain::name(format).data(), format_ == format)) format_ = format;
            ImGui::SameLine();
        }
        ImGui::NewLine();
        table_row("Volume packaged");
        ImGui::SetNextItemWidth(120.0f * ui_scale_);
        edit_quantity("##volume", volume_l_, volume);
        table_row("");
        ImGui::Checkbox(("Empties " + vessel->name).c_str(), &empties_);
        leftover_note(held->volume_l, vessel->name, units);
        table_row("When");
        time_picker("##at", at_);
        table_row("Note");
        ImGui::SetNextItemWidth(280.0f * ui_scale_);
        ImGui::InputTextWithHint("##note", "Keg or can counts, lot codes...", &text_);
        ImGui::EndTable();
    }

    const bool ready = volume_l_ > 0.0 && (empties_ || volume_l_ < held->volume_l - kEmptyVolumeL);
    if (!buttons("Package", ready)) return;
    const app::Packaging request{.vessel_id = element_id_,
                                 .format = format_,
                                 .at = at_,
                                 .volume_l = volume_l_,
                                 .empties_vessel = empties_,
                                 .note = text_};
    attempt([&] { batches_.package(request); });
}

void CellarDialogs::draw_dump() {
    const domain::Element* vessel = floor_.find(element_id_);
    const std::optional<app::VesselContents> held = batches_.contents(element_id_);
    const domain::Batch* batch = held ? batches_.find_active(held->batch_id) : nullptr;
    if (!vessel || !batch) return close();
    const calc::UnitPreferences units = units_for(element_id_);

    ImGui::Text("Dump all %s of %s from %s?",
                calc::format(held->volume_l, units.unit_for(Measure::Volume)).c_str(), domain::label(*batch).c_str(),
                vessel->name.c_str());
    note("It is recorded as dumped and the tank as needing cleaning. This can't be undone.");
    ImGui::Spacing();
    if (ImGui::BeginTable("##dump", 2, ImGuiTableFlags_SizingFixedFit)) {
        table_row("Reason");
        ImGui::SetNextItemWidth(280.0f * ui_scale_);
        ImGui::InputTextWithHint("##reason", "Optional", &text_);
        table_row("When");
        time_picker("##at", at_);
        ImGui::EndTable();
    }
    if (buttons("Dump", true, "Cancel", false)) attempt([this] { batches_.dump(element_id_, at_, text_); });
}

void CellarDialogs::draw_correct_volume() {
    const domain::Element* vessel = floor_.find(element_id_);
    const std::optional<app::VesselContents> held = batches_.contents(element_id_);
    const domain::Batch* batch = held ? batches_.find_active(held->batch_id) : nullptr;
    if (!vessel || !batch) return close();
    const calc::UnitPreferences units = units_for(element_id_);
    const calc::Unit volume = units.unit_for(Measure::Volume);

    ImGui::Text("%s in %s", domain::label(*batch).c_str(), vessel->name.c_str());
    note("Enter the volume measured in the tank. The difference from the recorded volume is logged as a correction.");
    ImGui::Spacing();
    if (ImGui::BeginTable("##correct", 2, ImGuiTableFlags_SizingFixedFit)) {
        table_row("Recorded");
        ImGui::TextUnformatted(calc::format(held->volume_l, volume).c_str());
        table_row("Measured");
        ImGui::SetNextItemWidth(120.0f * ui_scale_);
        edit_quantity("##volume", volume_l_, volume);
        table_row("When");
        time_picker("##at", at_);
        table_row("Note");
        ImGui::SetNextItemWidth(280.0f * ui_scale_);
        ImGui::InputText("##note", &text_);
        ImGui::EndTable();
    }
    if (buttons("Save", volume_l_ >= 0.0)) {
        attempt([this] { batches_.correct_volume(element_id_, volume_l_, at_, text_); });
    }
}

void CellarDialogs::draw_reading() {
    const domain::Batch* batch = batches_.find_active(batch_id_);
    if (!batch) return close();
    const domain::Element* vessel = floor_.find(element_id_);
    const calc::UnitPreferences units = units_for(vessel ? std::optional<std::int64_t>(element_id_) : std::nullopt);
    const calc::Unit gravity = units.unit_for(Measure::Gravity);
    const calc::Unit temperature = units.unit_for(Measure::Temperature);
    const float number_width = 100.0f * ui_scale_;

    ImGui::Text("%s%s", domain::label(*batch).c_str(), vessel ? (" in " + vessel->name).c_str() : "");
    ImGui::Spacing();

    // What the instrument read, and the gravity that works out to.
    double corrected = gravity_sg_;
    std::optional<double> raw;
    std::string how;
    const std::optional<double> og = batch->og();
    switch (method_) {
    case domain::ReadingMethod::Hydrometer:
        corrected = calc::hydrometer_corrected_sg(gravity_sg_, sample_temp_c_, calibration_temp_c_);
        raw = gravity_sg_;
        how = "Corrected for the sample's temperature.";
        break;
    case domain::ReadingMethod::Refractometer:
        raw = brix_;
        if (og) {
            corrected = calc::refractometer_fermenting_sg(calc::sg_to_brix(*og) * wcf_, brix_, wcf_);
            how = "Alcohol skews refractometers, so this uses Sean Terrill's correction with the batch's OG.";
        } else {
            corrected = calc::refractometer_wort_sg(brix_, wcf_);
            how = "There's no OG on record, so this is read as unfermented wort.";
        }
        break;
    case domain::ReadingMethod::Digital:
        break;
    }

    if (ImGui::BeginTable("##reading", 2, ImGuiTableFlags_SizingFixedFit)) {
        table_row("Gravity");
        ImGui::Checkbox("##has_gravity", &has_gravity_);
        ImGui::SameLine();
        ImGui::BeginDisabled(!has_gravity_);
        ImGui::SetNextItemWidth(150.0f * ui_scale_);
        enum_combo("##method", method_, domain::kReadingMethods);
        ImGui::EndDisabled();

        if (has_gravity_) {
            switch (method_) {
            case domain::ReadingMethod::Hydrometer:
                table_row("Reading");
                ImGui::SetNextItemWidth(number_width);
                edit_quantity("##raw", gravity_sg_, gravity);
                table_row("Sample at");
                ImGui::SetNextItemWidth(number_width);
                edit_quantity("##sample_temp", sample_temp_c_, temperature);
                table_row("Calibrated at");
                ImGui::SetNextItemWidth(number_width);
                edit_quantity("##calibration", calibration_temp_c_, temperature);
                ImGui::SetItemTooltip("Marked on the hydrometer: usually 60 °F (15.6 °C) or 68 °F (20 °C)");
                break;
            case domain::ReadingMethod::Refractometer:
                table_row("Reading");
                ImGui::SetNextItemWidth(number_width);
                edit_number("##brix", brix_, 1, "°Bx");
                table_row("Correction factor");
                ImGui::SetNextItemWidth(number_width);
                edit_number("##wcf", wcf_, 2);
                ImGui::SetItemTooltip("Your refractometer's wort correction factor (WCF), typically 1.02 to 1.06");
                break;
            case domain::ReadingMethod::Digital:
                table_row("Reading");
                ImGui::SetNextItemWidth(number_width);
                edit_quantity("##raw", gravity_sg_, gravity);
                break;
            }
            if (method_ != domain::ReadingMethod::Digital) {
                table_row("Gravity");
                ImGui::TextUnformatted(calc::format(corrected, gravity).c_str());
                ImGui::SameLine();
                ImGui::TextDisabled("(?)");
                ImGui::SetItemTooltip("%s", how.c_str());
            }
        }

        table_row("Temperature");
        ImGui::Checkbox("##has_temperature", &has_temperature_);
        ImGui::SameLine();
        ImGui::BeginDisabled(!has_temperature_);
        ImGui::SetNextItemWidth(number_width);
        edit_quantity("##temperature", temperature_c_, temperature);
        ImGui::EndDisabled();

        table_row("pH");
        ImGui::Checkbox("##has_ph", &has_ph_);
        ImGui::SameLine();
        ImGui::BeginDisabled(!has_ph_);
        ImGui::SetNextItemWidth(number_width);
        edit_number("##ph", ph_, 2);
        ImGui::EndDisabled();

        table_row("Taken");
        time_picker("##at", at_);
        table_row("Note");
        ImGui::SetNextItemWidth(280.0f * ui_scale_);
        ImGui::InputText("##note", &text_);
        ImGui::EndTable();
    }

    const bool ready = (has_gravity_ && calc::is_valid(corrected)) || has_temperature_ || has_ph_;
    if (!buttons("Save reading", ready)) return;
    domain::Reading reading;
    reading.batch_id = batch_id_;
    if (vessel) reading.vessel_id = element_id_;
    reading.at = at_;
    reading.method = method_;
    if (has_gravity_) {
        reading.gravity_sg = corrected;
        reading.raw_value = raw;
    }
    if (has_temperature_) reading.temperature_c = temperature_c_;
    if (has_ph_) reading.ph = ph_;
    reading.note = text_;
    attempt([&] { batches_.log_reading(reading); });
}

void CellarDialogs::draw_note() {
    const domain::Batch* batch = batch_id_ ? batches_.find_active(batch_id_) : nullptr;
    const domain::Element* element = element_id_ ? floor_.find(element_id_) : nullptr;
    if (batch && element) {
        ImGui::Text("%s in %s", domain::label(*batch).c_str(), element->name.c_str());
    } else if (batch) {
        ImGui::TextUnformatted(domain::label(*batch).c_str());
    } else if (element) {
        ImGui::TextUnformatted(element->name.c_str());
    }
    ImGui::InputTextMultiline("##note", &text_, ImVec2(420.0f, 110.0f) * ui_scale_);
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere(-1);
    ImGui::TextDisabled("When");
    ImGui::SameLine();
    time_picker("##at", at_);

    if (!buttons("Add note", !is_blank(text_))) return;
    const std::optional<std::int64_t> batch_id = batch ? std::optional<std::int64_t>(batch_id_) : std::nullopt;
    const std::optional<std::int64_t> element_id = element ? std::optional<std::int64_t>(element_id_) : std::nullopt;
    attempt([&] { batches_.add_note(batch_id, element_id, text_, at_); });
}

// --- Helpers ----------------------------------------------------------------

calc::UnitPreferences CellarDialogs::units_for(std::optional<std::int64_t> element_id) const {
    const domain::Element* element = element_id ? floor_.find(*element_id) : nullptr;
    return element ? units_.with_overrides(element->unit_overrides) : units_;
}

bool CellarDialogs::attempt(const std::function<void()>& action) {
    try {
        action();
    } catch (const std::exception& e) {
        error_ = e.what();
        return false;
    }
    close();
    return true;
}

bool CellarDialogs::buttons(const char* confirm, bool enabled, const char* dismiss, bool enter_confirms) {
    if (!error_.empty()) {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 440.0f * ui_scale_);
        ImGui::TextColored(kErrorColor, "%s", rendered(error_, units_).c_str());
        ImGui::PopTextWrapPos();
    }
    ImGui::Spacing();
    ImGui::BeginDisabled(!enabled);
    bool pressed = ImGui::Button(confirm);
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button(dismiss) || ImGui::IsKeyPressed(ImGuiKey_Escape)) close();

    // Enter saves, unless it's typing a new line in a notes box. A field
    // being typed in has already taken its value by now.
    const bool enter = ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false);
    if (enter_confirms && enabled && enter && !ImGui::IsAnyItemActive()) pressed = true;
    if (enter_confirms && enabled) {
        ImGui::SameLine();
        ImGui::TextDisabled("Enter saves");
    }
    return pressed;
}

void CellarDialogs::vessel_combo(const char* label, std::int64_t& vessel_id,
                                 const std::function<app::Availability(std::int64_t)>& check,
                                 const calc::UnitPreferences& units) {
    const domain::Element* chosen = floor_.find(vessel_id);
    ImGui::SetNextItemWidth(220.0f * ui_scale_);
    if (ImGui::BeginCombo(label, chosen ? chosen->name.c_str() : "Choose a tank")) {
        for (const domain::Element& element : floor_.elements()) {
            if (!domain::holds_beer(element.type)) continue;
            const app::Availability availability = check(element.id);
            ImGui::PushID(static_cast<int>(element.id));
            ImGui::BeginDisabled(!availability.allowed);
            if (ImGui::Selectable(element.name.c_str(), element.id == vessel_id)) vessel_id = element.id;
            ImGui::EndDisabled();
            if (!availability.message.empty()) {
                ImGui::SameLine();
                ImGui::TextDisabled("%s", rendered(availability.message, units).c_str());
            }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    // Why the chosen tank won't do, or what to watch out for.
    if (chosen) {
        const app::Availability availability = check(vessel_id);
        if (!availability.message.empty()) {
            ImGui::TextColored(availability.allowed ? kWarningColor : kErrorColor, "%s",
                               rendered(availability.message, units).c_str());
        }
    }
}

void CellarDialogs::note(const std::string& text) const {
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 440.0f * ui_scale_);
    ImGui::TextDisabled("%s", text.c_str());
    ImGui::PopTextWrapPos();
}

void CellarDialogs::leftover_note(double held_l, const std::string& vessel_name,
                                  const calc::UnitPreferences& units) const {
    const calc::Unit volume = units.unit_for(Measure::Volume);
    const double left_l = held_l - volume_l_;
    if (empties_) {
        if (left_l > kEmptyVolumeL) {
            note(calc::format(left_l, volume) + " left behind is recorded as a loss.");
        } else if (left_l < -kEmptyVolumeL) {
            note(calc::format(-left_l, volume) + " more than was recorded: the difference is recorded as a gain.");
        }
    } else if (left_l > kEmptyVolumeL) {
        note(calc::format(left_l, volume) + " stays in " + vessel_name + ".");
    } else {
        ImGui::TextColored(kWarningColor, "That's all of it: tick \"Empties %s\".", vessel_name.c_str());
    }
}

} // namespace ninkasi::ui
