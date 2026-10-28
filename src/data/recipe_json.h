#pragma once
// JSON storage format for recipes, library ingredients and calculation
// settings.
//
// Readers fill any missing field with its default and ignore fields they
// don't know, so data saved by older or newer versions still loads. Enums
// are stored as stable text keys. Malformed JSON throws DbError.

#include "domain/recipe.h"
#include "domain/recipe_math.h"

#include <string>
#include <string_view>

namespace ninkasi::data {

// The recipe's id is not part of the JSON; it is the database row's id.
std::string to_json(const domain::Recipe& recipe);
domain::Recipe recipe_from_json(std::string_view json);

std::string to_json(const domain::Fermentable& fermentable);
domain::Fermentable fermentable_from_json(std::string_view json);

std::string to_json(const domain::Hop& hop);
domain::Hop hop_from_json(std::string_view json);

std::string to_json(const domain::Yeast& yeast);
domain::Yeast yeast_from_json(std::string_view json);

std::string to_json(const domain::WaterSource& water);
domain::WaterSource water_source_from_json(std::string_view json);

std::string to_json(const domain::CalcSettings& settings);
domain::CalcSettings calc_settings_from_json(std::string_view json);

} // namespace ninkasi::data
