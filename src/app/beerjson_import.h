#pragma once
// Reads recipes from BeerJSON (github.com/beerjson/beerjson), the JSON
// successor to BeerXML that brewing software exports.

#include "domain/recipe.h"

#include <string>
#include <string_view>
#include <vector>

namespace ninkasi::app {

struct BeerJsonImport {
    std::vector<domain::Recipe> recipes;
    std::vector<std::string> warnings;  // values that were skipped or assumed, per recipe
};

// Reads every recipe in a BeerJSON document. Each starts as a copy of
// `defaults` (equipment, water, pitch rate: things BeerJSON recipes don't
// carry) and takes everything the file provides. Throws std::runtime_error
// if the text isn't JSON or has no recipes.
BeerJsonImport import_beerjson(std::string_view text, const domain::Recipe& defaults);

} // namespace ninkasi::app
