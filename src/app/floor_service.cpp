#include "app/floor_service.h"

#include "data/audit_log.h"

#include <algorithm>
#include <charconv>
#include <stdexcept>
#include <string>
#include <utility>

namespace ninkasi::app {

namespace {

constexpr std::string_view kEntity = "element";

std::string_view trim(std::string_view text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) return {};
    const auto last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

domain::Rect with_minimum_size(domain::Rect rect) {
    rect.width = std::max(rect.width, domain::kMinElementSize);
    rect.height = std::max(rect.height, domain::kMinElementSize);
    return rect;
}

} // namespace

FloorService::FloorService(data::Database& db, Session session)
    : db_(db), session_(std::move(session)), repository_(db), elements_(repository_.load_all()) {}

const domain::Element* FloorService::find(std::int64_t id) const {
    const auto found = std::find_if(elements_.begin(), elements_.end(),
                                    [id](const domain::Element& element) { return element.id == id; });
    return found == elements_.end() ? nullptr : &*found;
}

domain::Element& FloorService::get(std::int64_t id) {
    for (domain::Element& element : elements_) {
        if (element.id == id) return element;
    }
    throw std::invalid_argument("there is no element with id " + std::to_string(id));
}

std::int64_t FloorService::add(domain::ElementType type, double center_x, double center_y) {
    const domain::ElementTypeInfo& type_info = domain::info(type);

    domain::Element element;
    element.type = type;
    element.name = default_name(type);
    element.rect = {center_x - type_info.default_width / 2.0, center_y - type_info.default_height / 2.0,
                    type_info.default_width, type_info.default_height};

    data::Transaction tx(db_);
    element.id = repository_.insert(element);
    data::record_audit(db_, session_.user_id, "element.create", kEntity, element.id,
                       std::string(type_info.name) + " \"" + element.name + "\"");
    tx.commit();

    elements_.push_back(std::move(element));
    return elements_.back().id;
}

void FloorService::set_rect(std::int64_t id, const domain::Rect& rect) {
    domain::Element updated = get(id);
    updated.rect = with_minimum_size(rect);
    repository_.update(updated);
    get(id) = std::move(updated);
}

void FloorService::rename(std::int64_t id, std::string_view name) {
    const std::string_view trimmed = trim(name);
    if (trimmed.empty()) throw std::invalid_argument("an element's name can't be blank");

    domain::Element updated = get(id);
    if (updated.name == trimmed) return;
    const std::string details = "renamed \"" + updated.name + "\" to \"" + std::string(trimmed) + "\"";
    updated.name = trimmed;

    data::Transaction tx(db_);
    repository_.update(updated);
    data::record_audit(db_, session_.user_id, "element.update", kEntity, id, details);
    tx.commit();
    get(id) = std::move(updated);
}

void FloorService::set_capacity(std::int64_t id, std::optional<double> capacity_l) {
    if (capacity_l && !(*capacity_l >= 0.0)) throw std::invalid_argument("capacity can't be negative");

    domain::Element updated = get(id);
    if (updated.capacity_l == capacity_l) return;
    updated.capacity_l = capacity_l;
    const std::string details = "capacity of \"" + updated.name + "\" set to " +
                                (capacity_l ? std::to_string(*capacity_l) + " L" : std::string("none"));

    data::Transaction tx(db_);
    repository_.update(updated);
    data::record_audit(db_, session_.user_id, "element.update", kEntity, id, details);
    tx.commit();
    get(id) = std::move(updated);
}

void FloorService::set_unit_override(std::int64_t id, calc::Dimension dimension, std::optional<calc::Unit> unit) {
    if (unit && calc::dimension_of(*unit) != dimension) {
        throw std::invalid_argument("the unit doesn't measure that dimension");
    }
    domain::Element& element = get(id);
    repository_.set_unit_override(id, dimension, unit);
    element.unit_overrides[calc::index_of(dimension)] = unit;
}

void FloorService::remove(std::int64_t id) {
    const domain::Element& element = get(id);
    const std::string details = std::string(domain::info(element.type).name) + " \"" + element.name + "\"";

    data::Transaction tx(db_);
    repository_.remove(id);
    data::record_audit(db_, session_.user_id, "element.delete", kEntity, id, details);
    tx.commit();

    std::erase_if(elements_, [id](const domain::Element& e) { return e.id == id; });
}

std::string FloorService::default_name(domain::ElementType type) const {
    // One more than the highest number already used in "<Type> <number>"
    // names, so deleting "Fermenter 2" never produces a second "Fermenter 3".
    const std::string prefix = std::string(domain::info(type).name) + " ";
    int highest = 0;
    for (const domain::Element& element : elements_) {
        if (element.type != type || !element.name.starts_with(prefix)) continue;
        const std::string_view digits = std::string_view(element.name).substr(prefix.size());
        int number = 0;
        const auto [end, error] = std::from_chars(digits.data(), digits.data() + digits.size(), number);
        if (error == std::errc() && end == digits.data() + digits.size()) highest = std::max(highest, number);
    }
    return prefix + std::to_string(highest + 1);
}

} // namespace ninkasi::app
