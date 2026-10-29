#pragma once
// Amounts of money, in the brewery's one currency.

#include <string>

namespace ninkasi::domain {

// How the brewery writes money: "$1,234.50", "1.234,50 €", "CHF 12.00".
struct Currency {
    std::string symbol = "$";
    bool symbol_after = false;   // "12,00 €" rather than "€12,00"
    bool decimal_comma = false;  // "1.234,50" rather than "1,234.50"
    int decimals = 2;

    bool operator==(const Currency&) const = default;
};

// `amount` written in `currency`, with thousands grouped: "$1,234.50", and
// "−$12.00" for a negative amount. "--" if it isn't a number.
std::string format_money(double amount, const Currency& currency);

} // namespace ninkasi::domain
