#include "ui/charts.h"

#include "app/csv.h"
#include "domain/analytics.h"
#include "ui/display.h"
#include "ui/file_dialog.h"
#include "ui/fonts.h"

#include <implot.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <exception>
#include <iterator>

namespace ninkasi::ui::charts {

namespace {

// The categorical palette, in its fixed order: blue, orange, aqua, yellow,
// magenta, green, violet, red. Each hue has a step for each theme.
constexpr ImU32 kLightSlots[] = {0x2a78d6, 0xeb6834, 0x1baf7a, 0xeda100, 0xe87ba4, 0x008300, 0x6250d6, 0xe34948};
constexpr ImU32 kDarkSlots[] = {0x3987e5, 0xd95926, 0x199e70, 0xc98500, 0xd55181, 0x008300, 0x9085e9, 0xe66767};
// The blue ramp, steps 100 to 700.
constexpr ImU32 kBlueRamp[] = {0xcde2fb, 0xb7d3f6, 0x9ec5f4, 0x86b6ef, 0x6da7ec, 0x5598e7, 0x3987e5,
                               0x2a78d6, 0x256abf, 0x1c5cab, 0x184f95, 0x104281, 0x0d366b};

ImVec4 rgb(ImU32 hex, float alpha = 1.0f) {
    return ImVec4(static_cast<float>((hex >> 16) & 0xff) / 255.0f, static_cast<float>((hex >> 8) & 0xff) / 255.0f,
                  static_cast<float>(hex & 0xff) / 255.0f, alpha);
}

// Whether the window background is dark: the theme, as charts see it.
bool dark() {
    const ImVec4 bg = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
    return 0.2126f * bg.x + 0.7152f * bg.y + 0.0722f * bg.z < 0.5f;
}

ImVec4 surface() {
    ImVec4 bg = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
    bg.w = 1.0f;
    return bg;
}

constexpr ImPlotFlags kPlotFlags = ImPlotFlags_NoMenus | ImPlotFlags_NoBoxSelect | ImPlotFlags_NoMouseText;

// The axis formatter ImPlot calls, with a Format as its user data.
int format_tick(double value, char* buffer, int size, void* data) {
    const std::string text = (*static_cast<const Format*>(data))(value);
    return std::snprintf(buffer, static_cast<std::size_t>(size), "%s", text.c_str());
}

// Recessive grid and axes, the chart on the window's own background.
void push_style() {
    const bool is_dark = dark();
    ImPlot::PushStyleColor(ImPlotCol_PlotBg, surface());
    ImPlot::PushStyleColor(ImPlotCol_FrameBg, ImVec4(0, 0, 0, 0));
    ImPlot::PushStyleColor(ImPlotCol_PlotBorder, ImVec4(0, 0, 0, 0));
    ImPlot::PushStyleColor(ImPlotCol_AxisGrid, rgb(is_dark ? 0x2c2c2a : 0xd9d8d1));
    ImPlot::PushStyleColor(ImPlotCol_AxisText, rgb(0x898781));
    ImPlot::PushStyleColor(ImPlotCol_LegendBg, ImVec4(0, 0, 0, 0));
    ImPlot::PushStyleColor(ImPlotCol_LegendBorder, ImVec4(0, 0, 0, 0));
}

void pop_style() {
    ImPlot::PopStyleColor(7);
}

// Category ticks along X (or Y), a label every few so they don't collide.
void setup_categories(ImAxis axis, std::span<const std::string> labels, std::vector<double>& positions,
                      std::vector<const char*>& texts) {
    positions.clear();
    texts.clear();
    const std::size_t stride = axis == ImAxis_X1 ? std::max<std::size_t>(1, (labels.size() + 11) / 12) : 1;
    for (std::size_t i = 0; i < labels.size(); ++i) {
        positions.push_back(static_cast<double>(i));
        texts.push_back(i % stride == 0 ? labels[i].c_str() : "");
    }
    ImPlot::SetupAxisTicks(axis, positions.data(), static_cast<int>(positions.size()), texts.data());
}

// The label under the mouse in a category chart, if any.
std::optional<std::size_t> hovered_index(std::size_t count, bool horizontal = false) {
    if (!ImPlot::IsPlotHovered()) return std::nullopt;
    const ImPlotPoint mouse = ImPlot::GetPlotMousePos();
    const long index = std::lround(horizontal ? mouse.y : mouse.x);
    if (index < 0 || static_cast<std::size_t>(index) >= count) return std::nullopt;
    return static_cast<std::size_t>(index);
}

void swatch(ImVec4 color) {
    const float size = ImGui::GetTextLineHeight() * 0.8f;
    ImGui::ColorButton("##swatch", color, ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoBorder,
                       ImVec2(size, size));
    ImGui::SameLine();
}

// The values of one label, for hovering.
void values_tooltip(const std::string& label, std::span<const Series> series, std::size_t index, const Format& format,
                    bool total) {
    ImGui::BeginTooltip();
    ImGui::TextUnformatted(label.c_str());
    double sum = 0.0;
    for (const Series& entry : series) {
        const double value = index < entry.values.size() ? entry.values[index] : 0.0;
        sum += value;
        ImGui::PushID(&entry);
        if (series.size() > 1) swatch(series_color(entry.slot));
        ImGui::Text("%s  %s", entry.name.c_str(), format(value).c_str());
        ImGui::PopID();
    }
    if (total && series.size() > 1) ImGui::TextDisabled("Total  %s", format(sum).c_str());
    ImGui::EndTooltip();
}

double highest(std::span<const Series> series, bool stacked) {
    double top = 0.0;
    std::size_t count = 0;
    for (const Series& entry : series) count = std::max(count, entry.values.size());
    for (std::size_t i = 0; i < count; ++i) {
        double sum = 0.0;
        for (const Series& entry : series) {
            const double value = i < entry.values.size() ? entry.values[i] : 0.0;
            sum = stacked ? sum + value : std::max(sum, value);
        }
        top = std::max(top, sum);
    }
    return top;
}

double lowest(std::span<const Series> series) {
    double bottom = 0.0;
    for (const Series& entry : series) {
        for (double value : entry.values) bottom = std::min(bottom, value);
    }
    return bottom;
}

} // namespace

ImVec4 series_color(int slot) {
    const std::size_t index = static_cast<std::size_t>(((slot % 8) + 8) % 8);
    return rgb(dark() ? kDarkSlots[index] : kLightSlots[index]);
}

ImVec4 sequential_color(double t) {
    // "None" recedes toward the background and "all" stands out from it:
    // light to dark on the light theme; on the dark one, from the darkest
    // step faded into the background up to a light step.
    const double clamped = std::clamp(t, 0.0, 1.0);
    const double last = static_cast<double>(std::size(kBlueRamp) - 1);
    const double position = dark() ? last - clamped * (last - 4.0) : clamped * last;
    const std::size_t low = static_cast<std::size_t>(std::floor(position));
    const std::size_t high = std::min(low + 1, std::size(kBlueRamp) - 1);
    const float mix = static_cast<float>(position - static_cast<double>(low));
    const ImVec4 a = rgb(kBlueRamp[low]);
    const ImVec4 b = rgb(kBlueRamp[high]);
    ImVec4 color(a.x + (b.x - a.x) * mix, a.y + (b.y - a.y) * mix, a.z + (b.z - a.z) * mix, 1.0f);
    if (dark()) {
        const ImVec4 back = surface();
        const float weight = 0.2f + 0.8f * static_cast<float>(clamped);
        color = ImVec4(back.x + (color.x - back.x) * weight, back.y + (color.y - back.y) * weight,
                       back.z + (color.z - back.z) * weight, 1.0f);
    }
    return color;
}

ImVec4 good_color() { return rgb(0x0ca30c); }
ImVec4 warning_color() { return rgb(0xfab219); }
ImVec4 serious_color() { return rgb(0xec835a); }
ImVec4 critical_color() { return rgb(0xd03b3b); }
ImVec4 muted_ink() { return rgb(0x898781); }

// --- Headers and tables ------------------------------------------------------------

void chart_header(const char* title, bool& as_table, const std::function<std::string()>& csv) {
    ImGui::PushID(title);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(title);
    const float buttons = ImGui::CalcTextSize("Table").x + ImGui::CalcTextSize("Export...").x +
                          ImGui::GetStyle().FramePadding.x * 4.0f + ImGui::GetStyle().ItemSpacing.x;
    ImGui::SameLine(std::max(ImGui::GetContentRegionMax().x - buttons, ImGui::GetCursorPosX()));
    if (ImGui::SmallButton(as_table ? "Chart" : "Table")) as_table = !as_table;
    ImGui::SetItemTooltip(as_table ? "Show it as a chart" : "Show its numbers as a table");
    ImGui::SameLine();
    if (ImGui::SmallButton("Export...")) {
        std::string name = std::string(title) + ".csv";
        std::replace(name.begin(), name.end(), '/', '-');
        if (const auto path = save_file_dialog("Export the chart's numbers", "CSV files", "*.csv", "csv", name.c_str())) {
            try {
                app::write_csv_file(*path, csv());
            } catch (const std::exception&) {
                // Nothing more to do: the dialog chose where, and the write failed.
            }
        }
    }
    ImGui::SetItemTooltip("Save its numbers as a CSV file, for a spreadsheet");
    ImGui::PopID();
}

void series_table(const char* id, std::span<const std::string> labels, std::span<const Series> series,
                  const Format& format) {
    const int columns = static_cast<int>(series.size()) + 1;
    if (!ImGui::BeginTable(id, columns, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                                            ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_ScrollY,
                           ImVec2(0.0f, std::min(ImGui::GetTextLineHeightWithSpacing() * (labels.size() + 2.0f),
                                                 360.0f)))) {
        return;
    }
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("");
    for (const Series& entry : series) ImGui::TableSetupColumn(entry.name.c_str());
    ImGui::TableHeadersRow();
    for (std::size_t i = 0; i < labels.size(); ++i) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(labels[i].c_str());
        for (const Series& entry : series) {
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(format(i < entry.values.size() ? entry.values[i] : 0.0).c_str());
        }
    }
    ImGui::EndTable();
}

std::string series_csv(const char* first_column, std::span<const std::string> labels,
                       std::span<const Series> series) {
    std::vector<std::string> header{first_column};
    for (const Series& entry : series) header.push_back(entry.name);
    std::string text = app::csv_row(header);
    for (std::size_t i = 0; i < labels.size(); ++i) {
        std::vector<std::string> row{labels[i]};
        for (const Series& entry : series) {
            char number[32];
            std::snprintf(number, sizeof number, "%.4f", i < entry.values.size() ? entry.values[i] : 0.0);
            row.emplace_back(number);
        }
        text += app::csv_row(row);
    }
    return text;
}

// --- Bars and lines ---------------------------------------------------------------

std::optional<std::size_t> stacked_bars(const char* id, std::span<const std::string> labels,
                                        std::span<const Series> series, const Format& format, float height) {
    std::optional<std::size_t> clicked;
    push_style();
    if (ImPlot::BeginPlot(id, ImVec2(-1.0f, height), kPlotFlags)) {
        std::vector<double> positions;
        std::vector<const char*> texts;
        ImPlot::SetupAxis(ImAxis_X1, nullptr, ImPlotAxisFlags_NoGridLines);
        ImPlot::SetupAxis(ImAxis_Y1, nullptr);
        setup_categories(ImAxis_X1, labels, positions, texts);
        ImPlot::SetupAxisFormat(ImAxis_Y1, format_tick, const_cast<Format*>(&format));
        ImPlot::SetupAxisLimits(ImAxis_X1, -0.6, static_cast<double>(labels.size()) - 0.4, ImPlotCond_Always);
        ImPlot::SetupAxisLimits(ImAxis_Y1, 0.0, std::max(highest(series, true) * 1.08, 1.0), ImPlotCond_Always);
        ImPlot::SetupLegend(ImPlotLocation_North, ImPlotLegendFlags_Horizontal | ImPlotLegendFlags_Outside);

        // Each series' bar reaches the top of its part of the stack; drawn
        // from the top down, each covers the ones below it but shows its own part.
        std::vector<std::vector<double>> tops(series.size(), std::vector<double>(labels.size(), 0.0));
        for (std::size_t s = 0; s < series.size(); ++s) {
            for (std::size_t i = 0; i < labels.size(); ++i) {
                const double value = i < series[s].values.size() ? series[s].values[i] : 0.0;
                tops[s][i] = (s > 0 ? tops[s - 1][i] : 0.0) + value;
            }
        }
        // The legend in stack order, bottom first; the bars themselves unlisted.
        for (const Series& entry : series) {
            ImPlot::SetNextLineStyle(series_color(entry.slot));
            ImPlot::PlotDummy(entry.name.c_str());
        }
        for (std::size_t s = series.size(); s-- > 0;) {
            ImPlot::SetNextFillStyle(series_color(series[s].slot));
            ImPlot::SetNextLineStyle(surface(), 2.0f);  // a gap between neighbors
            const std::string hidden = "##" + series[s].name;
            ImPlot::PlotBars(hidden.c_str(), positions.data(), tops[s].data(), static_cast<int>(labels.size()), 0.7);
        }
        if (const std::optional<std::size_t> hovered = hovered_index(labels.size())) {
            values_tooltip(labels[*hovered], series, *hovered, format, true);
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) clicked = hovered;
        }
        ImPlot::EndPlot();
    }
    pop_style();
    return clicked;
}

std::optional<std::size_t> grouped_bars(const char* id, std::span<const std::string> labels,
                                        std::span<const Series> series, const Format& format, float height) {
    std::optional<std::size_t> clicked;
    push_style();
    if (ImPlot::BeginPlot(id, ImVec2(-1.0f, height), kPlotFlags)) {
        std::vector<double> positions;
        std::vector<const char*> texts;
        ImPlot::SetupAxis(ImAxis_X1, nullptr, ImPlotAxisFlags_NoGridLines);
        ImPlot::SetupAxis(ImAxis_Y1, nullptr);
        setup_categories(ImAxis_X1, labels, positions, texts);
        ImPlot::SetupAxisFormat(ImAxis_Y1, format_tick, const_cast<Format*>(&format));
        ImPlot::SetupAxisLimits(ImAxis_X1, -0.6, static_cast<double>(labels.size()) - 0.4, ImPlotCond_Always);
        ImPlot::SetupAxisLimits(ImAxis_Y1, 0.0, std::max(highest(series, false) * 1.08, 1.0), ImPlotCond_Always);
        if (series.size() > 1) {
            ImPlot::SetupLegend(ImPlotLocation_North, ImPlotLegendFlags_Horizontal | ImPlotLegendFlags_Outside);
        }
        const double group = 0.75;
        const double width = group / static_cast<double>(std::max<std::size_t>(series.size(), 1));
        for (std::size_t s = 0; s < series.size(); ++s) {
            std::vector<double> xs(positions);
            const double shift = -group / 2.0 + width * (static_cast<double>(s) + 0.5);
            for (double& x : xs) x += shift;
            std::vector<double> ys(labels.size(), 0.0);
            for (std::size_t i = 0; i < ys.size() && i < series[s].values.size(); ++i) ys[i] = series[s].values[i];
            ImPlot::SetNextFillStyle(series_color(series[s].slot));
            ImPlot::SetNextLineStyle(surface(), 2.0f);
            ImPlot::PlotBars(series[s].name.c_str(), xs.data(), ys.data(), static_cast<int>(ys.size()), width);
        }
        if (const std::optional<std::size_t> hovered = hovered_index(labels.size())) {
            values_tooltip(labels[*hovered], series, *hovered, format, false);
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) clicked = hovered;
        }
        ImPlot::EndPlot();
    }
    pop_style();
    return clicked;
}

void line_chart(const char* id, std::span<const std::string> labels, std::span<const Series> series,
                const Format& format, float height, int rolling) {
    push_style();
    if (ImPlot::BeginPlot(id, ImVec2(-1.0f, height), kPlotFlags)) {
        std::vector<double> positions;
        std::vector<const char*> texts;
        ImPlot::SetupAxis(ImAxis_X1, nullptr, ImPlotAxisFlags_NoGridLines);
        ImPlot::SetupAxis(ImAxis_Y1, nullptr);
        setup_categories(ImAxis_X1, labels, positions, texts);
        ImPlot::SetupAxisFormat(ImAxis_Y1, format_tick, const_cast<Format*>(&format));
        ImPlot::SetupAxisLimits(ImAxis_X1, -0.3, std::max(static_cast<double>(labels.size()) - 0.7, 0.3),
                                ImPlotCond_Always);
        const double top = highest(series, false);
        const double bottom = lowest(series);
        ImPlot::SetupAxisLimits(ImAxis_Y1, bottom < 0.0 ? bottom * 1.08 : 0.0, std::max(top * 1.08, 1.0),
                                ImPlotCond_Always);
        const bool averaged = series.size() == 1 && rolling > 1 && labels.size() > static_cast<std::size_t>(rolling);
        if (series.size() > 1 || averaged) {
            ImPlot::SetupLegend(ImPlotLocation_North, ImPlotLegendFlags_Horizontal | ImPlotLegendFlags_Outside);
        }
        for (const Series& entry : series) {
            const ImVec4 color = series_color(entry.slot);
            const int count = static_cast<int>(std::min(entry.values.size(), positions.size()));
            ImPlot::SetNextLineStyle(color, 2.0f);
            ImPlot::SetNextMarkerStyle(ImPlotMarker_Circle, 4.0f, color, 2.0f, surface());
            ImPlot::PlotLine(entry.name.c_str(), positions.data(), entry.values.data(), count);
            if (averaged) {
                const std::vector<double> average = domain::rolling_average(entry.values, rolling);
                ImVec4 faint = color;
                faint.w = 0.55f;
                ImPlot::SetNextLineStyle(faint, 1.5f);
                const std::string name = std::to_string(rolling) + "-point average";
                ImPlot::PlotLine(name.c_str(), positions.data(), average.data(), count);
            }
        }
        if (const std::optional<std::size_t> hovered = hovered_index(labels.size())) {
            // A crosshair on the hovered point, and its values.
            const double x = static_cast<double>(*hovered);
            ImPlot::SetNextLineStyle(muted_ink(), 1.0f);
            ImPlot::PlotInfLines("##hover", &x, 1);
            values_tooltip(labels[*hovered], series, *hovered, format, false);
        }
        ImPlot::EndPlot();
    }
    pop_style();
}

std::optional<std::size_t> ranked_bars(const char* id, std::span<const std::string> labels,
                                       std::span<const double> values, const Format& format, int slot) {
    std::optional<std::size_t> clicked;
    const float row = ImGui::GetTextLineHeightWithSpacing() * 1.6f;
    const float height = row * static_cast<float>(std::max<std::size_t>(labels.size(), 1)) + row * 1.5f;
    push_style();
    if (ImPlot::BeginPlot(id, ImVec2(-1.0f, height), kPlotFlags)) {
        // The first label at the top.
        std::vector<std::string> reversed(labels.rbegin(), labels.rend());
        std::vector<double> bars(values.rbegin(), values.rend());
        std::vector<double> positions;
        std::vector<const char*> texts;
        ImPlot::SetupAxis(ImAxis_X1, nullptr);
        ImPlot::SetupAxis(ImAxis_Y1, nullptr, ImPlotAxisFlags_NoGridLines);
        setup_categories(ImAxis_Y1, reversed, positions, texts);
        ImPlot::SetupAxisFormat(ImAxis_X1, format_tick, const_cast<Format*>(&format));
        const double top = values.empty() ? 1.0 : *std::max_element(values.begin(), values.end());
        ImPlot::SetupAxisLimits(ImAxis_X1, 0.0, std::max(top * 1.08, 1.0), ImPlotCond_Always);
        ImPlot::SetupAxisLimits(ImAxis_Y1, -0.6, static_cast<double>(labels.size()) - 0.4, ImPlotCond_Always);
        ImPlot::SetNextFillStyle(series_color(slot));
        ImPlot::SetNextLineStyle(surface(), 2.0f);
        ImPlot::PlotBars("##values", bars.data(), positions.data(), static_cast<int>(bars.size()), 0.65,
                         ImPlotBarsFlags_Horizontal);
        if (const std::optional<std::size_t> hovered = hovered_index(labels.size(), true)) {
            const std::size_t index = labels.size() - 1 - *hovered;
            ImGui::SetTooltip("%s  %s", labels[index].c_str(), format(values[index]).c_str());
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) clicked = index;
        }
        ImPlot::EndPlot();
    }
    pop_style();
    return clicked;
}

void heatmap(const char* id, std::span<const std::string> rows, std::span<const std::string> columns,
             std::span<const double> values, float height) {
    // Drawn as cells, so each is a solid step of the ramp with a gap between.
    push_style();
    if (ImPlot::BeginPlot(id, ImVec2(-1.0f, height), kPlotFlags)) {
        std::vector<double> x_positions;
        std::vector<const char*> x_texts;
        std::vector<double> y_positions;
        std::vector<const char*> y_texts;
        std::vector<std::string> reversed(rows.rbegin(), rows.rend());
        ImPlot::SetupAxis(ImAxis_X1, nullptr, ImPlotAxisFlags_NoGridLines);
        ImPlot::SetupAxis(ImAxis_Y1, nullptr, ImPlotAxisFlags_NoGridLines);
        setup_categories(ImAxis_X1, columns, x_positions, x_texts);
        setup_categories(ImAxis_Y1, reversed, y_positions, y_texts);
        ImPlot::SetupAxisLimits(ImAxis_X1, -0.5, static_cast<double>(columns.size()) - 0.5, ImPlotCond_Always);
        ImPlot::SetupAxisLimits(ImAxis_Y1, -0.5, static_cast<double>(rows.size()) - 0.5, ImPlotCond_Always);
        ImDrawList* draw = ImPlot::GetPlotDrawList();
        ImPlot::PushPlotClipRect();
        for (std::size_t r = 0; r < rows.size(); ++r) {
            const double y = static_cast<double>(rows.size() - 1 - r);
            for (std::size_t c = 0; c < columns.size(); ++c) {
                const double share = r * columns.size() + c < values.size() ? values[r * columns.size() + c] : 0.0;
                const ImVec2 a = ImPlot::PlotToPixels(static_cast<double>(c) - 0.5, y + 0.5);
                const ImVec2 b = ImPlot::PlotToPixels(static_cast<double>(c) + 0.5, y - 0.5);
                draw->AddRectFilled(ImVec2(a.x + 1.0f, a.y + 1.0f), ImVec2(b.x - 1.0f, b.y - 1.0f),
                                    ImGui::GetColorU32(sequential_color(share)), 2.0f);
            }
        }
        ImPlot::PopPlotClipRect();
        if (ImPlot::IsPlotHovered()) {
            const ImPlotPoint mouse = ImPlot::GetPlotMousePos();
            const long c = std::lround(mouse.x);
            const long y = std::lround(mouse.y);
            if (c >= 0 && static_cast<std::size_t>(c) < columns.size() && y >= 0 &&
                static_cast<std::size_t>(y) < rows.size()) {
                const std::size_t r = rows.size() - 1 - static_cast<std::size_t>(y);
                const std::size_t index = r * columns.size() + static_cast<std::size_t>(c);
                ImGui::SetTooltip("%s, %s: in use %.0f %% of the time", rows[r].c_str(),
                                  columns[static_cast<std::size_t>(c)].c_str(),
                                  index < values.size() ? values[index] * 100.0 : 0.0);
            }
        }
        ImPlot::EndPlot();
    }
    pop_style();
}

// --- Tiles ----------------------------------------------------------------------

void sparkline(const char* id, std::span<const double> values, ImVec2 size, int slot) {
    if (values.size() < 2) {
        ImGui::Dummy(size);
        return;
    }
    ImPlot::PushStyleVar(ImPlotStyleVar_PlotPadding, ImVec2(0, 0));
    ImPlot::PushStyleColor(ImPlotCol_PlotBg, ImVec4(0, 0, 0, 0));
    ImPlot::PushStyleColor(ImPlotCol_FrameBg, ImVec4(0, 0, 0, 0));
    ImPlot::PushStyleColor(ImPlotCol_PlotBorder, ImVec4(0, 0, 0, 0));
    if (ImPlot::BeginPlot(id, size, ImPlotFlags_CanvasOnly | ImPlotFlags_NoInputs)) {
        ImPlot::SetupAxes(nullptr, nullptr, ImPlotAxisFlags_NoDecorations, ImPlotAxisFlags_NoDecorations);
        const double top = *std::max_element(values.begin(), values.end());
        const double bottom = std::min(0.0, *std::min_element(values.begin(), values.end()));
        ImPlot::SetupAxesLimits(0.0, static_cast<double>(values.size() - 1), bottom, std::max(top, bottom + 1e-9),
                                ImPlotCond_Always);
        ImPlot::SetNextLineStyle(series_color(slot), 1.5f);
        ImPlot::PlotLine("##spark", values.data(), static_cast<int>(values.size()));
        ImPlot::EndPlot();
    }
    ImPlot::PopStyleColor(3);
    ImPlot::PopStyleVar();
}

void stat_tile(const char* id, const char* label, const std::string& value, std::optional<double> change,
               std::optional<bool> up_is_good, std::span<const double> series, float width) {
    ImGui::PushID(id);
    ImGui::BeginGroup();
    ImGui::TextDisabled("%s", label);
    ImGui::PushFont(heading_font());
    ImGui::TextUnformatted(value.c_str());
    ImGui::PopFont();
    if (change) {
        // The direction in words and an arrow; colored only when one way is good.
        const bool up = *change >= 0.0;
        char text[64];
        std::snprintf(text, sizeof text, "%s %.0f %%", up ? "Up" : "Down", std::abs(*change) * 100.0);
        if (up_is_good && std::abs(*change) >= 0.005) {
            // Good news in the success text green (a darker step on the light theme, for contrast).
            const ImVec4 good = dark() ? good_color() : rgb(0x006300);
            ImGui::TextColored(up == *up_is_good ? good : critical_color(), "%s %s", up ? "^" : "v", text);
        } else {
            ImGui::TextDisabled("%s", text);
        }
    } else {
        ImGui::TextDisabled("--");
    }
    sparkline("##spark", series, ImVec2(width, ImGui::GetTextLineHeight() * 1.6f));
    ImGui::EndGroup();
    ImGui::PopID();
}

} // namespace ninkasi::ui::charts
