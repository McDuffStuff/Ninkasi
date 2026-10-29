#pragma once
// Charts for the Insights window, drawn with ImPlot the same way everywhere:
//
// - Bars for totals per period, lines for trends (with a rolling average),
//   horizontal bars for rankings, a heatmap for tank use. No pies, and one
//   value axis per chart.
// - Series take colors from one categorical palette in a fixed order, each
//   series keeping its slot (a channel is always the same color) whatever
//   else is shown. The palette was checked for color-blind separation and
//   contrast against Ninkasi's dark and light window backgrounds; in the
//   light theme some colors are faint, so every chart also has a table view
//   and shows exact values on hover.
// - A legend for two or more series; a single series is named by its title.
// - Magnitude in a heatmap is one hue, light to dark. Status colors (good,
//   warning...) are kept for status, always beside an icon or a word.

#include <imgui.h>

#include <cstddef>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace ninkasi::ui::charts {

// How a value reads: "$1,234", "12.3 bbl", "45 %".
using Format = std::function<std::string(double)>;

// The color of categorical slot `slot` (0 to 7, then round again), for the
// current theme.
ImVec4 series_color(int slot);
// The heatmap's ramp: `t` from 0 (none) to 1 (all), for the current theme.
ImVec4 sequential_color(double t);
// Status colors, the same in both themes.
ImVec4 good_color();
ImVec4 warning_color();
ImVec4 serious_color();
ImVec4 critical_color();
// Muted ink for labels and axes.
ImVec4 muted_ink();

struct Series {
    std::string name;
    int slot = 0;  // its color, by what it is
    std::vector<double> values;  // one per label
};

// The row above a chart: its title, a Table/Chart switch and an Export
// button, which writes the chart's table (from `csv`) to a CSV file.
// `as_table` is the switch, kept by the caller.
void chart_header(const char* title, bool& as_table, const std::function<std::string()>& csv);

// The chart's numbers as a table: a row per label, a column per series.
void series_table(const char* id, std::span<const std::string> labels, std::span<const Series> series,
                  const Format& format);
// Its CSV: a header row, then a row per label.
std::string series_csv(const char* first_column, std::span<const std::string> labels,
                       std::span<const Series> series);

// Bars per label, the series stacked in the order given (the first at the
// bottom). Hovering a bar shows its values. Returns the label clicked.
std::optional<std::size_t> stacked_bars(const char* id, std::span<const std::string> labels,
                                        std::span<const Series> series, const Format& format, float height);
// Bars per label, the series side by side.
std::optional<std::size_t> grouped_bars(const char* id, std::span<const std::string> labels,
                                        std::span<const Series> series, const Format& format, float height);
// Lines over the labels. With one series and `rolling` above 1, its
// rolling average over that many points is drawn too.
void line_chart(const char* id, std::span<const std::string> labels, std::span<const Series> series,
                const Format& format, float height, int rolling = 0);
// Horizontal bars, largest first as given, in `slot`'s color. Returns the
// row clicked.
std::optional<std::size_t> ranked_bars(const char* id, std::span<const std::string> labels,
                                       std::span<const double> values, const Format& format, int slot = 0);
// Rows by columns of shares from 0 to 1 (row-major), as a ramp of one hue.
void heatmap(const char* id, std::span<const std::string> rows, std::span<const std::string> columns,
             std::span<const double> values, float height);

// A small line with no axes, for a stat tile.
void sparkline(const char* id, std::span<const double> values, ImVec2 size, int slot = 0);

// A figure as a tile: its name, its value large, its change from the
// comparison period (an arrow and a word, colored only when `up_is_good`
// says which way is good) and a sparkline of `series`.
void stat_tile(const char* id, const char* label, const std::string& value, std::optional<double> change,
               std::optional<bool> up_is_good, std::span<const double> series, float width);

} // namespace ninkasi::ui::charts
