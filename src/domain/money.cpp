#include "domain/money.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>

namespace ninkasi::domain {

std::string format_money(double amount, const Currency& currency) {
    if (!std::isfinite(amount)) return "--";
    const int decimals = std::clamp(currency.decimals, 0, 4);
    char digits[64];
    std::snprintf(digits, sizeof digits, "%.*f", decimals, std::abs(amount));
    const std::string text = digits;
    const std::size_t point = text.find('.');
    const std::string whole = text.substr(0, point);
    const std::string fraction = point == std::string::npos ? "" : text.substr(point + 1);

    // Thousands grouped from the right.
    const char group = currency.decimal_comma ? '.' : ',';
    std::string grouped;
    for (std::size_t i = 0; i < whole.size(); ++i) {
        if (i > 0 && (whole.size() - i) % 3 == 0) grouped += group;
        grouped += whole[i];
    }
    std::string number = grouped;
    if (!fraction.empty()) number += (currency.decimal_comma ? ',' : '.') + fraction;

    // A zero that rounds to zero isn't negative.
    const bool negative = amount < 0.0 && number.find_first_not_of("0.,") != std::string::npos;
    const std::string& symbol = currency.symbol;
    std::string written;
    if (currency.symbol_after) {
        written = symbol.empty() ? number : number + " " + symbol;
    } else {
        // A symbol made of letters ("CHF", "kr") is set apart from the number.
        const bool word = !symbol.empty() && std::isalpha(static_cast<unsigned char>(symbol.back()));
        written = symbol + (word ? " " : "") + number;
    }
    return negative ? "−" + written : written;
}

} // namespace ninkasi::domain
