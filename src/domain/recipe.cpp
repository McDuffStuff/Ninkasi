#include "domain/recipe.h"

namespace ninkasi::domain {

bool is_mashed(FermentableType type) {
    return type == FermentableType::Grain || type == FermentableType::Adjunct;
}

std::string_view name(FermentableType type) {
    switch (type) {
    case FermentableType::Grain:      return "Grain";
    case FermentableType::Adjunct:    return "Adjunct";
    case FermentableType::Sugar:      return "Sugar";
    case FermentableType::Extract:    return "Liquid extract";
    case FermentableType::DryExtract: return "Dry extract";
    case FermentableType::Fruit:      return "Fruit";
    case FermentableType::Other:      return "Other";
    }
    return "Other";
}

std::string_view name(calc::MaltCategory category) {
    switch (category) {
    case calc::MaltCategory::Base:       return "Base";
    case calc::MaltCategory::Crystal:    return "Crystal";
    case calc::MaltCategory::Roasted:    return "Roasted";
    case calc::MaltCategory::Acidulated: return "Acidulated";
    }
    return "Base";
}

std::string_view name(calc::HopForm form) {
    switch (form) {
    case calc::HopForm::Pellet:  return "Pellet";
    case calc::HopForm::Leaf:    return "Leaf";
    case calc::HopForm::Plug:    return "Plug";
    case calc::HopForm::Powder:  return "Powder";
    case calc::HopForm::Extract: return "Extract";
    }
    return "Pellet";
}

std::string_view name(calc::HopUse use) {
    switch (use) {
    case calc::HopUse::Boil:      return "Boil";
    case calc::HopUse::FirstWort: return "First wort";
    case calc::HopUse::Whirlpool: return "Whirlpool";
    case calc::HopUse::Mash:      return "Mash";
    case calc::HopUse::DryHop:    return "Dry hop";
    }
    return "Boil";
}

std::string_view name(YeastType type) {
    switch (type) {
    case YeastType::Ale:   return "Ale";
    case YeastType::Lager: return "Lager";
    case YeastType::Other: return "Other";
    }
    return "Other";
}

std::string_view name(YeastForm form) {
    switch (form) {
    case YeastForm::Liquid: return "Liquid";
    case YeastForm::Dry:    return "Dry";
    case YeastForm::Slurry: return "Slurry";
    }
    return "Liquid";
}

std::string_view name(MiscUse use) {
    switch (use) {
    case MiscUse::Mash:      return "Mash";
    case MiscUse::Boil:      return "Boil";
    case MiscUse::Whirlpool: return "Whirlpool";
    case MiscUse::Fermenter: return "Fermenter";
    case MiscUse::Packaging: return "Packaging";
    }
    return "Boil";
}

std::string_view name(calc::StarterMethod method) {
    switch (method) {
    case calc::StarterMethod::StirPlate:   return "Stir plate";
    case calc::StarterMethod::Shaken:      return "Shaken";
    case calc::StarterMethod::NoAgitation: return "No agitation";
    }
    return "Stir plate";
}

} // namespace ninkasi::domain
