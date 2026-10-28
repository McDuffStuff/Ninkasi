#include "domain/fuzzy_match.h"

#include <algorithm>
#include <cctype>
#include <string>

namespace ninkasi::domain {

namespace {

char lower(char c) {
    return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

bool is_word_char(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) != 0;
}

// Whether a word starts at `pos`: the text's start, after a space or
// punctuation, or a capital after a small letter ("BrewDog").
bool word_starts_at(std::string_view text, std::size_t pos) {
    if (pos == 0) return true;
    const char before = text[pos - 1];
    const char at = text[pos];
    if (!is_word_char(before)) return true;
    return std::isupper(static_cast<unsigned char>(at)) && std::islower(static_cast<unsigned char>(before));
}

// One word of the search against the text (`lowered` is the text in lower case).
std::optional<int> word_score(std::string_view text, const std::string& lowered, const std::string& word) {
    // As typed: the best place it appears.
    std::optional<int> best;
    for (std::size_t pos = lowered.find(word); pos != std::string::npos; pos = lowered.find(word, pos + 1)) {
        int score = 1000 - static_cast<int>(std::min<std::size_t>(pos, 400));
        if (word_starts_at(text, pos)) score += 500;
        const std::size_t end = pos + word.size();
        if (end == text.size() || !is_word_char(text[end])) score += 200;  // a whole word
        best = std::max(best.value_or(score), score);
    }
    if (best) return best;

    // With letters in between: each letter in turn, taking the next word
    // start that has it if there is one ("wcipa"), otherwise the next letter.
    int score = 0;
    std::size_t from = 0;
    std::size_t previous = std::string::npos;
    for (const char c : word) {
        std::size_t pos = lowered.find(c, from);
        if (pos == std::string::npos) return std::nullopt;
        if (previous == std::string::npos || pos != previous + 1) {
            for (std::size_t start = pos; start < lowered.size(); ++start) {
                if (lowered[start] == c && word_starts_at(text, start)) {
                    pos = start;
                    break;
                }
            }
        }
        score += 10;
        if (previous != std::string::npos && pos == previous + 1) score += 15;
        if (word_starts_at(text, pos)) score += 30;
        score -= static_cast<int>(std::min<std::size_t>(pos - from, 10));
        previous = pos;
        from = pos + 1;
    }
    return score;
}

} // namespace

std::optional<int> fuzzy_score(std::string_view text, std::string_view search) {
    std::string lowered(text);
    std::transform(lowered.begin(), lowered.end(), lowered.begin(), lower);

    int total = 0;
    bool any = false;
    std::size_t start = 0;
    while (start < search.size()) {
        // The next word of the search.
        while (start < search.size() && std::isspace(static_cast<unsigned char>(search[start]))) ++start;
        std::size_t end = start;
        while (end < search.size() && !std::isspace(static_cast<unsigned char>(search[end]))) ++end;
        if (end == start) break;
        std::string word(search.substr(start, end - start));
        std::transform(word.begin(), word.end(), word.begin(), lower);
        const std::optional<int> score = word_score(text, lowered, word);
        if (!score) return std::nullopt;
        total += *score;
        any = true;
        start = end;
    }
    if (!any) return 0;
    return total - static_cast<int>(std::min<std::size_t>(text.size(), 200));
}

} // namespace ninkasi::domain
