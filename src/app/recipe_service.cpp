#include "app/recipe_service.h"

#include "calc/units.h"
#include "data/audit_log.h"
#include "data/recipe_json.h"

#include <stdexcept>
#include <string>
#include <utility>

namespace ninkasi::app {

namespace {

constexpr const char* kDefaultsKey = "recipes.defaults";
constexpr std::string_view kEntity = "recipe";

void require_name(const domain::Recipe& recipe) {
    if (recipe.name.find_first_not_of(" \t\r\n") == std::string::npos) {
        throw std::invalid_argument("a recipe's name can't be blank");
    }
}

} // namespace

domain::Recipe built_in_recipe_defaults() {
    domain::Recipe recipe;
    recipe.batch_volume_l = 10.0 * calc::to_canonical(1.0, calc::Unit::UsBarrel);
    recipe.boil_time_min = 60.0;
    recipe.efficiency = 0.75;
    // Typical for a 10 bbl brewhouse; set your own under Recipe defaults.
    recipe.equipment.boil_off_l_per_hr = 70.0;
    recipe.equipment.kettle_loss_l = 60.0;
    recipe.equipment.mash_tun_loss_l = 20.0;
    recipe.mash_steps = {{"Saccharification", 67.0, 60.0}};
    return recipe;
}

RecipeService::RecipeService(data::Database& db, Session session)
    : db_(db), session_(std::move(session)), repository_(db), settings_(db) {}

std::vector<data::RecipeSummary> RecipeService::list() {
    return repository_.list();
}

std::optional<domain::Recipe> RecipeService::load(std::int64_t id) {
    return repository_.load(id);
}

domain::Recipe RecipeService::new_recipe(std::string_view name) {
    domain::Recipe recipe = defaults();
    recipe.id = 0;
    recipe.name = name;
    return recipe;
}

std::int64_t RecipeService::create(const domain::Recipe& recipe) {
    require_name(recipe);
    data::Transaction tx(db_);
    const std::int64_t id = repository_.insert(recipe);
    data::record_audit(db_, session_.user_id, "recipe.create", kEntity, id, "\"" + recipe.name + "\"");
    tx.commit();
    return id;
}

void RecipeService::save(const domain::Recipe& recipe) {
    require_name(recipe);
    data::Transaction tx(db_);
    repository_.update(recipe);
    data::record_audit(db_, session_.user_id, "recipe.update", kEntity, recipe.id, "\"" + recipe.name + "\"");
    tx.commit();
}

std::int64_t RecipeService::duplicate(std::int64_t id) {
    std::optional<domain::Recipe> recipe = repository_.load(id);
    if (!recipe) throw std::invalid_argument("there is no recipe with id " + std::to_string(id));
    recipe->name += " (copy)";
    return create(*recipe);
}

void RecipeService::remove(std::int64_t id) {
    const std::optional<domain::Recipe> recipe = repository_.load(id);
    if (!recipe) return;
    data::Transaction tx(db_);
    repository_.remove(id);
    data::record_audit(db_, session_.user_id, "recipe.delete", kEntity, id, "\"" + recipe->name + "\"");
    tx.commit();
}

domain::Recipe RecipeService::defaults() {
    const std::optional<std::string> saved = settings_.get(kDefaultsKey);
    if (!saved) return built_in_recipe_defaults();
    try {
        return data::recipe_from_json(*saved);
    } catch (const data::DbError&) {
        return built_in_recipe_defaults();
    }
}

void RecipeService::set_defaults(const domain::Recipe& defaults) {
    require_admin(session_, "change the defaults for new recipes");
    // Only the brewery-wide settings matter; drop any ingredients.
    domain::Recipe stored = defaults;
    stored.id = 0;
    stored.name.clear();
    stored.fermentables.clear();
    stored.hops.clear();
    stored.yeasts.clear();
    stored.miscs.clear();
    settings_.set(kDefaultsKey, data::to_json(stored));
}

} // namespace ninkasi::app
