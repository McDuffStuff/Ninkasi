#include "domain/fuzzy_match.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <string>
#include <vector>

using ninkasi::domain::fuzzy_score;

namespace {

// The texts that match, best first.
std::vector<std::string> ranked(std::vector<std::string> texts, const std::string& search) {
    std::vector<std::pair<int, std::string>> scored;
    for (const std::string& text : texts) {
        if (const std::optional<int> score = fuzzy_score(text, search)) scored.emplace_back(*score, text);
    }
    std::stable_sort(scored.begin(), scored.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    std::vector<std::string> result;
    for (const auto& [score, text] : scored) result.push_back(text);
    return result;
}

} // namespace

TEST_CASE("a blank search matches everything") {
    CHECK(fuzzy_score("Pale Ale", "") == 0);
    CHECK(fuzzy_score("Pale Ale", "   ") == 0);
}

TEST_CASE("searches match as typed or with letters in between, ignoring case") {
    CHECK(fuzzy_score("West Coast IPA", "coast"));
    CHECK(fuzzy_score("West Coast IPA", "COAST"));
    CHECK(fuzzy_score("West Coast IPA", "wcipa"));
    CHECK(fuzzy_score("West Coast IPA", "wst"));
    CHECK(fuzzy_score("West Coast IPA", "ipa west"));  // words in any order
    CHECK_FALSE(fuzzy_score("West Coast IPA", "stout"));
    CHECK_FALSE(fuzzy_score("West Coast IPA", "ipa stout"));  // every word must match
    CHECK_FALSE(fuzzy_score("Pale Ale", "ipa"));
}

TEST_CASE("the most relevant match comes first") {
    const std::vector<std::string> recipes = {"Smoked Amber", "Pale Ale", "Amber Ale", "Hazy IPA",
                                              "West Coast IPA", "Oatmeal Stout", "Imperial Stout"};
    // A word start beats the middle of a word; earlier beats later.
    CHECK(ranked(recipes, "amb") == std::vector<std::string>{"Amber Ale", "Smoked Amber"});
    // Whole words as typed beat letters spread out.
    CHECK(ranked(recipes, "ipa").front() == "Hazy IPA");
    CHECK(ranked(recipes, "stout") == std::vector<std::string>{"Oatmeal Stout", "Imperial Stout"});
    // Initials find the beer.
    CHECK(ranked(recipes, "wci").front() == "West Coast IPA");
    CHECK(ranked(recipes, "imp st").front() == "Imperial Stout");
}
