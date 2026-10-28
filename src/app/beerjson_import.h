#pragma once
// Reads recipes and water profiles from BeerJSON (github.com/beerjson/beerjson),
// the JSON successor to BeerXML that brewing software exports.

#include "domain/recipe.h"

#include <string>
#include <string_view>
#include <vector>

namespace ninkasi::app {

struct BeerJsonImport {
    std::vector<domain::Recipe> recipes;
    std::vector<domain::WaterSource> waters;  // the document's water profiles, for the library
    std::vector<std::string> warnings;        // values that were skipped or assumed
};

// Reads every recipe and water profile in a BeerJSON document. Each recipe
// starts as a copy of `defaults` (equipment, pitch rate: things BeerJSON
// recipes don't carry) and takes everything the file provides; its water
// additions become its source water, blended by volume when there are
// several. Throws std::runtime_error if the text isn't JSON or has neither
// recipes nor water profiles.
BeerJsonImport import_beerjson(std::string_view text, const domain::Recipe& defaults);

} // namespace ninkasi::app
