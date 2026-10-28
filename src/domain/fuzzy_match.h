#pragma once
// Fuzzy matching for search-as-you-type lists, such as the search field at
// the top of every dropdown.

#include <optional>
#include <string_view>

namespace ninkasi::domain {

// How well `text` matches a search the brewer typed: nullopt if it doesn't
// match, otherwise higher is better.
//
// Each word of the search must turn up in the text, ignoring case: as typed
// ("coast" in "West Coast IPA"), or with letters in between ("wcipa" or
// "wst"). Matches as typed beat ones with gaps, and among those, matches at
// the start of a word, of a whole word and nearer the start of the text
// score higher. Shorter texts win ties. A blank search matches everything
// with a score of 0.
std::optional<int> fuzzy_score(std::string_view text, std::string_view search);

} // namespace ninkasi::domain
