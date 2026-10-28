#include "data/element_repository.h"

#include <string>

namespace ninkasi::data {

std::vector<domain::Element> ElementRepository::load_all() {
    std::vector<domain::Element> elements;

    auto stmt = db_.prepare("SELECT id, type, name, x, y, width, height, capacity_l FROM elements ORDER BY id;");
    while (stmt.step()) {
        const std::optional<domain::ElementType> type = domain::element_type_from_key(stmt.column_text(1));
        if (!type) continue;

        domain::Element element;
        element.id = stmt.column_int64(0);
        element.type = *type;
        element.name = stmt.column_text(2);
        element.rect = {stmt.column_double(3), stmt.column_double(4), stmt.column_double(5), stmt.column_double(6)};
        element.capacity_l = stmt.column_optional_double(7);
        elements.push_back(std::move(element));
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
    stmt.bind_optional(7, element.capacity_l);
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
    stmt.bind_optional(6, element.capacity_l);
    stmt.bind(7, element.id);
    stmt.execute();
}

void ElementRepository::remove(std::int64_t id) {
    auto stmt = db_.prepare("DELETE FROM elements WHERE id = ?;");
    stmt.bind(1, id);
    stmt.execute();
}

} // namespace ninkasi::data
