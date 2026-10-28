#include "app/floor_service.h"

#include "app/picture.h"
#include "data/audit_log.h"
#include "data/batch_repository.h"

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
    : db_(db), session_(std::move(session)), repository_(db), elements_(repository_.load_all()) {
    background_ = repository_.load_background();
    if (background_) {
        if (const std::optional<PictureSize> size = picture_size(background_->image)) {
            background_->pixel_width = size->width;
            background_->pixel_height = size->height;
        } else {
            background_.reset();  // unreadable: leave the floor plain
        }
    }
}

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
    require_admin(session_, "add equipment");
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
    require_admin(session_, "move equipment");
    domain::Element updated = get(id);
    updated.rect = with_minimum_size(rect);
    repository_.update(updated);
    get(id) = std::move(updated);
}

void FloorService::rename(std::int64_t id, std::string_view name) {
    require_admin(session_, "rename equipment");
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
    require_admin(session_, "change equipment's capacity");
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

std::optional<std::string> FloorService::removal_blocker(std::int64_t id) {
    const domain::Element& element = get(id);
    if (!data::BatchRepository(db_).in_use(id)) return std::nullopt;
    if (element.type == domain::ElementType::Brewhouse) {
        return element.name + " has a brew waiting for its knock-out. Log the knock-out or delete the brew first.";
    }
    return element.name + " holds beer. Move, package or dump it first.";
}

void FloorService::remove(std::int64_t id) {
    require_admin(session_, "remove equipment");
    if (const std::optional<std::string> blocker = removal_blocker(id)) throw std::invalid_argument(*blocker);
    const domain::Element& element = get(id);
    const std::string details = std::string(domain::info(element.type).name) + " \"" + element.name + "\"";

    data::Transaction tx(db_);
    repository_.remove(id);
    data::record_audit(db_, session_.user_id, "element.delete", kEntity, id, details);
    tx.commit();

    std::erase_if(elements_, [id](const domain::Element& e) { return e.id == id; });
}

void FloorService::set_background(std::vector<unsigned char> file, std::string_view file_name) {
    require_admin(session_, "change the floor's background");
    if (file.size() > kMaxBackgroundBytes) {
        throw std::invalid_argument("That picture is too big; choose one under 25 MB.");
    }
    const std::optional<PictureSize> size = picture_size(file);
    if (!size) throw std::invalid_argument("Ninkasi can't read that picture; choose a PNG, JPEG, BMP or GIF.");

    domain::FloorBackground background;
    background.image = std::move(file);
    background.file_name = std::string(file_name);
    background.pixel_width = size->width;
    background.pixel_height = size->height;
    if (background_) {
        // A replacement takes the old picture's place.
        background.x = background_->x;
        background.y = background_->y;
        background.width = background_->width;
        background.opacity = background_->opacity;
    } else {
        cover_equipment(background);
    }

    data::Transaction tx(db_);
    repository_.save_background(background);
    data::record_audit(db_, session_.user_id, "floor.background", "floor", 0, "background picture " + background.file_name);
    tx.commit();
    background_ = std::move(background);
    ++background_revision_;
}

void FloorService::place_background(double x, double y, double width, double opacity) {
    require_admin(session_, "move the floor's background");
    if (!background_) return;
    width = std::max(width, domain::kMinElementSize);
    opacity = std::clamp(opacity, 0.05, 1.0);
    repository_.place_background(x, y, width, opacity);  // layout, like moving equipment: not audited
    background_->x = x;
    background_->y = y;
    background_->width = width;
    background_->opacity = opacity;
}

void FloorService::fit_background() {
    require_admin(session_, "move the floor's background");
    if (!background_) return;
    domain::FloorBackground fitted = *background_;
    cover_equipment(fitted);
    place_background(fitted.x, fitted.y, fitted.width, fitted.opacity);
}

void FloorService::cover_equipment(domain::FloorBackground& background) const {
    if (elements_.empty()) {
        background.x = 0.0;
        background.y = 0.0;
        background.width = std::min(2000.0, static_cast<double>(background.pixel_width));
        return;
    }
    double left = elements_.front().rect.x;
    double top = elements_.front().rect.y;
    double right = left;
    double bottom = top;
    for (const domain::Element& element : elements_) {
        left = std::min(left, element.rect.x);
        top = std::min(top, element.rect.y);
        right = std::max(right, element.rect.x + element.rect.width);
        bottom = std::max(bottom, element.rect.y + element.rect.height);
    }
    constexpr double kMargin = 60.0;
    const double aspect = static_cast<double>(background.pixel_height) / static_cast<double>(background.pixel_width);
    background.width = std::max(right - left + 2.0 * kMargin, (bottom - top + 2.0 * kMargin) / aspect);
    background.x = (left + right) / 2.0 - background.width / 2.0;
    background.y = (top + bottom) / 2.0 - background.width * aspect / 2.0;
}

void FloorService::remove_background() {
    require_admin(session_, "remove the floor's background");
    if (!background_) return;
    data::Transaction tx(db_);
    repository_.remove_background();
    data::record_audit(db_, session_.user_id, "floor.background", "floor", 0, "background picture removed");
    tx.commit();
    background_.reset();
    ++background_revision_;
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
