#include "ui/insights/insights_window.h"

#include "app/csv.h"
#include "domain/money.h"
#include "ui/charts.h"
#include "ui/date_widgets.h"
#include "ui/display.h"
#include "ui/inventory/inventory_views.h"
#include "ui/sales/sales_widgets.h"
#include "ui/search_combo.h"
#include "ui/widgets.h"

#include <imgui.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <exception>

namespace ninkasi::ui {

namespace {

constexpr ImGuiTableFlags kTableFlags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                                        ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_PadOuterX;

// Each channel keeps its color, whichever are shown.
int channel_slot(domain::Channel channel) {
    return static_cast<int>(channel);
}

bool same_query(const app::InsightsQuery& a, const app::InsightsQuery& b) {
    return a.period == b.period && a.grouping == b.grouping && a.comparison == b.comparison;
}

std::vector<std::string> labels_of(const std::vector<domain::Bucket>& buckets) {
    std::vector<std::string> labels;
    for (const domain::Bucket& bucket : buckets) labels.push_back(bucket.label);
    return labels;
}

std::string percent(double fraction) {
    char text[32];
    std::snprintf(text, sizeof text, std::abs(fraction) < 0.1 ? "%.1f %%" : "%.0f %%", fraction * 100.0);
    return text;
}

// "+12 %", "-5 %", or "--" with nothing to compare with.
std::string change_text(std::optional<double> change) {
    if (!change) return "--";
    char text[32];
    std::snprintf(text, sizeof text, "%+.0f %%", *change * 100.0);
    return text;
}

ImVec4 status_color(domain::RunwayStatus status) {
    switch (status) {
    case domain::RunwayStatus::Out:        return charts::critical_color();
    case domain::RunwayStatus::BrewNow:    return charts::serious_color();
    case domain::RunwayStatus::BrewSoon:   return charts::warning_color();
    case domain::RunwayStatus::Plenty:     return charts::good_color();
    case domain::RunwayStatus::NotSelling: return charts::muted_ink();
    }
    return charts::muted_ink();
}

// A status as a shape and a word, its color only helping.
void status_label(domain::RunwayStatus status) {
    const float size = ImGui::GetTextLineHeight() * 0.55f;
    const ImVec2 at = ImGui::GetCursorScreenPos();
    const ImVec2 center(at.x + size * 0.6f, at.y + ImGui::GetTextLineHeight() * 0.5f);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImU32 color = ImGui::GetColorU32(status_color(status));
    switch (status) {
    case domain::RunwayStatus::Out:
    case domain::RunwayStatus::BrewNow:  // a diamond: act
        draw->AddQuadFilled(ImVec2(center.x, center.y - size * 0.6f), ImVec2(center.x + size * 0.6f, center.y),
                            ImVec2(center.x, center.y + size * 0.6f), ImVec2(center.x - size * 0.6f, center.y), color);
        break;
    case domain::RunwayStatus::BrewSoon:  // a triangle: soon
        draw->AddTriangleFilled(ImVec2(center.x, center.y - size * 0.6f),
                                ImVec2(center.x + size * 0.6f, center.y + size * 0.5f),
                                ImVec2(center.x - size * 0.6f, center.y + size * 0.5f), color);
        break;
    default:  // a circle: fine, or nothing to do
        draw->AddCircleFilled(center, size * 0.5f, color);
        break;
    }
    ImGui::Dummy(ImVec2(size * 1.4f, ImGui::GetTextLineHeight()));
    ImGui::SameLine();
    ImGui::TextUnformatted(domain::name(status).data());
}

void section(const char* title) {
    ImGui::Spacing();
    ImGui::SeparatorText(title);
}

// The chart series of sales by channel: revenue, with the taproom's expected
// revenue as a series of its own beside its actual; or volume, in the
// brewery's unit. Channels with nothing in the period are left out.
std::vector<charts::Series> channel_series(const app::SalesInsights& sales, bool revenue,
                                           const std::function<double(double)>& in_volume_unit) {
    std::vector<charts::Series> series;
    for (const app::ChannelSeries& channel : sales.channels) {
        charts::Series entry{channel.expected ? "Taproom, expected" : std::string(domain::name(channel.channel)),
                             channel.expected ? 6 : channel_slot(channel.channel), {}};
        bool any = false;
        for (std::size_t i = 0; i < sales.buckets.size(); ++i) {
            const double value = revenue ? channel.revenue[i] : in_volume_unit(channel.volume_l[i]);
            entry.values.push_back(value);
            any = any || value != 0.0;
        }
        if (any) series.push_back(std::move(entry));
    }
    return series;
}

// That revenue is partly an estimate: a mark and words, in the warning color.
void estimate_note(const std::string& amount, int days) {
    ImGui::PushStyleColor(ImGuiCol_Text, kWarningColor);
    ImGui::TextWrapped("(!) Estimate: %s of this is the taproom's expected revenue, from kegs sent there on %d %s "
                       "with no point-of-sale import or takings. Import its sales for those days (Sales > Import "
                       "POS sales) for what it actually took.",
                       amount.c_str(), days, days == 1 ? "day" : "days");
    ImGui::PopStyleColor();
}

} // namespace

InsightsWindow::InsightsWindow(app::InsightsService& insights, const app::SalesService& sales,
                               const app::ExpenseService& expenses, const calc::UnitPreferences& units, float ui_scale)
    : insights_(insights), sales_(sales), expenses_(expenses), units_(units), ui_scale_(ui_scale) {
    query_.period = domain::preset_period(domain::kPeriodPresets[static_cast<std::size_t>(preset_)], domain::today());
    query_.grouping = domain::default_grouping(query_.period);
}

void InsightsWindow::show() {
    visible_ = true;
    focus_ = true;
    if (preset_ >= 0) {
        query_.period =
            domain::preset_period(domain::kPeriodPresets[static_cast<std::size_t>(preset_)], domain::today());
    }
    tank_days_ = insights_.default_tank_days();
    rate_weeks_ = insights_.rate_weeks();
    error_.clear();
}

// --- Formats ------------------------------------------------------------------------

std::string InsightsWindow::money(double amount) const {
    return domain::format_money(amount, sales_.currency());
}

std::string InsightsWindow::volume(double liters) const {
    return calc::format(liters, units_.unit_for(calc::Measure::Volume));
}

double InsightsWindow::in_volume_unit(double liters) const {
    return calc::from_canonical(liters, units_.unit_for(calc::Measure::Volume));
}

std::string InsightsWindow::volume_unit() const {
    return std::string(calc::symbol(units_.unit_for(calc::Measure::Volume)));
}

// --- The window ---------------------------------------------------------------------

void InsightsWindow::invalidate_if_stale() {
    if (computed_query_ && same_query(*computed_query_, query_) && computed_revision_ == insights_.revision()) return;
    computed_query_ = query_;
    computed_revision_ = insights_.revision();
    overview_.reset();
    sales_insights_.reset();
    beer_detail_.reset();
    stock_.reset();
    ingredients_.reset();
    production_.reset();
    batch_costs_.reset();
    profit_loss_.reset();
    sales_bucket_.reset();
}

void InsightsWindow::draw() {
    if (!visible_) return;
    try {
        insights_.refresh();
    } catch (const std::exception& e) {
        error_ = e.what();
    }
    invalidate_if_stale();
    ImGui::SetNextWindowSize(ImVec2(1200.0f, 820.0f) * ui_scale_, ImGuiCond_FirstUseEver);
    const bool raise = focus_;
    if (focus_) {
        ImGui::SetNextWindowFocus();
        focus_ = false;
    }
    const bool expanded = ImGui::Begin("Insights", &visible_);
    if (raise) raise_os_window();
    if (!expanded) {
        ImGui::End();
        return;
    }
    draw_controls();
    if (!error_.empty()) ImGui::TextColored(kErrorColor, "%s", error_.c_str());

    const bool admin = insights_.money();
    if (ImGui::BeginTabBar("##insights")) {
        const auto tab = [&](const char* name, void (InsightsWindow::*draw_tab)()) {
            if (!ImGui::BeginTabItem(name)) return;
            if (ImGui::BeginChild("##tab")) {
                try {
                    (this->*draw_tab)();
                } catch (const std::exception& e) {
                    error_ = e.what();
                }
            }
            ImGui::EndChild();
            ImGui::EndTabItem();
        };
        tab("Overview", &InsightsWindow::draw_overview);
        tab("Sales", &InsightsWindow::draw_sales);
        tab("Stock & planning", &InsightsWindow::draw_stock);
        tab("Ingredients", &InsightsWindow::draw_ingredients);
        tab("Production", &InsightsWindow::draw_production);
        if (admin) {
            tab("Batch costs", &InsightsWindow::draw_batch_costs);
            tab("Profit & loss", &InsightsWindow::draw_profit_loss);
        }
        ImGui::EndTabBar();
    }
    ImGui::End();
}

void InsightsWindow::draw_controls() {
    const float s = ui_scale_;
    // The period: a preset ending today, or any days.
    ImGui::SetNextItemWidth(170.0f * s);
    const char* shown = preset_ >= 0 ? domain::name(domain::kPeriodPresets[static_cast<std::size_t>(preset_)]).data()
                                     : "Custom days";
    if (begin_search_combo("##period", shown)) {
        std::vector<std::string> names;
        for (domain::PeriodPreset preset : domain::kPeriodPresets) names.emplace_back(domain::name(preset));
        names.emplace_back("Custom days");
        for (std::size_t i : search_order(names)) {
            const int choice = i < domain::kPeriodPresets.size() ? static_cast<int>(i) : -1;
            if (!search_item(names[i].c_str(), choice == preset_)) continue;
            preset_ = choice;
            if (preset_ >= 0) query_.period = domain::preset_period(domain::kPeriodPresets[i], domain::today());
            if (!grouping_chosen_) query_.grouping = domain::default_grouping(query_.period);
        }
        end_search_combo();
    }
    ImGui::SameLine();
    if (preset_ < 0) {
        bool changed = date_picker("##from", query_.period.from);
        ImGui::SameLine();
        ImGui::TextDisabled("to");
        ImGui::SameLine();
        changed |= date_picker("##to", query_.period.to);
        if (query_.period.to < query_.period.from) std::swap(query_.period.from, query_.period.to);
        if (changed && !grouping_chosen_) query_.grouping = domain::default_grouping(query_.period);
    } else {
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s to %s", date_text(query_.period.from).c_str(), date_text(query_.period.to).c_str());
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(130.0f * s);
    if (enum_combo("##grouping", query_.grouping, domain::kGroupings)) grouping_chosen_ = true;
    ImGui::SameLine();
    ImGui::SetNextItemWidth(190.0f * s);
    enum_combo("##comparison", query_.comparison, domain::kComparisons);
    if (const std::optional<domain::Period> compared = domain::comparison_period(query_.period, query_.comparison)) {
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s to %s", date_text(compared->from).c_str(), date_text(compared->to).c_str());
    }
}

// --- Overview -----------------------------------------------------------------------

void InsightsWindow::draw_overview() {
    if (!overview_) overview_ = insights_.overview(query_);
    if (!sales_insights_) sales_insights_ = insights_.sales(query_);
    const app::Overview& overview = *overview_;
    const float width = 175.0f * ui_scale_;
    const float spacing = ImGui::GetStyle().ItemSpacing.x * 3.0f;
    const int per_row = std::max(1, static_cast<int>((ImGui::GetContentRegionAvail().x + spacing) / (width + spacing)));
    int placed = 0;
    const auto next = [&] {
        if (placed++ % per_row != 0) ImGui::SameLine(0.0f, spacing);
    };
    const auto converted = [&](const std::vector<double>& liters) {
        std::vector<double> values;
        for (double value : liters) values.push_back(in_volume_unit(value));
        return values;
    };

    if (overview.revenue) {
        next();
        charts::stat_tile("revenue", overview.expected.amount > 0.0 ? "Revenue, partly expected" : "Revenue",
                          money(overview.revenue->now), overview.revenue->change(), true, overview.revenue->series,
                          width);
    }
    if (overview.gross_margin) {
        next();
        charts::stat_tile("margin", "Gross margin", percent(overview.gross_margin->now), overview.gross_margin->change(),
                          true, overview.gross_margin->series, width);
    }
    next();
    charts::stat_tile("sold", "Beer sold", volume(overview.sold_l.now), overview.sold_l.change(), true,
                      converted(overview.sold_l.series), width);
    next();
    charts::stat_tile("brewed", "Beer brewed", volume(overview.brewed_l.now), overview.brewed_l.change(), std::nullopt,
                      converted(overview.brewed_l.series), width);
    if (overview.inventory_value) {
        next();
        charts::stat_tile("value", "Stock value today", money(*overview.inventory_value), std::nullopt, std::nullopt, {},
                          width);
    }
    next();
    charts::stat_tile("kegs", "Kegs at customers", std::to_string(overview.kegs_out), std::nullopt, std::nullopt, {},
                      width);
    next();
    charts::stat_tile("brew", "Beers to brew soon", std::to_string(overview.beers_to_brew), std::nullopt, std::nullopt,
                      {}, width);
    ImGui::SetItemTooltip("Beers that run out before a new batch could be ready, or soon will: see Stock & planning");

    if (overview.expected.amount > 0.0) {
        ImGui::Spacing();
        estimate_note(money(overview.expected.amount), overview.expected.days);
    }

    // The period's sales by channel, as on the Sales tab.
    section("Sales");
    const app::SalesInsights& sales = *sales_insights_;
    const bool revenue = insights_.money();
    const std::vector<std::string> labels = labels_of(sales.buckets);
    const std::vector<charts::Series> series =
        channel_series(sales, revenue, [this](double liters) { return in_volume_unit(liters); });
    const charts::Format format = revenue ? charts::Format([this](double v) { return money(v); })
                                          : charts::Format([this](double v) { return number_text(v, 1, " " + volume_unit()); });
    charts::chart_header(revenue ? "Revenue by channel" : "Beer sold by channel", table_overview_,
                         [&] { return charts::series_csv("Period", labels, series); });
    if (series.empty()) {
        wrapped_note("Nothing sold in this period.");
    } else if (table_overview_) {
        charts::series_table("##overview_table", labels, series, format);
    } else {
        charts::stacked_bars("##overview_sales", labels, series, format, 300.0f * ui_scale_);
    }
}

// --- Sales --------------------------------------------------------------------------

void InsightsWindow::draw_sales() {
    if (!sales_insights_) sales_insights_ = insights_.sales(query_);
    const app::SalesInsights& sales = *sales_insights_;
    const bool admin = insights_.money();
    if (admin) {
        if (ImGui::RadioButton("Revenue", sales_by_revenue_)) sales_by_revenue_ = true;
        ImGui::SameLine();
        if (ImGui::RadioButton("Volume", !sales_by_revenue_)) sales_by_revenue_ = false;
        if (sales.unpriced_lines > 0) {
            ImGui::SameLine();
            ImGui::TextColored(kWarningColor, "%d sale %s no price yet (Sales > Sales history prices them)",
                               sales.unpriced_lines, sales.unpriced_lines == 1 ? "line has" : "lines have");
        }
    }
    const bool revenue = admin && sales_by_revenue_;
    const std::vector<std::string> labels = labels_of(sales.buckets);
    const std::vector<charts::Series> series =
        channel_series(sales, revenue, [this](double liters) { return in_volume_unit(liters); });
    const charts::Format format = revenue ? charts::Format([this](double v) { return money(v); })
                                          : charts::Format([this](double v) { return number_text(v, 1, " " + volume_unit()); });
    charts::chart_header(revenue ? "Revenue by channel" : "Beer sold by channel", table_sales_,
                         [&] { return charts::series_csv("Period", labels, series); });
    if (series.empty()) {
        wrapped_note("Nothing sold in this period.");
    } else if (table_sales_) {
        charts::series_table("##sales_table", labels, series, format);
    } else if (const std::optional<std::size_t> clicked =
                   charts::stacked_bars("##sales_chart", labels, series, format, 300.0f * ui_scale_)) {
        sales_bucket_ = clicked;
        const domain::Bucket& bucket = sales.buckets[*clicked];
        bucket_sales_ = insights_.sales_between(bucket.first, bucket.last);
    }
    if (!table_sales_ && !series.empty()) ImGui::TextDisabled("Click a bar for the sales in it.");
    if (revenue && sales.taproom && sales.taproom->counted.amount > 0.0) {
        estimate_note(money(sales.taproom->counted.amount), sales.taproom->counted.days);
    }
    if (sales_bucket_ && *sales_bucket_ < sales.buckets.size()) draw_sales_bucket(*sales_bucket_);
    if (admin && sales.taproom) draw_taproom_check();

    // By beer.
    section("By beer");
    const float s = ui_scale_;
    if (ImGui::BeginTable("##beers", admin ? 8 : 6, kTableFlags)) {
        ImGui::TableSetupColumn("Beer", ImGuiTableColumnFlags_WidthFixed, 200 * s);
        ImGui::TableSetupColumn("Sold", ImGuiTableColumnFlags_WidthFixed, 100 * s);
        ImGui::TableSetupColumn("Packages", ImGuiTableColumnFlags_WidthFixed, 260 * s);
        ImGui::TableSetupColumn("A week", ImGuiTableColumnFlags_WidthFixed, 90 * s);
        ImGui::TableSetupColumn("Change", ImGuiTableColumnFlags_WidthFixed, 70 * s);
        if (admin) ImGui::TableSetupColumn("Revenue", ImGuiTableColumnFlags_WidthFixed, 110 * s);
        ImGui::TableSetupColumn("Ever sold", ImGuiTableColumnFlags_WidthFixed, 100 * s);
        if (admin) ImGui::TableSetupColumn("Ever earned", ImGuiTableColumnFlags_WidthFixed, 110 * s);
        ImGui::TableHeadersRow();
        for (const app::BeerSales& beer : sales.beers) {
            ImGui::PushID(beer.beer.c_str());
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            if (ImGui::Selectable(beer.beer.c_str(), domain::same_beer(selected_beer_, beer.beer),
                                  ImGuiSelectableFlags_SpanAllColumns)) {
                selected_beer_ = beer.beer;
                beer_detail_.reset();
            }
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(volume(beer.volume_l).c_str());
            ImGui::TableNextColumn();
            std::string packages;
            for (const auto& [type, count] : beer.units) {
                if (count == 0) continue;
                if (!packages.empty()) packages += ", ";
                packages += domain::count_text(type, count);
            }
            ImGui::TextUnformatted(packages.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(volume(beer.per_week_l).c_str());
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", change_text(beer.volume_before ? domain::change(beer.volume_l, *beer.volume_before)
                                                                     : std::nullopt)
                                          .c_str());
            if (admin) {
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(money(beer.revenue).c_str());
            }
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", volume(beer.lifetime_l).c_str());
            if (admin) {
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%s", money(beer.lifetime_revenue).c_str());
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (sales.beers.empty()) wrapped_note("No beer sold in this period.");
    if (!selected_beer_.empty()) draw_beer_detail();
}

void InsightsWindow::draw_taproom_check() {
    const app::SalesInsights& sales = *sales_insights_;
    const app::TaproomCheck& check = *sales.taproom;
    const auto any = [](const std::vector<double>& values) {
        return std::any_of(values.begin(), values.end(), [](double value) { return value != 0.0; });
    };
    section("Taproom: expected and actual");
    if (check.actual_days == 0) {
        wrapped_note("None of the taproom's sales are imported for this period yet, so its revenue is all "
                     "expected: what the kegs sent there should bring in, their volume times the yield at the "
                     "price per fl oz. Import its point of sale (Sales > Import POS sales) to compare.");
        return;
    }
    {
        ImGui::TextWrapped("On the %d %s imported from the point of sale, the kegs sent to the taproom should have "
                           "brought in %s; its beer sold for %s (%s).",
                           check.actual_days, check.actual_days == 1 ? "day" : "days",
                           money(check.expected_on_those_days).c_str(), money(check.actual_on_those_days).c_str(),
                           change_text(domain::change(check.actual_on_those_days, check.expected_on_those_days))
                               .c_str());
        ImGui::TextDisabled("A keg counts the day it's sent, and is poured over the days after: over weeks they "
                            "should match.");
    }
    const std::vector<std::string> labels = labels_of(sales.buckets);
    const std::vector<charts::Series> series{{"Expected, from kegs", 6, check.expected},
                                             {"Actual, from the point of sale", 2, check.actual}};
    const charts::Format format = [this](double v) { return money(v); };
    if (!any(check.expected) && !any(check.actual)) return;
    charts::chart_header("The taproom's beer on the days imported, expected and actual", table_taproom_,
                         [&] { return charts::series_csv("Period", labels, series); });
    if (table_taproom_) {
        charts::series_table("##taproom_table", labels, series, format);
    } else {
        charts::grouped_bars("##taproom", labels, series, format, 240.0f * ui_scale_);
    }
}

void InsightsWindow::draw_sales_bucket(std::size_t bucket) {
    const app::SalesInsights& sales = *sales_insights_;
    const bool admin = insights_.money();
    section(("Sales in " + sales.buckets[bucket].label).c_str());
    if (ImGui::SmallButton("Close")) {
        sales_bucket_.reset();
        return;
    }
    if (bucket_sales_.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("No sales were recorded then; the beer was taken out as sold or for the taproom.");
        return;
    }
    const float s = ui_scale_;
    if (!ImGui::BeginTable("##bucket_sales", admin ? 4 : 3, kTableFlags | ImGuiTableFlags_ScrollY,
                           ImVec2(0.0f, 220.0f * s))) {
        return;
    }
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("When", ImGuiTableColumnFlags_WidthFixed, 140 * s);
    ImGui::TableSetupColumn("Sold to", ImGuiTableColumnFlags_WidthFixed, 200 * s);
    ImGui::TableSetupColumn("What", ImGuiTableColumnFlags_WidthStretch);
    if (admin) ImGui::TableSetupColumn("Total", ImGuiTableColumnFlags_WidthFixed, 110 * s);
    ImGui::TableHeadersRow();
    for (const domain::Sale& sale : bucket_sales_) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(time_text(sale.at).c_str());
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(sale.customer.empty() ? domain::name(sale.channel).data() : sale.customer.c_str());
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(sale_text(sale, units_).c_str());
        if (admin) {
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(money(sale.revenue()).c_str());
        }
    }
    ImGui::EndTable();
}

void InsightsWindow::draw_beer_detail() {
    if (!beer_detail_) beer_detail_ = insights_.beer_detail(selected_beer_, query_);
    const app::BeerDetail& detail = *beer_detail_;
    const bool admin = insights_.money();
    section(detail.beer.c_str());
    if (ImGui::SmallButton("Close")) {
        selected_beer_.clear();
        beer_detail_.reset();
        return;
    }

    // Every month it has sold, with a three-month average.
    const std::vector<std::string> months = labels_of(detail.months);
    std::vector<charts::Series> monthly{{"Sold", 0, {}}};
    for (double liters : detail.monthly_l) monthly[0].values.push_back(in_volume_unit(liters));
    const charts::Format volume_format = [this](double v) { return number_text(v, 1, " " + volume_unit()); };
    charts::chart_header("Sold each month, ever", table_beer_, [&] { return charts::series_csv("Month", months, monthly); });
    if (months.empty()) {
        wrapped_note("It hasn't been sold yet.");
    } else if (table_beer_) {
        charts::series_table("##beer_months", months, monthly, volume_format);
    } else {
        charts::line_chart("##beer_line", months, monthly, volume_format, 220.0f * ui_scale_, 3);
    }

    // Who bought it in the period: by revenue for admins, else by volume.
    std::vector<std::string> names;
    std::vector<double> values;
    std::vector<app::BeerDetail::Customer> customers = detail.customers;
    if (admin) {
        std::sort(customers.begin(), customers.end(), [](const auto& a, const auto& b) { return a.revenue > b.revenue; });
    }
    for (const app::BeerDetail::Customer& customer : customers) {
        names.push_back(customer.name);
        values.push_back(admin ? customer.revenue : in_volume_unit(customer.volume_l));
    }
    const charts::Format customer_format =
        admin ? charts::Format([this](double v) { return money(v); }) : volume_format;
    charts::chart_header("Who bought it", table_customers_, [&] {
        std::string csv = app::csv_row({"Customer", admin ? "Revenue" : "Volume"});
        for (std::size_t i = 0; i < names.size(); ++i) csv += app::csv_row({names[i], std::to_string(values[i])});
        return csv;
    });
    if (names.empty()) {
        wrapped_note("No sales of it were recorded in this period.");
    } else if (table_customers_) {
        std::vector<charts::Series> one{{admin ? "Revenue" : "Volume", 0, values}};
        charts::series_table("##customer_table", names, one, customer_format);
    } else {
        charts::ranked_bars("##customers", names, values, customer_format);
    }

    // The batches its beer came from.
    if (!detail.batches.empty()) {
        std::string batches;
        for (const app::BeerDetail::Batch& batch : detail.batches) {
            if (!batches.empty()) batches += ", ";
            batches += "#" + batch.code + " (" + volume(batch.volume_l) + ")";
        }
        ImGui::TextDisabled("From batches %s", batches.c_str());
    }
}

// --- Stock and planning -------------------------------------------------------------

void InsightsWindow::draw_stock() {
    if (!stock_) stock_ = insights_.stock(domain::today());
    const float s = ui_scale_;
    wrapped_note(("How long each beer lasts at the rate it sold over the last " + std::to_string(insights_.rate_weeks()) +
                  " weeks, counting what's in the tanks, and when to brew it so more is ready in time: each beer's "
                  "time in tanks is the middle of its last five batches' (" +
                  std::to_string(insights_.default_tank_days()) + " days until it has some).")
                     .c_str());
    if (ImGui::BeginTable("##runway", 8, kTableFlags)) {
        ImGui::TableSetupColumn("Beer", ImGuiTableColumnFlags_WidthFixed, 200 * s);
        ImGui::TableSetupColumn("Packaged", ImGuiTableColumnFlags_WidthFixed, 100 * s);
        ImGui::TableSetupColumn("In tanks", ImGuiTableColumnFlags_WidthFixed, 100 * s);
        ImGui::TableSetupColumn("Sold a week", ImGuiTableColumnFlags_WidthFixed, 100 * s);
        ImGui::TableSetupColumn("Days left", ImGuiTableColumnFlags_WidthFixed, 80 * s);
        ImGui::TableSetupColumn("Runs out", ImGuiTableColumnFlags_WidthFixed, 110 * s);
        ImGui::TableSetupColumn("Brew by", ImGuiTableColumnFlags_WidthFixed, 110 * s);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 130 * s);
        ImGui::TableHeadersRow();
        for (const app::BeerRunway& beer : *stock_) {
            const domain::Runway& runway = beer.runway;
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(beer.beer.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(volume(beer.packaged_l).c_str());
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", beer.in_tanks_l > 0.0 ? volume(beer.in_tanks_l).c_str() : "");
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(runway.per_week > 0.0 ? volume(runway.per_week).c_str() : "");
            ImGui::TableNextColumn();
            if (runway.days_left) ImGui::Text("%d", *runway.days_left);
            ImGui::TableNextColumn();
            if (runway.runs_out) ImGui::TextUnformatted(date_text(*runway.runs_out).c_str());
            ImGui::TableNextColumn();
            if (runway.brew_by) ImGui::TextUnformatted(date_text(*runway.brew_by).c_str());
            ImGui::SetItemTooltip("%d days in tanks", beer.tank_days);
            ImGui::TableNextColumn();
            status_label(runway.status);
        }
        ImGui::EndTable();
    }
    if (stock_->empty()) wrapped_note("No beer on hand or sold lately.");

    // Admins choose how it's worked out.
    if (!insights_.money()) return;
    section("Planning");
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Days in tanks, until a beer has batches to tell");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80.0f * s);
    ImGui::InputInt("##tank_days", &tank_days_, 0, 0);
    ImGui::SameLine();
    ImGui::TextUnformatted("Sales rate over the last");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(60.0f * s);
    ImGui::InputInt("##rate_weeks", &rate_weeks_, 0, 0);
    ImGui::SameLine();
    ImGui::TextUnformatted("weeks");
    ImGui::SameLine();
    tank_days_ = std::clamp(tank_days_, 1, 365);
    rate_weeks_ = std::clamp(rate_weeks_, 1, 104);
    const bool changed = tank_days_ != insights_.default_tank_days() || rate_weeks_ != insights_.rate_weeks();
    ImGui::BeginDisabled(!changed);
    if (ImGui::Button("Save")) {
        try {
            insights_.set_planning(tank_days_, rate_weeks_);
            error_.clear();
        } catch (const std::exception& e) {
            error_ = e.what();
        }
    }
    ImGui::EndDisabled();
}

// --- Ingredients --------------------------------------------------------------------

void InsightsWindow::draw_ingredients() {
    if (!ingredients_) ingredients_ = insights_.ingredients(query_);
    const app::IngredientInsights& insights = *ingredients_;
    const bool admin = insights_.money();
    const float s = ui_scale_;
    if (insights.stock_value) {
        ImGui::Text("Ingredients and packaging on hand are worth %s", money(*insights.stock_value).c_str());
    }
    wrapped_note(admin ? "Click an item for its use over time and what it cost." : "Click an item for its use over time.");
    if (ImGui::BeginTable("##items", 7, kTableFlags | ImGuiTableFlags_ScrollY, ImVec2(0.0f, 280.0f * s))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Item", ImGuiTableColumnFlags_WidthFixed, 220 * s);
        ImGui::TableSetupColumn("Used", ImGuiTableColumnFlags_WidthFixed, 110 * s);
        ImGui::TableSetupColumn("A week", ImGuiTableColumnFlags_WidthFixed, 110 * s);
        ImGui::TableSetupColumn("On hand", ImGuiTableColumnFlags_WidthFixed, 110 * s);
        ImGui::TableSetupColumn("Reorder at", ImGuiTableColumnFlags_WidthFixed, 110 * s);
        ImGui::TableSetupColumn("Days to reorder", ImGuiTableColumnFlags_WidthFixed, 110 * s);
        ImGui::TableSetupColumn("Days left", ImGuiTableColumnFlags_WidthFixed, 80 * s);
        ImGui::TableHeadersRow();
        for (const app::ItemUse& use : insights.items) {
            ImGui::PushID(static_cast<int>(use.item.id));
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            if (ImGui::Selectable(use.item.name.c_str(), selected_item_ == use.item.id,
                                  ImGuiSelectableFlags_SpanAllColumns)) {
                selected_item_ = use.item.id;
            }
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(stock_text(use.item, use.used, units_).c_str());
            ImGui::TableNextColumn();
            if (use.per_week > 0.0) ImGui::TextUnformatted(stock_text(use.item, use.per_week, units_).c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(stock_text(use.item, use.on_hand, units_).c_str());
            ImGui::TableNextColumn();
            if (use.item.reorder_level) {
                ImGui::TextDisabled("%s", stock_text(use.item, *use.item.reorder_level, units_).c_str());
            }
            ImGui::TableNextColumn();
            if (use.days_to_reorder) {
                if (*use.days_to_reorder <= 14) {
                    ImGui::TextColored(kWarningColor, "%d", *use.days_to_reorder);
                } else {
                    ImGui::Text("%d", *use.days_to_reorder);
                }
            }
            ImGui::TableNextColumn();
            if (use.days_left) ImGui::Text("%d", *use.days_left);
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    // The item chosen: its use per bucket, and what each lot of it cost.
    const auto chosen = std::find_if(insights.items.begin(), insights.items.end(),
                                     [this](const app::ItemUse& use) { return use.item.id == selected_item_; });
    if (chosen != insights.items.end()) {
        const std::optional<calc::Unit> unit = stock_display_unit(chosen->item, units_);
        const auto shown = [&](double canonical) { return unit ? calc::from_canonical(canonical, *unit) : canonical; };
        const std::string unit_name = unit ? std::string(calc::symbol(*unit)) : "each";
        const std::vector<std::string> labels = labels_of(insights.buckets);
        std::vector<charts::Series> use{{"Used", 0, {}}};
        for (double value : chosen->series) use[0].values.push_back(shown(value));
        const charts::Format format = [unit_name](double v) { return number_text(v, 1, " " + unit_name); };
        section(chosen->item.name.c_str());
        charts::chart_header("Used", table_use_, [&] { return charts::series_csv("Period", labels, use); });
        if (table_use_) {
            charts::series_table("##use_table", labels, use, format);
        } else {
            charts::line_chart("##use", labels, use, format, 220.0f * s, 4);
        }
        if (admin) {
            const auto prices = insights.prices.find(chosen->item.id);
            if (prices != insights.prices.end() && !prices->second.empty()) {
                std::vector<std::string> days;
                std::vector<charts::Series> cost{{"Cost per " + unit_name, 1, {}}};
                for (const app::PricePoint& point : prices->second) {
                    days.push_back(date_text(point.day) + (point.supplier.empty() ? "" : ", " + point.supplier));
                    // A cost per kilogram, liter or each, per the unit shown.
                    cost[0].values.push_back(unit ? point.unit_cost * calc::to_canonical(1.0, *unit) : point.unit_cost);
                }
                const charts::Format money_format = [this](double v) { return money(v); };
                charts::chart_header(("What it cost per " + unit_name).c_str(), table_prices_,
                                     [&] { return charts::series_csv("Received", days, cost); });
                if (table_prices_) {
                    charts::series_table("##prices_table", days, cost, money_format);
                } else {
                    charts::line_chart("##prices", days, cost, money_format, 200.0f * s);
                }
            }
        }
    }

    // Who was paid, most first.
    if (!admin) return;
    section("Spent with suppliers");
    std::vector<std::string> names;
    std::vector<double> amounts;
    for (const auto& [supplier, amount] : insights.suppliers) {
        names.push_back(supplier);
        amounts.push_back(amount);
    }
    const charts::Format money_format = [this](double v) { return money(v); };
    charts::chart_header("Spent with each supplier, on deliveries in the period", table_suppliers_, [&] {
        std::string csv = app::csv_row({"Supplier", "Spent"});
        for (std::size_t i = 0; i < names.size(); ++i) csv += app::csv_row({names[i], std::to_string(amounts[i])});
        return csv;
    });
    if (names.empty()) {
        wrapped_note("No deliveries with a cost in this period.");
    } else if (table_suppliers_) {
        std::vector<charts::Series> one{{"Spent", 0, amounts}};
        charts::series_table("##suppliers_table", names, one, money_format);
    } else {
        charts::ranked_bars("##suppliers", names, amounts, money_format);
    }
}

// --- Production ---------------------------------------------------------------------

void InsightsWindow::draw_production() {
    if (!production_) production_ = insights_.production(query_);
    const app::ProductionInsights& production = *production_;
    const float s = ui_scale_;
    const std::vector<std::string> labels = labels_of(production.buckets);
    std::vector<charts::Series> series{{"Brewed", 0, {}}, {"Packaged", 1, {}}};
    for (double liters : production.brewed_l.series) series[0].values.push_back(in_volume_unit(liters));
    for (double liters : production.packaged_l.series) series[1].values.push_back(in_volume_unit(liters));
    const charts::Format format = [this](double v) { return number_text(v, 1, " " + volume_unit()); };
    charts::chart_header("Brewed and packaged", table_production_,
                         [&] { return charts::series_csv("Period", labels, series); });
    ImGui::TextDisabled("%s brewed, %s packaged", volume(production.brewed_l.now).c_str(),
                        volume(production.packaged_l.now).c_str());
    if (table_production_) {
        charts::series_table("##production_table", labels, series, format);
    } else {
        charts::grouped_bars("##production", labels, series, format, 260.0f * s);
    }

    section("Batches finished in the period");
    if (production.batches.empty()) {
        wrapped_note("None finished in this period.");
    } else if (ImGui::BeginTable("##losses", 9, kTableFlags)) {
        ImGui::TableSetupColumn("Batch", ImGuiTableColumnFlags_WidthFixed, 70 * s);
        ImGui::TableSetupColumn("Beer", ImGuiTableColumnFlags_WidthFixed, 170 * s);
        ImGui::TableSetupColumn("Brewed", ImGuiTableColumnFlags_WidthFixed, 100 * s);
        ImGui::TableSetupColumn("Packaged", ImGuiTableColumnFlags_WidthFixed, 100 * s);
        ImGui::TableSetupColumn("Lost", ImGuiTableColumnFlags_WidthFixed, 70 * s);
        ImGui::TableSetupColumn("In transfers", ImGuiTableColumnFlags_WidthFixed, 100 * s);
        ImGui::TableSetupColumn("Dumped", ImGuiTableColumnFlags_WidthFixed, 100 * s);
        ImGui::TableSetupColumn("Measured short", ImGuiTableColumnFlags_WidthFixed, 110 * s);
        ImGui::TableSetupColumn("Days in tanks", ImGuiTableColumnFlags_WidthFixed, 100 * s);
        ImGui::TableHeadersRow();
        for (const app::BatchLosses& batch : production.batches) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("#%s", batch.code.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(batch.beer.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(volume(batch.brewed_l).c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(volume(batch.packaged_l).c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(percent(batch.lost_share()).c_str());
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", volume(batch.transfer_loss_l).c_str());
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", batch.dumped_l > 0.0 ? volume(batch.dumped_l).c_str() : "");
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", batch.adjusted_l != 0.0 ? volume(batch.adjusted_l).c_str() : "");
            ImGui::TableNextColumn();
            if (batch.days_in_tanks) ImGui::Text("%d", *batch.days_in_tanks);
        }
        ImGui::EndTable();
    }

    section("Tank use");
    if (production.tanks.empty()) {
        wrapped_note("No tank has held beer yet.");
    } else {
        std::vector<std::string> tanks;
        std::vector<double> shares;
        std::vector<charts::Series> by_tank;
        for (const app::ProductionInsights::TankUse& tank : production.tanks) {
            tanks.push_back(tank.vessel);
            shares.insert(shares.end(), tank.share.begin(), tank.share.end());
            by_tank.push_back({tank.vessel, 0, tank.share});
        }
        const charts::Format share_format = [](double v) { return percent(v); };
        charts::chart_header("How much of the time each tank held beer", table_tanks_,
                             [&] { return charts::series_csv("Period", labels, by_tank); });
        if (table_tanks_) {
            charts::series_table("##tanks_table", labels, by_tank, share_format);
        } else {
            const float row = ImGui::GetTextLineHeightWithSpacing() * 1.7f;
            charts::heatmap("##tanks", tanks, labels, shares, row * static_cast<float>(tanks.size() + 1));
        }
    }

    if (!production.tank_times.empty()) {
        section("Days from brewing to packaging");
        if (ImGui::BeginTable("##tank_times", 3, kTableFlags)) {
            ImGui::TableSetupColumn("Beer", ImGuiTableColumnFlags_WidthFixed, 200 * s);
            ImGui::TableSetupColumn("Typically", ImGuiTableColumnFlags_WidthFixed, 90 * s);
            ImGui::TableSetupColumn("Batches", ImGuiTableColumnFlags_WidthFixed, 70 * s);
            ImGui::TableHeadersRow();
            for (const app::ProductionInsights::TankTime& time : production.tank_times) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(time.beer.c_str());
                ImGui::TableNextColumn();
                ImGui::Text("%d days", time.median_days);
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%d", time.batches);
            }
            ImGui::EndTable();
        }
    }
}

// --- Batch costs --------------------------------------------------------------------

void InsightsWindow::draw_batch_costs() {
    if (!batch_costs_) batch_costs_ = insights_.batch_costs();
    const float s = ui_scale_;
    wrapped_note("What each batch cost, from the invoices its ingredients and cans came on, and what its beer sold "
                 "for. Beer costs its ingredients over the liters packaged, so losses make the rest cost more.");
    if (batch_costs_->empty()) {
        wrapped_note("No batch has drawn ingredients from inventory yet.");
        return;
    }
    const calc::Unit small = fill_volume_unit(units_);
    if (!ImGui::BeginTable("##batch_costs", 11, kTableFlags | ImGuiTableFlags_ScrollY | ImGuiTableFlags_ScrollX,
                           ImVec2(0.0f, -FLT_MIN))) {
        return;
    }
    ImGui::TableSetupScrollFreeze(2, 1);
    ImGui::TableSetupColumn("Batch", ImGuiTableColumnFlags_WidthFixed, 70 * s);
    ImGui::TableSetupColumn("Beer", ImGuiTableColumnFlags_WidthFixed, 160 * s);
    ImGui::TableSetupColumn("Packaged", ImGuiTableColumnFlags_WidthFixed, 100 * s);
    ImGui::TableSetupColumn("Ingredients", ImGuiTableColumnFlags_WidthFixed, 100 * s);
    ImGui::TableSetupColumn("Cans", ImGuiTableColumnFlags_WidthFixed, 90 * s);
    ImGui::TableSetupColumn(("Per " + volume_unit()).c_str(), ImGuiTableColumnFlags_WidthFixed, 90 * s);
    ImGui::TableSetupColumn("Half-barrel", ImGuiTableColumnFlags_WidthFixed, 90 * s);
    ImGui::TableSetupColumn("Sixtel", ImGuiTableColumnFlags_WidthFixed, 80 * s);
    ImGui::TableSetupColumn("Can", ImGuiTableColumnFlags_WidthFixed, 120 * s);
    ImGui::TableSetupColumn("Sold for", ImGuiTableColumnFlags_WidthFixed, 100 * s);
    ImGui::TableSetupColumn("Margin", ImGuiTableColumnFlags_WidthFixed, 80 * s);
    ImGui::TableHeadersRow();
    const double unit_l = calc::to_canonical(1.0, units_.unit_for(calc::Measure::Volume));
    for (const app::BatchCostRow& row : *batch_costs_) {
        const domain::BatchCost& cost = row.cost;
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::Text("#%s", row.batch.code.c_str());
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(row.batch.beer.c_str());
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(cost.packaged_l > 0.0 ? volume(cost.packaged_l).c_str() : "");
        ImGui::TableNextColumn();
        if (cost.draws == 0) {
            ImGui::TextDisabled("none drawn");
        } else {
            ImGui::TextUnformatted(money(cost.ingredients).c_str());
            if (cost.basis == domain::CostBasis::Estimated) {
                ImGui::SameLine();
                ImGui::TextColored(kWarningColor, "est.");
                ImGui::SetItemTooltip("Partly at an item's latest cost: a delivery had no cost, or wasn't logged");
            } else if (cost.basis == domain::CostBasis::Unknown) {
                ImGui::SameLine();
                ImGui::TextColored(kWarningColor, "part");
                ImGui::SetItemTooltip("Some ingredients have no cost entered on any delivery");
            }
        }
        ImGui::TableNextColumn();
        if (cost.packaging > 0.0) ImGui::TextUnformatted(money(cost.packaging).c_str());
        const std::optional<double> per_l = cost.liquid_per_l();
        ImGui::TableNextColumn();
        if (per_l) ImGui::TextUnformatted(money(*per_l * unit_l).c_str());
        ImGui::TableNextColumn();
        if (per_l) ImGui::TextUnformatted(money(*cost.package_cost(domain::PackageType::HalfBarrel,
                                                                   domain::keg_volume_l(domain::PackageType::HalfBarrel)))
                                              .c_str());
        ImGui::TableNextColumn();
        if (per_l) ImGui::TextUnformatted(money(*cost.package_cost(domain::PackageType::Sixtel,
                                                                   domain::keg_volume_l(domain::PackageType::Sixtel)))
                                              .c_str());
        ImGui::TableNextColumn();
        if (per_l && cost.cans_packaged > 0) {
            ImGui::Text("%s (%s)", money(*cost.package_cost(domain::PackageType::Can, cost.can_volume_l)).c_str(),
                        calc::format(cost.can_volume_l, small).c_str());
        }
        ImGui::TableNextColumn();
        if (row.revenue > 0.0) ImGui::TextUnformatted(money(row.revenue).c_str());
        ImGui::TableNextColumn();
        if (const std::optional<double> margin = row.margin()) ImGui::TextUnformatted(percent(*margin).c_str());
    }
    ImGui::EndTable();
}

// --- Profit and loss ----------------------------------------------------------------

void InsightsWindow::draw_profit_loss() {
    if (!profit_loss_) profit_loss_ = insights_.profit_loss(query_);
    const app::ProfitLoss& statement = *profit_loss_;
    const app::ProfitLossColumn& now = statement.now;
    const app::ProfitLossColumn* before = statement.before ? &*statement.before : nullptr;
    const float s = ui_scale_;
    wrapped_note("A management report from the sales, invoices and expenses recorded, not the brewery's books.");
    if (now.taproom_expected.amount > 0.0) estimate_note(money(now.taproom_expected.amount), now.taproom_expected.days);

    // The statement: each line now, in the comparison period, and the change.
    struct Line {
        std::string name;
        double now = 0.0;
        std::optional<double> before;
        bool total = false;
        bool percent = false;
    };
    std::vector<Line> lines;
    const auto add = [&](std::string name, double value_now, std::optional<double> value_before, bool total = false,
                         bool is_percent = false) {
        lines.push_back({std::move(name), value_now, value_before, total, is_percent});
    };
    const auto then = [&](auto field) -> std::optional<double> {
        if (!before) return std::nullopt;
        return field(*before);
    };
    for (domain::Channel channel : domain::kChannels) {
        const auto found = now.revenue.find(channel);
        const double value_now = found == now.revenue.end() ? 0.0 : found->second;
        const std::optional<double> value_before = then([&](const app::ProfitLossColumn& column) {
            const auto other = column.revenue.find(channel);
            return other == column.revenue.end() ? 0.0 : other->second;
        });
        if (value_now == 0.0 && value_before.value_or(0.0) == 0.0) continue;
        add("Revenue, " + std::string(domain::name(channel)), value_now, value_before);
    }
    if (now.taproom_expected.amount != 0.0 || (before && before->taproom_expected.amount != 0.0)) {
        add("Revenue, Taproom (expected)", now.taproom_expected.amount,
            then([](const auto& c) { return c.taproom_expected.amount; }));
    }
    add("Revenue", now.total_revenue, then([](const auto& c) { return c.total_revenue; }), true);
    add("Cost of goods sold", now.cost_of_goods, then([](const auto& c) { return c.cost_of_goods; }));
    add("Gross profit", now.gross_profit, then([](const auto& c) { return c.gross_profit; }), true);
    add("Gross margin", now.gross_margin().value_or(0.0),
        then([](const auto& c) { return c.gross_margin().value_or(0.0); }), false, true);
    add("Excise tax", now.excise, then([](const auto& c) { return c.excise; }));
    add("Beer lost or given away, at cost", now.beer_lost, then([](const auto& c) { return c.beer_lost; }));
    std::map<std::int64_t, bool> categories;
    for (const auto& [id, amount] : now.expenses) categories[id] = true;
    if (before) {
        for (const auto& [id, amount] : before->expenses) categories[id] = true;
    }
    for (const auto& [id, present] : categories) {
        const domain::ExpenseCategory* category = expenses_.find_category(id);
        const auto found = now.expenses.find(id);
        add(category ? category->name : "Expenses", found == now.expenses.end() ? 0.0 : found->second,
            then([id](const app::ProfitLossColumn& column) {
                const auto other = column.expenses.find(id);
                return other == column.expenses.end() ? 0.0 : other->second;
            }));
    }
    add("Expenses", now.total_expenses, then([](const auto& c) { return c.total_expenses; }), true);
    add("Net operating income", now.net, then([](const auto& c) { return c.net; }), true);

    const auto shown = [&](const Line& line, double value) { return line.percent ? percent(value) : money(value); };
    charts::chart_header("Profit and loss", table_pl_, [&] {
        std::string csv = app::csv_row({"Line", "This period", "Compared with"});
        for (const Line& line : lines) {
            csv += app::csv_row({line.name, std::to_string(line.now), line.before ? std::to_string(*line.before) : ""});
        }
        return csv;
    });
    if (ImGui::BeginTable("##statement", 4, kTableFlags)) {
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 280 * s);
        ImGui::TableSetupColumn("This period", ImGuiTableColumnFlags_WidthFixed, 130 * s);
        ImGui::TableSetupColumn(before ? "Compared with" : "", ImGuiTableColumnFlags_WidthFixed, 130 * s);
        ImGui::TableSetupColumn(before ? "Change" : "", ImGuiTableColumnFlags_WidthFixed, 80 * s);
        ImGui::TableHeadersRow();
        for (const Line& line : lines) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            if (line.total) {
                ImGui::TextUnformatted(line.name.c_str());
            } else {
                ImGui::TextDisabled("    %s", line.name.c_str());
            }
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(shown(line, line.now).c_str());
            ImGui::TableNextColumn();
            if (line.before) ImGui::TextDisabled("%s", shown(line, *line.before).c_str());
            ImGui::TableNextColumn();
            if (line.before && !line.percent) ImGui::TextDisabled("%s", change_text(domain::change(line.now, *line.before)).c_str());
        }
        ImGui::EndTable();
    }

    // Revenue against everything it paid for, over the period.
    section("Revenue and costs");
    const std::vector<std::string> labels = labels_of(statement.buckets);
    const std::vector<charts::Series> series{{"Revenue", 0, statement.revenue_series},
                                             {"Costs and expenses", 1, statement.cost_series}};
    const charts::Format money_format = [this](double v) { return money(v); };
    if (table_pl_) {
        charts::series_table("##pl_table", labels, series, money_format);
    } else {
        charts::line_chart("##pl_chart", labels, series, money_format, 240.0f * s);
    }

    // By beer.
    if (!statement.beers.empty()) {
        section("Margin by beer");
        if (ImGui::BeginTable("##beer_margins", 4, kTableFlags)) {
            ImGui::TableSetupColumn("Beer", ImGuiTableColumnFlags_WidthFixed, 200 * s);
            ImGui::TableSetupColumn("Revenue", ImGuiTableColumnFlags_WidthFixed, 120 * s);
            ImGui::TableSetupColumn("Cost", ImGuiTableColumnFlags_WidthFixed, 120 * s);
            ImGui::TableSetupColumn("Margin", ImGuiTableColumnFlags_WidthFixed, 80 * s);
            ImGui::TableHeadersRow();
            for (const app::ProfitLoss::BeerMargin& beer : statement.beers) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(beer.beer.c_str());
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(money(beer.revenue).c_str());
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(money(beer.cost).c_str());
                ImGui::TableNextColumn();
                if (const std::optional<double> margin = beer.margin()) ImGui::TextUnformatted(percent(*margin).c_str());
            }
            ImGui::EndTable();
        }
    }

    // How sure it is.
    if (statement.unpriced_lines > 0 || statement.uncosted_removals > 0 || statement.estimated_batches > 0 ||
        now.taproom_expected.amount > 0.0) {
        section("How sure it is");
        if (now.taproom_expected.amount > 0.0) {
            ImGui::TextColored(kWarningColor, "%s of the taproom's revenue is expected, on %d %s not yet imported.",
                               money(now.taproom_expected.amount).c_str(), now.taproom_expected.days,
                               now.taproom_expected.days == 1 ? "day" : "days");
        }
        if (statement.unpriced_lines > 0) {
            ImGui::TextColored(kWarningColor, "%d sale %s no price: revenue is short (Sales > Sales history).",
                               statement.unpriced_lines, statement.unpriced_lines == 1 ? "line has" : "lines have");
        }
        if (statement.uncosted_removals > 0) {
            ImGui::TextColored(kWarningColor,
                               "%d %s of beer from batches with no cost: their cost of goods is left out.",
                               statement.uncosted_removals, statement.uncosted_removals == 1 ? "removal" : "removals");
        }
        if (statement.estimated_batches > 0) {
            ImGui::TextColored(kWarningColor,
                               "%d %s sold partly at estimated cost (see Batch costs).", statement.estimated_batches,
                               statement.estimated_batches == 1 ? "batch" : "batches");
        }
    }
}

} // namespace ninkasi::ui
