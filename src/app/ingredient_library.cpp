#include "app/ingredient_library.h"

#include "data/audit_log.h"
#include "data/settings_repository.h"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <utility>

namespace ninkasi::app {

namespace {

using calc::MaltCategory;
using domain::FermentableType;

constexpr const char* kSeededKey = "library.seeded";
constexpr std::string_view kEntity = "ingredient";

// Generic ingredients with typical values: a starting point, not a spec sheet.
domain::Fermentable malt(const char* name, FermentableType type, MaltCategory category, double yield, double lovibond) {
    domain::Fermentable f;
    f.name = name;
    f.type = type;
    f.category = category;
    f.yield = yield;
    f.color_lovibond = lovibond;
    return f;
}

const std::vector<domain::Fermentable>& starter_fermentables() {
    using enum FermentableType;
    using enum MaltCategory;
    static const std::vector<domain::Fermentable> kFermentables = {
        malt("Pilsner malt", Grain, Base, 0.80, 1.7),
        malt("Pale malt (2-row)", Grain, Base, 0.79, 1.8),
        malt("Pale ale malt", Grain, Base, 0.79, 3.0),
        malt("Maris Otter", Grain, Base, 0.79, 3.0),
        malt("Vienna malt", Grain, Base, 0.78, 3.5),
        malt("Munich malt (light)", Grain, Base, 0.77, 9.0),
        malt("Wheat malt", Grain, Base, 0.81, 2.0),
        malt("Rye malt", Grain, Base, 0.77, 3.5),
        malt("Dextrin malt", Grain, Crystal, 0.72, 1.5),
        malt("Crystal 15", Grain, Crystal, 0.74, 15.0),
        malt("Crystal 40", Grain, Crystal, 0.74, 40.0),
        malt("Crystal 60", Grain, Crystal, 0.74, 60.0),
        malt("Crystal 120", Grain, Crystal, 0.72, 120.0),
        malt("Chocolate malt", Grain, Roasted, 0.70, 350.0),
        malt("Roasted barley", Grain, Roasted, 0.68, 300.0),
        malt("Black malt", Grain, Roasted, 0.65, 500.0),
        malt("Acidulated malt", Grain, Acidulated, 0.58, 2.0),
        malt("Flaked oats", Adjunct, Base, 0.70, 1.0),
        malt("Flaked maize", Adjunct, Base, 0.80, 1.0),
        malt("Dextrose (corn sugar)", Sugar, Base, 0.91, 0.0),
        malt("Sucrose (table sugar)", Sugar, Base, 1.00, 0.0),
        malt("Light dry malt extract", DryExtract, Base, 0.95, 4.0),
        malt("Light liquid malt extract", Extract, Base, 0.78, 4.0),
    };
    return kFermentables;
}

const std::vector<domain::Hop>& starter_hops() {
    static const std::vector<domain::Hop> kHops = {
        {"Amarillo", "US", calc::HopForm::Pellet, 0.09},
        {"Cascade", "US", calc::HopForm::Pellet, 0.055},
        {"Centennial", "US", calc::HopForm::Pellet, 0.10},
        {"Chinook", "US", calc::HopForm::Pellet, 0.13},
        {"Citra", "US", calc::HopForm::Pellet, 0.12},
        {"Columbus", "US", calc::HopForm::Pellet, 0.15},
        {"East Kent Goldings", "UK", calc::HopForm::Pellet, 0.05},
        {"Fuggle", "UK", calc::HopForm::Pellet, 0.045},
        {"Galaxy", "AU", calc::HopForm::Pellet, 0.14},
        {"Hallertau Mittelfrüh", "DE", calc::HopForm::Pellet, 0.04},
        {"Magnum", "DE", calc::HopForm::Pellet, 0.13},
        {"Mosaic", "US", calc::HopForm::Pellet, 0.12},
        {"Nelson Sauvin", "NZ", calc::HopForm::Pellet, 0.12},
        {"Saaz", "CZ", calc::HopForm::Pellet, 0.035},
        {"Simcoe", "US", calc::HopForm::Pellet, 0.13},
        {"Tettnanger", "DE", calc::HopForm::Pellet, 0.045},
        {"Warrior", "US", calc::HopForm::Pellet, 0.16},
        {"Willamette", "US", calc::HopForm::Pellet, 0.05},
    };
    return kHops;
}

domain::Yeast yeast(const char* name, domain::YeastType type, domain::YeastForm form, double attenuation) {
    domain::Yeast y;
    y.name = name;
    y.type = type;
    y.form = form;
    y.attenuation = attenuation;
    return y;
}

const std::vector<domain::Yeast>& starter_yeasts() {
    using enum domain::YeastType;
    using enum domain::YeastForm;
    static const std::vector<domain::Yeast> kYeasts = {
        yeast("American ale (clean)", Ale, Liquid, 0.75),
        yeast("American ale, dry", Ale, Dry, 0.78),
        yeast("English ale", Ale, Liquid, 0.70),
        yeast("Kölsch / German ale", Ale, Liquid, 0.75),
        yeast("Belgian ale", Ale, Liquid, 0.76),
        yeast("Saison", Ale, Liquid, 0.85),
        yeast("Hefeweizen", Ale, Liquid, 0.74),
        yeast("German lager", Lager, Liquid, 0.75),
        yeast("Lager, dry", Lager, Dry, 0.80),
    };
    return kYeasts;
}

bool is_blank(const std::string& name) {
    return name.find_first_not_of(" \t\r\n") == std::string::npos;
}

} // namespace

IngredientLibrary::IngredientLibrary(data::Database& db, Session session)
    : db_(db), session_(std::move(session)), repository_(db) {
    seed_once();
    reload();
}

void IngredientLibrary::seed_once() {
    // A flag, not an emptiness check: a brewer who deletes everything
    // shouldn't get the starter set back.
    data::SettingsRepository settings(db_);
    if (settings.get(kSeededKey)) return;

    data::Transaction tx(db_);
    for (const domain::Fermentable& f : starter_fermentables()) repository_.insert(f);
    for (const domain::Hop& h : starter_hops()) repository_.insert(h);
    for (const domain::Yeast& y : starter_yeasts()) repository_.insert(y);
    settings.set(kSeededKey, "1");
    tx.commit();
}

void IngredientLibrary::reload() {
    fermentables_ = repository_.fermentables();
    hops_ = repository_.hops();
    yeasts_ = repository_.yeasts();
}

template <>
std::vector<domain::LibraryItem<domain::Fermentable>>& IngredientLibrary::items<domain::Fermentable>() {
    return fermentables_;
}
template <>
std::vector<domain::LibraryItem<domain::Hop>>& IngredientLibrary::items<domain::Hop>() {
    return hops_;
}
template <>
std::vector<domain::LibraryItem<domain::Yeast>>& IngredientLibrary::items<domain::Yeast>() {
    return yeasts_;
}

template <typename T>
std::int64_t IngredientLibrary::add_item(const T& item) {
    if (is_blank(item.name)) throw std::invalid_argument("an ingredient's name can't be blank");
    data::Transaction tx(db_);
    const std::int64_t id = repository_.insert(item);
    data::record_audit(db_, session_.user_id, "ingredient.create", kEntity, id, "\"" + item.name + "\"");
    tx.commit();
    reload();  // keeps each list in name order
    return id;
}

template <typename T>
void IngredientLibrary::update_item(std::int64_t id, const T& item) {
    if (is_blank(item.name)) throw std::invalid_argument("an ingredient's name can't be blank");
    auto& list = items<T>();
    const auto found = std::find_if(list.begin(), list.end(), [id](const auto& entry) { return entry.id == id; });
    if (found == list.end()) throw std::invalid_argument("there is no such ingredient");
    if (found->item == item) return;

    data::Transaction tx(db_);
    repository_.update(id, item);
    data::record_audit(db_, session_.user_id, "ingredient.update", kEntity, id, "\"" + item.name + "\"");
    tx.commit();
    found->item = item;
}

std::int64_t IngredientLibrary::add(const domain::Fermentable& f) { return add_item(f); }
std::int64_t IngredientLibrary::add(const domain::Hop& h) { return add_item(h); }
std::int64_t IngredientLibrary::add(const domain::Yeast& y) { return add_item(y); }

void IngredientLibrary::update(std::int64_t id, const domain::Fermentable& f) { update_item(id, f); }
void IngredientLibrary::update(std::int64_t id, const domain::Hop& h) { update_item(id, h); }
void IngredientLibrary::update(std::int64_t id, const domain::Yeast& y) { update_item(id, y); }

void IngredientLibrary::remove(std::int64_t id) {
    std::string name;
    auto find_name = [&](const auto& list) {
        for (const auto& entry : list) {
            if (entry.id == id) name = entry.item.name;
        }
    };
    find_name(fermentables_);
    find_name(hops_);
    find_name(yeasts_);
    if (name.empty()) return;

    data::Transaction tx(db_);
    repository_.remove(id);
    data::record_audit(db_, session_.user_id, "ingredient.delete", kEntity, id, "\"" + name + "\"");
    tx.commit();

    auto erase = [id](auto& list) { std::erase_if(list, [id](const auto& entry) { return entry.id == id; }); };
    erase(fermentables_);
    erase(hops_);
    erase(yeasts_);
}

} // namespace ninkasi::app
