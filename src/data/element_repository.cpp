#include "data/element_repository.h"

#include <string>
#include <unordered_map>

namespace ninkasi::data {

namespace {

void bind_optional(Statement& stmt, int index, std::optional<double> value) {
    if (value) {
        stmt.bind(index, *value);
    } else {
        stmt.bind_null(index);
    }
}

} // namespace

std::vector<domain::Element> ElementRepository::load_all() {
    std::vector<domain::Element> elements;
    std::unordered_map<std::int64_t, std::size_t> index_by_id;

    auto stmt = db_.prepare("SELECT id, type, name, x, y, width, height, capacity_l FROM elements ORDER BY id;");
    while (stmt.step()) {
        const std::optional<domain::ElementType> type = domain::element_type_from_key(stmt.column_text(1));
        if (!type) continue;

        domain::Element element;
        element.id = stmt.column_int64(0);
        element.type = *type;
        element.name = stmt.column_text(2);
        element.rect = {stmt.column_double(3), stmt.column_double(4), stmt.column_double(5), stmt.column_double(6)};
        if (!stmt.column_is_null(7)) element.capacity_l = stmt.column_double(7);

        index_by_id[element.id] = elements.size();
        elements.push_back(std::move(element));
    }

    auto overrides = db_.prepare("SELECT element_id, dimension, unit FROM element_unit_overrides;");
    while (overrides.step()) {
        const auto found = index_by_id.find(overrides.column_int64(0));
        const std::optional<calc::Dimension> dimension = calc::dimension_from_key(overrides.column_text(1));
        const std::optional<calc::Unit> unit = calc::unit_from_key(overrides.column_text(2));
        if (found == index_by_id.end() || !dimension || !unit || calc::dimension_of(*unit) != *dimension) continue;
        elements[found->second].unit_overrides[calc::index_of(*dimension)] = *unit;
    }
    return elements;
}

std::int64_t ElementRepository::insert(const domain::Element& element) {
    auto stmt = db_.prepare(
        "INSERT INTO elements (type, name, x, y, width, height, capacity_l) VALUES (?, ?, ?, ?, ?, ?, ?);");
    stmt.bind(1, domain::info(element.type).key)
        .bind(2, element.name)
        .bind(3, element.rect.x)
        .bind(4, element.rect.y)
        .bind(5, element.rect.width)
        .bind(6, element.rect.height);
    bind_optional(stmt, 7, element.capacity_l);
    stmt.execute();
    return db_.last_insert_id();
}

void ElementRepository::update(const domain::Element& element) {
    auto stmt = db_.prepare(
        "UPDATE elements SET name = ?, x = ?, y = ?, width = ?, height = ?, capacity_l = ? WHERE id = ?;");
    stmt.bind(1, element.name)
        .bind(2, element.rect.x)
        .bind(3, element.rect.y)
        .bind(4, element.rect.width)
        .bind(5, element.rect.height);
    bind_optional(stmt, 6, element.capacity_l);
    stmt.bind(7, element.id);
    stmt.execute();
}

void ElementRepository::remove(std::int64_t id) {
    auto stmt = db_.prepare("DELETE FROM elements WHERE id = ?;");
    stmt.bind(1, id);
    stmt.execute();
}

void ElementRepository::set_unit_override(std::int64_t element_id, calc::Dimension dimension,
                                          std::optional<calc::Unit> unit) {
    if (unit) {
        auto stmt = db_.prepare(
            "INSERT INTO element_unit_overrides (element_id, dimension, unit) VALUES (?, ?, ?) "
            "ON CONFLICT(element_id, dimension) DO UPDATE SET unit = excluded.unit;");
        stmt.bind(1, element_id).bind(2, calc::key(dimension)).bind(3, calc::key(*unit));
        stmt.execute();
    } else {
        auto stmt = db_.prepare("DELETE FROM element_unit_overrides WHERE element_id = ? AND dimension = ?;");
        stmt.bind(1, element_id).bind(2, calc::key(dimension));
        stmt.execute();
    }
}

} // namespace ninkasi::data
